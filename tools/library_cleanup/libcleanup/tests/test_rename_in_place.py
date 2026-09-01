"""rename_in_place renames survivors to "<Artist> - <Title>.ext" inside
their existing folder and never moves them into an <Artist>/ tree or prunes
directories -- the property restructure.py deliberately doesn't have, needed
for a hand-curated <Genre>/<Artist>/ layout (a portable-player copy).
"""

import shutil
from pathlib import Path

import mutagen

from libcleanup import checkpoint, rename_in_place

FIXTURES_DIR = Path(__file__).resolve().parents[4] / "tests" / "audio" / "fixtures"

_INSERT = (
    "INSERT INTO files (path, size, mtime, dedup_status, type, retag_done, "
    "needs_review, restructure_done, artist, title, genre) "
    "VALUES (?, 1, 1, 'unique', 'Song', 1, 0, 0, ?, ?, ?)"
)


def _mk(tmp_path):
    lib = tmp_path / "Music_DAP"
    exc = tmp_path / "pipeline"
    lib.mkdir()
    exc.mkdir()
    return lib, exc


def _seed(exc, path, artist, title, genre=None):
    with checkpoint.connect(exc) as conn:
        conn.execute(_INSERT, (str(path), artist, title, genre))


def test_renames_in_place_without_moving_or_pruning(tmp_path):
    lib, exc = _mk(tmp_path)
    folder = lib / "Trip-Hop" / "Portishead"
    folder.mkdir(parents=True)
    src = folder / "01 - portishead - mysterons (2011 remaster).mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", src)
    _seed(exc, src, "Portishead", "Mysterons", genre="Trip-Hop")

    stats = rename_in_place.rename_in_place(lib, exc)

    dest = folder / "Portishead - Mysterons.mp3"
    assert dest.exists()
    assert not src.exists()
    assert folder.is_dir()                       # hand-curated tree untouched
    assert (lib / "Trip-Hop").is_dir()
    assert not (lib / "Portishead").exists()     # no <Artist>/ tree created at root
    assert stats["renamed"] == 1
    assert stats["tags_embedded"] == 1

    with checkpoint.connect(exc) as conn:
        row = conn.execute("SELECT * FROM files WHERE path=?", (str(dest),)).fetchone()
    assert row is not None
    assert row["restructure_done"] == 1
    assert row["final_relative_path"] == "Trip-Hop/Portishead/Portishead - Mysterons.mp3"
    assert mutagen.File(dest, easy=True).get("genre") == ["Trip-Hop"]


def test_missing_artist_tag_leaves_filename_untouched(tmp_path):
    lib, exc = _mk(tmp_path)
    folder = lib / "Downtempo & Lo-Fi" / "_Unsorted"
    folder.mkdir(parents=True)
    src = folder / "weird original name.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", src)
    _seed(exc, src, None, "Something")

    stats = rename_in_place.rename_in_place(lib, exc)

    assert src.exists()                          # untouched
    assert stats["skipped_no_artist"] == 1
    assert stats["renamed"] == 0
    with checkpoint.connect(exc) as conn:
        row = conn.execute("SELECT restructure_done FROM files WHERE path=?", (str(src),)).fetchone()
    assert row["restructure_done"] == 1         # marked done, won't re-appear


def test_name_collision_gets_suffix(tmp_path):
    lib, exc = _mk(tmp_path)
    folder = lib / "Trip-Hop" / "Portishead"
    folder.mkdir(parents=True)
    for name in ("a.mp3", "b.mp3"):
        p = folder / name
        shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", p)
        _seed(exc, p, "Portishead", "Roads")

    rename_in_place.rename_in_place(lib, exc)

    assert sorted(p.name for p in folder.iterdir()) == [
        "Portishead - Roads.mp3",
        "Portishead - Roads__1.mp3",
    ]


def test_already_correctly_named_is_noop(tmp_path):
    lib, exc = _mk(tmp_path)
    folder = lib / "Trip-Hop" / "Portishead"
    folder.mkdir(parents=True)
    src = folder / "Portishead - Roads.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", src)
    _seed(exc, src, "Portishead", "Roads")

    stats = rename_in_place.rename_in_place(lib, exc)

    assert src.exists()
    assert stats["already_named"] == 1
    assert stats["renamed"] == 0
    with checkpoint.connect(exc) as conn:
        row = conn.execute("SELECT restructure_done FROM files WHERE path=?", (str(src),)).fetchone()
    assert row["restructure_done"] == 1


def test_monster_artist_credit_is_length_capped(tmp_path):
    """retag/beets can return a 250-char ';'-joined performer credit as the
    artist -- '<Artist> - <Title>.ext' then blows past NAME_MAX and shutil
    dies with OSError [Errno 36]. The stem must be trimmed (artist first,
    title kept) so the rename still succeeds."""
    lib, exc = _mk(tmp_path)
    folder = lib / "Country_Western"
    folder.mkdir(parents=True)
    src = folder / "08. Darius Rucker - Wagon Wheel.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", src)
    monster = "Darius Rucker;" + ";".join(f"Session Player {i}" for i in range(40))
    _seed(exc, src, monster, "Wagon Wheel")

    stats = rename_in_place.rename_in_place(lib, exc)

    assert stats["renamed"] == 1
    out = next(p for p in folder.iterdir() if p.suffix == ".mp3")
    assert len(out.name.encode("utf-8")) <= 255
    assert out.name.startswith("Darius Rucker;")
    assert out.name.endswith("Wagon Wheel.mp3")   # title + ext survive intact
    assert not src.exists()


def test_dry_run_changes_nothing(tmp_path):
    lib, exc = _mk(tmp_path)
    folder = lib / "Trip-Hop" / "Portishead"
    folder.mkdir(parents=True)
    src = folder / "messy.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", src)
    _seed(exc, src, "Portishead", "Roads")

    stats = rename_in_place.rename_in_place(lib, exc, dry_run=True)

    assert src.exists()
    assert stats["renamed"] == 1
    with checkpoint.connect(exc) as conn:
        row = conn.execute("SELECT restructure_done FROM files WHERE path=?", (str(src),)).fetchone()
    assert row["restructure_done"] == 0        # not committed
