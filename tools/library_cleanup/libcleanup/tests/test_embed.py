"""Regression coverage for embed.py's RS_VERIFIED_FIELDS marker: RadioStation
only trusts an embedded BPM/ReplayGain tag when this marker also names it
(see embed.py's module-level doc comment and RadioStation's own
src/audio/VerifiedFieldsTag.h). This pipeline is one of the two trusted
writers, so every time it writes a BPM value it must also merge "BPM" into
the marker -- and merge, not overwrite, since RadioStation's own
ReplayGainAnalyzer may have already written "REPLAYGAIN" there first.
"""

import shutil
from pathlib import Path

import mutagen
import mutagen.id3

from libcleanup import embed

FIXTURES_DIR = Path(__file__).resolve().parents[4] / "tests" / "audio" / "fixtures"


def _verified_fields_id3(path: Path) -> set[str]:
    tags = mutagen.id3.ID3(path)
    frame = tags.get(f"TXXX:{embed.VERIFIED_FIELDS_KEY}")
    if frame is None:
        return set()
    return {token.strip() for token in frame.text[0].split(",") if token.strip()}


def _verified_fields_vorbis(path: Path) -> set[str]:
    audio = mutagen.File(path)
    values = audio.tags.get(embed.VERIFIED_FIELDS_KEY, [])
    if not values:
        return set()
    return {token.strip() for token in values[0].split(",") if token.strip()}


def test_write_tags_marks_bpm_verified_for_mp3(tmp_path):
    path = tmp_path / "song.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", path)

    assert embed.write_tags(path, file_type="Song", energy=0.5, bpm=128.0, year=None, genre=None)

    assert _verified_fields_id3(path) == {"BPM"}


def test_write_tags_marks_bpm_verified_for_flac(tmp_path):
    path = tmp_path / "song.flac"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.flac", path)

    assert embed.write_tags(path, file_type="Song", energy=0.5, bpm=128.0, year=None, genre=None)

    assert _verified_fields_vorbis(path) == {"BPM"}


def test_write_tags_merges_rather_than_overwrites_existing_marker_mp3(tmp_path):
    path = tmp_path / "song.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", path)

    # Simulates RadioStation's own ReplayGainAnalyzer having already
    # verified this file before library_cleanup ever touches it.
    try:
        tags = mutagen.id3.ID3(path)
    except mutagen.id3.ID3NoHeaderError:
        tags = mutagen.id3.ID3()
    tags.setall(
        f"TXXX:{embed.VERIFIED_FIELDS_KEY}",
        [mutagen.id3.TXXX(encoding=3, desc=embed.VERIFIED_FIELDS_KEY, text=["REPLAYGAIN"])],
    )
    tags.save(path)

    assert embed.write_tags(path, file_type="Song", energy=0.5, bpm=128.0, year=None, genre=None)

    assert _verified_fields_id3(path) == {"BPM", "REPLAYGAIN"}


def test_write_tags_without_bpm_does_not_touch_marker(tmp_path):
    path = tmp_path / "song.mp3"
    shutil.copyfile(FIXTURES_DIR / "tone_untagged.mp3", path)

    assert embed.write_tags(path, file_type="Song", energy=0.5, bpm=None, year=None, genre=None)

    assert _verified_fields_id3(path) == set()
