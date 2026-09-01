"""Regression coverage for energy.py's fix: pass 2 must recompute the
min-max normalization range (and rewrite every row's score) across every
successfully-analyzed file in the library, not just rows scored in this
particular run -- otherwise scores from different runs sit on different,
incomparable scales. See energy.py's module docstring for the bug this
guards against.
"""

import json

from libcleanup import checkpoint, energy


def _insert_row(conn, path, energy_raw, existing_energy=None):
    conn.execute(
        "INSERT INTO files (path, size, mtime, type, dedup_status, needs_review, retag_done, "
        "title_dedup_done, energy_raw, energy) VALUES (?, 1, 1, 'Song', 'unique', 0, 1, 1, ?, ?)",
        (path, json.dumps(energy_raw), existing_energy),
    )


def test_energy_normalizes_across_whole_library_not_just_new_rows(tmp_path):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    old_path = str(library_root / "Old.mp3")
    new_path = str(library_root / "New.mp3")

    with checkpoint.connect(excluded_root) as conn:
        # "old_path" was normalized in a previous run when it was the only
        # file in the library -- min-max against itself alone gives 1.0 on
        # every feature, a stale score that must be recomputed once a
        # second file with a wider range exists.
        _insert_row(conn, old_path, {"rms": 0.2, "tempo": 100.0, "centroid": 1000.0}, existing_energy=1.0)
        # "new_path" was just extracted this run and has no score yet.
        _insert_row(conn, new_path, {"rms": 0.8, "tempo": 140.0, "centroid": 3000.0}, existing_energy=None)

    stats = energy.energy(library_root, excluded_root, workers=1, dry_run=False)

    # Both rows -- the already-scored one and the new one -- must be
    # revisited by pass 2, not just the new row.
    assert stats["normalized"] == 2

    with checkpoint.connect(excluded_root) as conn:
        old_row = conn.execute("SELECT energy FROM files WHERE path=?", (old_path,)).fetchone()
        new_row = conn.execute("SELECT energy FROM files WHERE path=?", (new_path,)).fetchone()

    # old_path is now the lower extreme across the combined range, so its
    # score must drop from its stale 1.0 down to 0.0 -- proof the range
    # was recomputed library-wide, not just over the newly-scored row.
    assert old_row["energy"] == 0.0
    assert new_row["energy"] == 1.0


def test_energy_leaves_failed_sentinel_rows_alone(tmp_path):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    ok_path = str(library_root / "Ok.mp3")
    failed_path = str(library_root / "Failed.mp3")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, ok_path, {"rms": 0.5, "tempo": 120.0, "centroid": 2000.0})
        conn.execute(
            "INSERT INTO files (path, size, mtime, type, dedup_status, needs_review, retag_done, "
            "title_dedup_done, energy_raw, energy) VALUES (?, 1, 1, 'Song', 'unique', 0, 1, 1, '{}', -1)",
            (failed_path,),
        )

    energy.energy(library_root, excluded_root, workers=1, dry_run=False)

    with checkpoint.connect(excluded_root) as conn:
        failed_row = conn.execute("SELECT energy FROM files WHERE path=?", (failed_path,)).fetchone()

    assert failed_row["energy"] == -1
