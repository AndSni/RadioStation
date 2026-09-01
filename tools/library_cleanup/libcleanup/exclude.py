"""Stage 0: wholesale-relocate exception folders out of /Music into the
parallel /Excluded Audio tree, untouched -- no scanning, deduping, or
tagging, per user instruction. Must run before `scan`, so scan never even
sees these folders.

/Music ends up strictly Artist/Song after the full pipeline runs, suitable
for a direct copy into AzuraCast's media library.
"""

import shutil
from pathlib import Path

WHOLESALE_EXCLUDED_FOLDERS = ["RAW", "GTA_Radio", "Jingles", "Sleep", "Audio Books", "Playlists"]


def relocate_exceptions(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    from . import log

    logger = log.get()
    stats = {"folders_moved": 0, "missing": 0}

    for name in WHOLESALE_EXCLUDED_FOLDERS:
        src = library_root / name
        if not src.exists():
            stats["missing"] += 1
            continue

        dest = excluded_root / name
        file_count = sum(1 for _ in src.rglob("*") if _.is_file())
        logger.info(f"[EXCLUDE] {name}/ ({file_count} files) -> {dest}")

        if dry_run:
            stats["folders_moved"] += 1
            continue

        excluded_root.mkdir(parents=True, exist_ok=True)
        if dest.exists():
            # Destination already has a prior partial relocation (e.g. a
            # re-run after interruption) -- merge rather than clobber.
            for item in src.iterdir():
                shutil.move(str(item), str(dest / item.name))
            src.rmdir()
        else:
            shutil.move(str(src), str(dest))
        stats["folders_moved"] += 1

    return stats
