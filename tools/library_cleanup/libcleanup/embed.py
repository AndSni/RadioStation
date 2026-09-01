"""Writes Type/Energy/BPM/Year/Genre/Album directly into each file's own
tags, so the data is portable to any tool that reads standard/custom audio
tags -- not locked into this pipeline's manifest.csv or RadioStation's own
DB.

Genre/Year/Album use standard tag fields (only written when known, i.e.
beets found a MusicBrainz match). BPM also uses a standard field but, like
Type/Energy, is pipeline-computed and always written when known. Type/Energy
have no standard field, so they're written as custom fields: ID3 TXXX frames
(MP3), plain Vorbis comment keys (FLAC/OGG/Opus -- these support arbitrary
keys natively), or MP4 freeform atoms (M4A/M4B/M4R).

restructure.py calls write_tags() exactly once per file, at the moment it
first moves that file into /Music -- there was previously no way for a
later metadata improvement (e.g. a better BPM detector) to ever reach a
file already sitting in /Music. embed_all() below is the fix: an idempotent
stage that re-embeds every current survivor's tags regardless of
restructure history, safe to re-run any time upstream data changes.
"""

from pathlib import Path

import mutagen
import mutagen.id3
import mutagen.mp4

VORBIS_EXTENSIONS = {".flac", ".ogg", ".opus"}
ID3_EXTENSIONS = {".mp3"}
MP4_EXTENSIONS = {".m4a", ".m4b", ".m4r"}

TYPE_FREEFORM = "----:com.radiostation.libcleanup:TYPE"
ENERGY_FREEFORM = "----:com.radiostation.libcleanup:ENERGY"


def _energy_str(energy: float | None) -> str | None:
    return f"{energy:.3f}" if energy is not None and energy >= 0 else None


def _write_id3(path: Path, file_type, energy, bpm, year, genre, album) -> None:
    try:
        tags = mutagen.id3.ID3(path)
    except mutagen.id3.ID3NoHeaderError:
        tags = mutagen.id3.ID3()

    tags.setall("TXXX:TYPE", [mutagen.id3.TXXX(encoding=3, desc="TYPE", text=[file_type])])
    energy_str = _energy_str(energy)
    if energy_str is not None:
        tags.setall("TXXX:ENERGY", [mutagen.id3.TXXX(encoding=3, desc="ENERGY", text=[energy_str])])
    if bpm is not None:
        tags.setall("TBPM", [mutagen.id3.TBPM(encoding=3, text=[str(round(bpm))])])
    if year:
        tags.setall("TDRC", [mutagen.id3.TDRC(encoding=3, text=[str(year)])])
    if genre:
        tags.setall("TCON", [mutagen.id3.TCON(encoding=3, text=[genre])])
    if album:
        tags.setall("TALB", [mutagen.id3.TALB(encoding=3, text=[album])])

    tags.save(path)


def _write_vorbis(path: Path, file_type, energy, bpm, year, genre, album) -> None:
    audio = mutagen.File(path)
    if audio is None:
        return
    if audio.tags is None:
        audio.add_tags()

    audio.tags["TYPE"] = [file_type]
    energy_str = _energy_str(energy)
    if energy_str is not None:
        audio.tags["ENERGY"] = [energy_str]
    if bpm is not None:
        audio.tags["BPM"] = [str(round(bpm))]
    if year:
        audio.tags["DATE"] = [str(year)]
    if genre:
        audio.tags["GENRE"] = [genre]
    if album:
        audio.tags["ALBUM"] = [album]

    audio.save()


def _write_mp4(path: Path, file_type, energy, bpm, year, genre, album) -> None:
    audio = mutagen.mp4.MP4(path)
    if audio.tags is None:
        audio.add_tags()

    audio.tags[TYPE_FREEFORM] = [file_type.encode("utf-8")]
    energy_str = _energy_str(energy)
    if energy_str is not None:
        audio.tags[ENERGY_FREEFORM] = [energy_str.encode("utf-8")]
    if bpm is not None:
        audio.tags["tmpo"] = [round(bpm)]
    if year:
        audio.tags["\xa9day"] = [str(year)]
    if genre:
        audio.tags["\xa9gen"] = [genre]
    if album:
        audio.tags["\xa9alb"] = [album]

    audio.save()


def write_tags(
    path: Path, *, file_type: str, energy: float | None, bpm: float | None, year: int | None, genre: str | None,
    album: str | None = None,
) -> bool:
    """Embeds Type (always) / Energy+BPM (if given) / Year+Genre+Album (if
    known) directly into the file. Returns False (no-op) for formats
    mutagen can't write custom fields into (e.g. webm) rather than raising."""
    suffix = path.suffix.lower()
    try:
        if suffix in ID3_EXTENSIONS:
            _write_id3(path, file_type, energy, bpm, year, genre, album)
        elif suffix in VORBIS_EXTENSIONS:
            _write_vorbis(path, file_type, energy, bpm, year, genre, album)
        elif suffix in MP4_EXTENSIONS:
            _write_mp4(path, file_type, energy, bpm, year, genre, album)
        else:
            return False
    except Exception:  # noqa: BLE001 - tag-embedding failure shouldn't abort restructure
        return False
    return True


# Stray leftover from a now-reverted experiment (RadioStation briefly had
# its own analyzers that coordinated with this pipeline via a custom
# RS_VERIFIED_FIELDS trust tag) -- stripped defensively wherever found so no
# file keeps a marker for a mechanism that no longer exists on either side.
_STRAY_VERIFIED_FIELDS_KEY = "RS_VERIFIED_FIELDS"


def _strip_id3_bpm_energy(path: Path) -> bool:
    try:
        tags = mutagen.id3.ID3(path)
    except mutagen.id3.ID3NoHeaderError:
        return False
    removed = False
    for key in ("TBPM", "TXXX:ENERGY", f"TXXX:{_STRAY_VERIFIED_FIELDS_KEY}"):
        if key in tags:
            del tags[key]
            removed = True
    if removed:
        tags.save(path)
    return removed


def _strip_vorbis_bpm_energy(path: Path) -> bool:
    audio = mutagen.File(path)
    if audio is None or audio.tags is None:
        return False
    removed = False
    for key in ("BPM", "ENERGY", _STRAY_VERIFIED_FIELDS_KEY):
        if key in audio.tags:
            del audio.tags[key]
            removed = True
    if removed:
        audio.save()
    return removed


def _strip_mp4_bpm_energy(path: Path) -> bool:
    audio = mutagen.mp4.MP4(path)
    if audio.tags is None:
        return False
    removed = False
    for key in ("tmpo", ENERGY_FREEFORM):
        if key in audio.tags:
            del audio.tags[key]
            removed = True
    if removed:
        audio.save()
    return removed


def strip_bpm_energy(path: Path) -> bool:
    """Removes BPM/Energy (and any stray verified-fields marker) from a
    file's tags, for wipe_bpm_energy's clean-slate recompute. Returns
    whether anything was actually removed -- a no-op file (never analyzed,
    or an unsupported format) returns False rather than raising."""
    suffix = path.suffix.lower()
    try:
        if suffix in ID3_EXTENSIONS:
            return _strip_id3_bpm_energy(path)
        if suffix in VORBIS_EXTENSIONS:
            return _strip_vorbis_bpm_energy(path)
        if suffix in MP4_EXTENSIONS:
            return _strip_mp4_bpm_energy(path)
    except Exception:  # noqa: BLE001 - one bad file shouldn't stop the batch
        return False
    return False


def embed_all(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    """Re-embeds every current survivor's tags from its present DB values,
    regardless of restructure history -- reaches files restructured before
    a metadata fix landed, not just freshly-placed ones. Always safe to
    re-run (matches manifest.py's own no-gate pattern): a file whose tags
    already match just gets rewritten with the same values."""
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {"embedded": 0, "failed": 0, "skipped_missing": 0}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0"
            )
        ]

    logger.info(f"[EMBED] re-embedding tags for {len(rows)} current survivor(s)...")
    for row in rows:
        path = Path(row["path"])
        try:
            relative = path.relative_to(library_root)
        except ValueError:
            relative = path

        if not path.exists():
            stats["skipped_missing"] += 1
            logger.info(f"[EMBED] skip (missing): {relative}")
            continue

        if dry_run:
            stats["embedded"] += 1
            logger.info(f"[EMBED] would re-embed: {relative}")
            continue

        embedded = write_tags(
            path, file_type=row["type"], energy=row["energy"], bpm=row["bpm"], year=row["year"],
            genre=row["genre"], album=row["album"],
        )
        stats["embedded" if embedded else "failed"] += 1
        logger.info(f"[EMBED] {relative} -> embedded={embedded}")

    return stats
