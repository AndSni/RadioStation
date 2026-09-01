"""Guards the 'Genre: Rock, Rock, Rock' / 'Artist: X, X' class of bug: the
pipeline's tag writers must use replace semantics, so re-running a stage
(or running it over a file some other tagger already multi-valued) yields
exactly one value per core field, not an accumulating list.

Every writer traced to `setall` (ID3) or `audio[key] = value` (mutagen
easy-mode / VComment), both of which drop existing values first -- these
tests pin that so a future switch to `.add()`/append is caught.
"""

import shutil
from pathlib import Path

import mutagen
import mutagen.id3
from mutagen.flac import FLAC

from libcleanup import embed

FIXTURES_DIR = Path(__file__).resolve().parents[4] / "tests" / "audio" / "fixtures"


def test_embed_collapses_preexisting_multivalue_genre_flac(tmp_path):
    path = tmp_path / "s.flac"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.flac", path)

    # A prior tagger left GENRE tripled and ARTIST doubled.
    f = FLAC(path)
    f["genre"] = ["Rock", "Rock", "Rock"]
    f["artist"] = ["Portishead", "Portishead"]
    f.save()
    assert FLAC(path)["genre"] == ["Rock", "Rock", "Rock"]

    embed.write_tags(path, file_type="Song", energy=None, bpm=None, year=2001, genre="Rock", album="Dummy")

    f2 = FLAC(path)
    assert f2["genre"] == ["Rock"]        # collapsed
    assert f2["album"] == ["Dummy"]
    assert f2["date"] == ["2001"]


def test_embed_is_idempotent_across_repeat_runs_flac(tmp_path):
    path = tmp_path / "s.flac"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.flac", path)

    for _ in range(4):
        embed.write_tags(path, file_type="Song", energy=0.5, bpm=120.0, year=1994, genre="Trip-Hop", album="Dummy")

    f = FLAC(path)
    for key in ("genre", "album", "date", "bpm", "type"):
        assert len(f.get(key, [])) <= 1, f"{key} accumulated: {f.get(key)}"
    assert f["genre"] == ["Trip-Hop"]


def test_embed_collapses_multivalue_genre_frame_mp3(tmp_path):
    path = tmp_path / "s.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", path)

    tags = mutagen.id3.ID3()
    tags.setall("TCON", [mutagen.id3.TCON(encoding=3, text=["Rock", "Rock", "Rock"])])
    tags.setall("TPE1", [mutagen.id3.TPE1(encoding=3, text=["X", "X"])])
    tags.save(path)

    embed.write_tags(path, file_type="Song", energy=None, bpm=None, year=2001, genre="Rock", album="A")

    tags2 = mutagen.id3.ID3(path)
    assert len(tags2.getall("TCON")) == 1
    assert tags2.getall("TCON")[0].text == ["Rock"]
    assert len(tags2.getall("TALB")) == 1
    assert len(tags2.getall("TDRC")) == 1
    assert len(tags2.getall("TXXX:TYPE")) == 1

    # run twice more -- still exactly one of each
    for _ in range(2):
        embed.write_tags(path, file_type="Song", energy=None, bpm=None, year=2001, genre="Rock", album="A")
    tags3 = mutagen.id3.ID3(path)
    for key in ("TCON", "TALB", "TDRC", "TXXX:TYPE"):
        assert len(tags3.getall(key)) == 1, f"{key}: {tags3.getall(key)}"


def test_embed_leaves_multivalue_untouched_when_genre_unknown_flac(tmp_path):
    """Documents the known gap: embed only rewrites GENRE when a value is
    supplied. If retag found no catalog match (genre stays None), a
    pre-existing multi-valued GENRE is NOT cleaned by this pipeline."""
    path = tmp_path / "s.flac"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.flac", path)
    f = FLAC(path)
    f["genre"] = ["Rock", "Rock"]
    f.save()

    embed.write_tags(path, file_type="Song", energy=None, bpm=None, year=None, genre=None, album=None)

    assert FLAC(path)["genre"] == ["Rock", "Rock"]  # unchanged -- gap, by design
