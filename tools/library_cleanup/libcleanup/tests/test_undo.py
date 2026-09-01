"""Regression coverage for undo.py's fix: undoing one title_dedup_move
entry must only strip its own entry from the keeper's duplicate_of list
(not blank the whole field), and must reset title_dedup_done so the
restored file re-enters title_dedup.py's own grouping query. See
undo.py's module docstring for the bug this guards against.
"""

import json

from libcleanup import checkpoint, undo


def test_undo_title_dedup_move_restores_one_entry_and_resets_flag(tmp_path):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    keeper_path = str(library_root / "Artist" / "Song.mp3")
    loser_original = library_root / "Other" / "Song (dup).mp3"
    loser_dest = excluded_root / "Duplicates" / "Other" / "Song (dup).mp3"
    other_loser_relative = "Other2/Song (dup2).mp3"
    this_loser_relative = "Other/Song (dup).mp3"

    loser_dest.parent.mkdir(parents=True)
    loser_dest.write_bytes(b"fake audio")

    with checkpoint.connect(excluded_root) as conn:
        conn.execute(
            "INSERT INTO files (path, size, mtime, dedup_status, title_dedup_done, duplicate_of) "
            "VALUES (?, 1, 1, 'keeper', 1, ?)",
            (keeper_path, f"{this_loser_relative};{other_loser_relative}"),
        )
        conn.execute(
            "INSERT INTO files (path, size, mtime, dedup_status, title_dedup_done, "
            "restructure_done) VALUES (?, 1, 1, 'title_loser', 1, 0)",
            (str(loser_dest),),
        )

    log_path = checkpoint.undo_log_path(excluded_root)
    log_path.write_text(json.dumps({
        "action": "title_dedup_move",
        "from": str(loser_original),
        "to": str(loser_dest),
        "keeper": keeper_path,
        "artist": "artist",
        "title": "song",
        "timestamp": "2026-01-01T00:00:00+00:00",
    }) + "\n")

    stats = undo.undo(log_path, library_root, excluded_root)

    assert stats == {"restored": 1, "skipped": 0}
    assert loser_original.exists()
    assert not loser_dest.exists()

    with checkpoint.connect(excluded_root) as conn:
        loser_row = conn.execute("SELECT * FROM files WHERE path=?", (str(loser_original),)).fetchone()
        assert loser_row["dedup_status"] is None
        assert loser_row["title_dedup_done"] == 0

        keeper_row = conn.execute("SELECT duplicate_of FROM files WHERE path=?", (keeper_path,)).fetchone()
        # Only this undo's own entry should be gone -- the other loser's
        # record must survive.
        assert keeper_row["duplicate_of"] == other_loser_relative


def test_undo_dedup_move_leaves_title_dedup_done_untouched(tmp_path):
    """A plain dedup_move (fingerprint dedup, pre-retag) shouldn't force a
    re-run of title_dedup -- there's nothing wrong with reusing tags/energy
    a re-promoted file already had before demotion."""
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    keeper_path = str(library_root / "Artist" / "Song.mp3")
    loser_original = library_root / "Other" / "Song (dup).mp3"
    loser_dest = excluded_root / "Duplicates" / "Other" / "Song (dup).mp3"

    loser_dest.parent.mkdir(parents=True)
    loser_dest.write_bytes(b"fake audio")

    with checkpoint.connect(excluded_root) as conn:
        conn.execute(
            "INSERT INTO files (path, size, mtime, dedup_status) VALUES (?, 1, 1, 'keeper')",
            (keeper_path,),
        )
        conn.execute(
            "INSERT INTO files (path, size, mtime, dedup_status, title_dedup_done) "
            "VALUES (?, 1, 1, 'loser', 1)",
            (str(loser_dest),),
        )

    log_path = checkpoint.undo_log_path(excluded_root)
    log_path.write_text(json.dumps({
        "action": "dedup_move",
        "from": str(loser_original),
        "to": str(loser_dest),
        "keeper": keeper_path,
        "cluster_id": "abc",
        "score": 0.99,
        "timestamp": "2026-01-01T00:00:00+00:00",
    }) + "\n")

    undo.undo(log_path, library_root, excluded_root)

    with checkpoint.connect(excluded_root) as conn:
        loser_row = conn.execute("SELECT * FROM files WHERE path=?", (str(loser_original),)).fetchone()
        assert loser_row["dedup_status"] is None
        assert loser_row["title_dedup_done"] == 1


def test_undo_missing_keeper_logs_warning_not_crash(tmp_path):
    """If the keeper's path changed since the log entry was written (e.g.
    restructured/renamed after the move), undo should still restore the
    file and just warn -- not crash or silently do nothing."""
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    loser_original = library_root / "Other" / "Song (dup).mp3"
    loser_dest = excluded_root / "Duplicates" / "Other" / "Song (dup).mp3"
    loser_dest.parent.mkdir(parents=True)
    loser_dest.write_bytes(b"fake audio")

    with checkpoint.connect(excluded_root) as conn:
        conn.execute(
            "INSERT INTO files (path, size, mtime, dedup_status) VALUES (?, 1, 1, 'loser')",
            (str(loser_dest),),
        )

    log_path = checkpoint.undo_log_path(excluded_root)
    log_path.write_text(json.dumps({
        "action": "dedup_move",
        "from": str(loser_original),
        "to": str(loser_dest),
        "keeper": str(library_root / "Artist" / "Renamed Since.mp3"),
        "cluster_id": "abc",
        "score": 0.99,
        "timestamp": "2026-01-01T00:00:00+00:00",
    }) + "\n")

    stats = undo.undo(log_path, library_root, excluded_root)

    assert stats == {"restored": 1, "skipped": 0}
    assert loser_original.exists()
