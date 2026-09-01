"""Stage 6: embed Type/Energy/BPM/Year/Genre into each survivor's own tags
(see embed.py -- portable to any tool, not just this pipeline/RadioStation),
then move every survivor to /Music/<Artist>/<Artist> - <Title>.ext --
classify.py only ever assigns Type=Song now (OST/Ambient/Classics content
has real artists and goes through the same treatment), so every row
reaching this stage is Song content. Corrupt/too-short files (flagged at
scan time), sub-MIN_BITRATE files (flagged at scan time), and intro/outro/
filler tracks (flagged by the intro_outro stage) are all relocated untouched
to their own <excluded_root> subfolder instead -- no tags, no Artist folder
(see _relocate_wholesale, the shared shape all three use).

"Does the artist already have a folder?" -- yes: file moves in; no: the
folder is created first, then the file moves in.
"""

import json
import os
import re
import shutil
from datetime import datetime, timezone
from pathlib import Path

from . import embed

UNSAFE_CHARS = re.compile(r'[<>:"/\\|?*\x00-\x1f]')


def _sanitize(name: str) -> str:
    name = UNSAFE_CHARS.sub("_", name).strip(" .")
    return name or "Unknown"


def _relocate_wholesale(conn, library_root: Path, excluded_root: Path, dedup_status: str, folder: str, label: str, dry_run: bool, logger) -> int:
    """Shared shape for corrupt/low_quality/intro_outro: move every row with
    this dedup_status, untouched, to excluded_root/folder/<relative path>.
    Same crash-resume pattern as every other mover in this pipeline (a
    missing source with an existing destination means a prior run moved it
    but crashed before the commit)."""
    rows = [
        dict(r) for r in conn.execute(
            "SELECT * FROM files WHERE dedup_status=? AND restructure_done=0", (dedup_status,)
        )
    ]
    for row in rows:
        src = Path(row["path"])
        relative = src.relative_to(library_root)
        dest = excluded_root / folder / relative
        logger.info(f"[RESTRUCTURE] {label}: {relative} -> {dest}")
        if dry_run:
            continue

        if not src.exists() and dest.exists():
            logger.info(f"[RESTRUCTURE] {label} file already moved (resuming after interruption): {relative}")
        else:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(src), str(dest))

        conn.execute(
            "UPDATE files SET path=?, restructure_done=1, final_relative_path=? WHERE path=?",
            (str(dest), str(relative), row["path"]),
        )
        conn.commit()
    return len(rows)


def _prune_empty_dirs(library_root: Path, logger) -> int:
    """Removes now-empty leftover source folders (Easy/, OST/, etc.) so
    /Music ends up strictly Artist folders -- a clean copy target for
    AzuraCast. Bottom-up so a folder emptied by removing its last empty
    child also gets removed in the same pass."""
    removed = 0
    for dirpath, dirnames, filenames in os.walk(library_root, topdown=False):
        dir_path = Path(dirpath)
        if dir_path == library_root:
            continue
        if not dirnames and not filenames:
            dir_path.rmdir()
            removed += 1
            logger.info(f"[RESTRUCTURE] removed empty folder: {dir_path.relative_to(library_root)}")
    return removed


def restructure(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {
        "songs_moved": 0, "corrupt_relocated": 0, "low_quality_relocated": 0,
        "intro_outro_relocated": 0, "needs_review_relocated": 0,
        "tags_embedded": 0, "tags_embed_failed": 0,
    }

    with checkpoint.connect(excluded_root) as conn:
        stats["corrupt_relocated"] = _relocate_wholesale(
            conn, library_root, excluded_root, "corrupt", checkpoint.CORRUPT_FOLDER, "corrupt", dry_run, logger
        )
        stats["low_quality_relocated"] = _relocate_wholesale(
            conn, library_root, excluded_root, "low_quality", checkpoint.LOW_QUALITY_FOLDER, "low quality", dry_run, logger
        )
        stats["intro_outro_relocated"] = _relocate_wholesale(
            conn, library_root, excluded_root, "intro_outro", checkpoint.INTRO_OUTRO_FOLDER, "intro/outro", dry_run, logger
        )

        # Low-confidence filename-derived artist/title guesses (see
        # artist_normalize.is_garbled_name, set during retag) never become
        # an Artist folder -- quarantined here instead, same relocation
        # shape as the corrupt branch above, plus a human-readable report.
        review_rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE needs_review=1 AND restructure_done=0"
            )
        ]
        for row in review_rows:
            src = Path(row["path"])
            relative = src.relative_to(library_root)
            dest = excluded_root / checkpoint.NEEDS_REVIEW_FOLDER / relative
            stats["needs_review_relocated"] += 1
            logger.info(f"[RESTRUCTURE] needs review ({row['needs_review_reason']}): {relative} -> {dest}")
            if dry_run:
                continue

            if not src.exists() and dest.exists():
                logger.info(f"[RESTRUCTURE] needs-review file already moved (resuming after interruption): {relative}")
            else:
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.move(str(src), str(dest))

            with checkpoint.needs_review_report_path(excluded_root).open("a") as report_file:
                report_file.write(json.dumps({
                    "path": str(dest),
                    "reason": row["needs_review_reason"],
                    "candidate_artist": row["artist"],
                    "candidate_title": row["title"],
                    "duplicate_of": row["duplicate_of"],
                    "timestamp": datetime.now(timezone.utc).isoformat(),
                }) + "\n")

            conn.execute(
                "UPDATE files SET path=?, restructure_done=1, final_relative_path=? WHERE path=?",
                (str(dest), str(relative), row["path"]),
            )
            conn.commit()

        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE dedup_status IN ('unique','keeper') AND needs_review=0 AND restructure_done=0"
            )
        ]

        for row in rows:
            src = Path(row["path"])
            artist = _sanitize(row["artist"] or "Unknown Artist")
            title = _sanitize(row["title"] or src.stem)
            dest_dir = library_root / artist
            filename_stem = f"{artist} - {title}"
            first_choice_dest = dest_dir / f"{filename_stem}{src.suffix}"

            stats["songs_moved"] += 1
            if dry_run:
                logger.info(f"[RESTRUCTURE] {src.relative_to(library_root)} -> {first_choice_dest.relative_to(library_root)}")
                continue

            if not src.exists() and first_choice_dest.exists():
                # Prior run embedded tags and moved the file but crashed
                # before the commit -- recognize it at its expected
                # destination instead of crashing on a missing source or
                # (worse) treating the already-moved file as a fresh
                # "collision" and renaming it again.
                dest = first_choice_dest
                logger.info(f"[RESTRUCTURE] already moved (resuming after interruption): {src.name} -> {dest.relative_to(library_root)}")
            else:
                embedded = embed.write_tags(
                    src,
                    file_type=row["type"],
                    energy=row["energy"],
                    bpm=row["bpm"],
                    year=row["year"],
                    genre=row["genre"],
                    album=row["album"],
                )
                stats["tags_embedded" if embedded else "tags_embed_failed"] += 1

                dest_dir.mkdir(parents=True, exist_ok=True)
                dest = first_choice_dest
                counter = 1
                while dest.exists() and dest.resolve() != src.resolve():
                    dest = dest_dir / f"{filename_stem}__{counter}{src.suffix}"
                    counter += 1

                shutil.move(str(src), str(dest))
                logger.info(f"[RESTRUCTURE] {src.name} -> {dest.relative_to(library_root)} (tags embedded: {embedded})")

            final_relative = dest.relative_to(library_root)
            conn.execute(
                "UPDATE files SET path=?, restructure_done=1, final_relative_path=? WHERE path=?",
                (str(dest), str(final_relative), row["path"]),
            )
            conn.commit()

    if not dry_run:
        stats["empty_folders_removed"] = _prune_empty_dirs(library_root, logger)

    return stats
