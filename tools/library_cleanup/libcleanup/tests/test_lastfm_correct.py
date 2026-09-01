"""Coverage for the Last.fm name-correction stage. No real network calls --
_lastfm_track_correction/_lastfm_artist_correction are monkeypatched to
return canned results, since these tests care about the tiering/apply
logic, not the actual HTTP+JSON parsing (which is a thin, defensively-
written wrapper covered by inspection, same as retag.py's Discogs call).
"""

import json

from libcleanup import checkpoint, lastfm_correct


def _insert_row(conn, path, artist, title, restructure_done=0):
    conn.execute(
        "INSERT INTO files (path, size, mtime, type, dedup_status, needs_review, retag_done, "
        "artist, title, restructure_done) VALUES (?, 1, 1, 'Song', 'unique', 0, 1, ?, ?, ?)",
        (path, artist, title, restructure_done),
    )


def test_near_identical_correction_auto_applies(tmp_path, monkeypatch):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    path = str(library_root / "Unknown" / "Song.mp3")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, path, "Guns N Roses", "Paradise City")

    monkeypatch.setattr(
        lastfm_correct, "_lastfm_track_correction",
        lambda apikey, artist, title: ("Guns N' Roses", "Paradise City"),
    )
    monkeypatch.setattr(lastfm_correct, "_lastfm_api_key", lambda: "fake-key")

    stats = lastfm_correct.correct_names(library_root, excluded_root, workers=1)

    assert stats["auto_applied"] == 1
    assert stats["reported"] == 0

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute("SELECT * FROM files WHERE path=?", (path,)).fetchone()
        assert row["artist"] == "Guns N' Roses"
        assert row["title"] == "Paradise City"
        assert row["lastfm_correct_done"] == 1

    assert not checkpoint.lastfm_correction_report_path(excluded_root).exists()


def test_meaningfully_different_correction_goes_to_report_only(tmp_path, monkeypatch):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    path = str(library_root / "Unknown" / "Song.mp3")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, path, "Some Artist", "Champange Supernova")

    monkeypatch.setattr(
        lastfm_correct, "_lastfm_track_correction",
        lambda apikey, artist, title: ("Some Artist", "Champagne Supernova"),
    )
    monkeypatch.setattr(lastfm_correct, "_lastfm_api_key", lambda: "fake-key")

    stats = lastfm_correct.correct_names(library_root, excluded_root, workers=1)

    assert stats["auto_applied"] == 0
    assert stats["reported"] == 1

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute("SELECT * FROM files WHERE path=?", (path,)).fetchone()
        # Untouched -- report-only, no auto-apply.
        assert row["title"] == "Champange Supernova"
        assert row["lastfm_correct_done"] == 1

    report = json.loads(checkpoint.lastfm_correction_report_path(excluded_root).read_text())
    assert len(report) == 1
    assert report[0]["suggested_title"] == "Champagne Supernova"
    assert report[0]["files"] == [path]


def test_no_correction_found_just_marks_done(tmp_path, monkeypatch):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    path = str(library_root / "Unknown" / "Song.mp3")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, path, "Obscure Act", "Obscure Song")

    monkeypatch.setattr(lastfm_correct, "_lastfm_track_correction", lambda apikey, artist, title: None)
    monkeypatch.setattr(lastfm_correct, "_lastfm_artist_correction", lambda apikey, artist: None)
    monkeypatch.setattr(lastfm_correct, "_lastfm_api_key", lambda: "fake-key")

    stats = lastfm_correct.correct_names(library_root, excluded_root, workers=1)

    assert stats["no_correction"] == 1
    assert stats["auto_applied"] == 0
    assert stats["reported"] == 0

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute("SELECT * FROM files WHERE path=?", (path,)).fetchone()
        assert row["artist"] == "Obscure Act"
        assert row["lastfm_correct_done"] == 1


def test_already_restructured_row_gets_physically_moved(tmp_path, monkeypatch):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    old_dir = library_root / "Guns N Roses"
    old_dir.mkdir()
    old_path = old_dir / "Guns N Roses - Paradise City.mp3"
    old_path.write_bytes(b"fake audio")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, str(old_path), "Guns N Roses", "Paradise City", restructure_done=1)

    monkeypatch.setattr(
        lastfm_correct, "_lastfm_track_correction",
        lambda apikey, artist, title: ("Guns N' Roses", "Paradise City"),
    )
    monkeypatch.setattr(lastfm_correct, "_lastfm_api_key", lambda: "fake-key")

    stats = lastfm_correct.correct_names(library_root, excluded_root, workers=1)

    assert stats["auto_applied"] == 1
    assert stats["files_moved"] == 1
    assert not old_path.exists()
    new_path = library_root / "Guns N' Roses" / "Guns N' Roses - Paradise City.mp3"
    assert new_path.exists()
    assert not old_dir.exists()  # pruned, now empty

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute("SELECT * FROM files WHERE path=?", (str(new_path),)).fetchone()
        assert row is not None
        assert row["artist"] == "Guns N' Roses"


def test_manual_apply_applies_a_reviewed_report_entry(tmp_path):
    library_root = tmp_path / "Music"
    excluded_root = tmp_path / "Excluded"
    library_root.mkdir()
    excluded_root.mkdir()

    path = str(library_root / "Unknown" / "Champange.mp3")

    with checkpoint.connect(excluded_root) as conn:
        _insert_row(conn, path, "Some Artist", "Champange Supernova")

    stats = lastfm_correct.manual_apply(
        library_root, excluded_root,
        from_artist="Some Artist", from_title="Champange Supernova",
        to_artist="Some Artist", to_title="Champagne Supernova",
    )

    assert stats["rows_updated"] == 1

    with checkpoint.connect(excluded_root) as conn:
        row = conn.execute(
            "SELECT * FROM files WHERE artist=? AND title=?", ("Some Artist", "Champagne Supernova")
        ).fetchone()
        assert row is not None
