"""In-place filename normalization: rename each survivor to
"<Artist> - <Title>.ext" within its current directory and embed its tags,
WITHOUT moving it into an <Artist>/ folder or pruning anything (the two
extra things restructure.py does).

For a library whose folder layout is deliberately hand-curated -- e.g. a
portable-player copy sorted <Genre>/<Artist>/ -- and must survive the
cleanup pipeline untouched. Run this instead of `restructure`, right after
`retag` (skipping lastfm-correct / intro-outro / title-dedup / energy /
artist-folder-merge -- none of which this mode needs). A later `embed` run
is still a safe no-op re-write, exactly as on the normal path.

Reuses restructure_done as the "placement finalized" flag so `embed` and
the TUI's progress tracking need no new column, and reuses restructure.py's
_sanitize so both movers name files identically.

Crash-resume shape matches restructure.py: a missing source whose expected
new name already exists means a prior run renamed it but crashed before the
commit.
"""

import shutil
from pathlib import Path

from . import embed
from .restructure import _sanitize

# Survivors retag has finished and this stage hasn't. retag_done=1 already
# implies a Song survivor (retag only ever touches those), so no explicit
# type/dedup_status-only gate is needed beyond mirroring restructure.py's
# needs_review exclusion.
_PENDING_WHERE = (
    "dedup_status IN ('unique','keeper') AND needs_review=0 "
    "AND retag_done=1 AND restructure_done=0"
)

# NAME_MAX is 255 bytes on ext4/xfs/btrfs. Stay well under it so a "__N"
# collision suffix, the extension, and multibyte UTF-8 all still fit.
# retag/beets can hand back a 250-char ';'-joined performer-credit string
# as the artist (real case: Darius Rucker - "Wagon Wheel", 20+ session
# musicians) -- without this the rename dies with OSError [Errno 36].
_MAX_STEM_BYTES = 200


def _rename_stem(artist: str, title: str, suffix: str) -> str:
    """"<artist> - <title>", trimmed to fit the filesystem name limit.
    Trims the ARTIST (drops trailing credits), keeping the whole title and
    extension; only if the title alone still busts the budget does it trim
    the combined string from the end."""
    sep = " - "
    budget = _MAX_STEM_BYTES - len(suffix.encode("utf-8"))
    tail = (sep + title).encode("utf-8")
    art_budget = budget - len(tail)
    if art_budget >= 20:
        a = artist
        while len(a.encode("utf-8")) > art_budget and a:
            a = a[:-1]
        a = a.rstrip(" ;,.-") or "Unknown"
        return f"{a}{sep}{title}"
    whole = f"{artist}{sep}{title}"
    while len(whole.encode("utf-8")) > budget and whole:
        whole = whole[:-1]
    return whole.rstrip(" ;,.-") or "Untitled"


def _record(conn, old_path: str, dest: Path, library_root: Path) -> None:
    conn.execute(
        "UPDATE files SET path=?, restructure_done=1, final_relative_path=? WHERE path=?",
        (str(dest), str(dest.relative_to(library_root)), old_path),
    )
    conn.commit()


def _embed(row: dict, path: Path, stats: dict) -> None:
    embedded = embed.write_tags(
        path,
        file_type=row["type"],
        energy=row["energy"],
        bpm=row["bpm"],
        year=row["year"],
        genre=row["genre"],
        album=row["album"],
    )
    stats["tags_embedded" if embedded else "tags_embed_failed"] += 1


def rename_in_place(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {
        "renamed": 0, "already_named": 0, "skipped_no_artist": 0,
        "tags_embedded": 0, "tags_embed_failed": 0,
    }

    with checkpoint.connect(excluded_root) as conn:
        rows = [dict(r) for r in conn.execute(f"SELECT * FROM files WHERE {_PENDING_WHERE}")]

        for row in rows:
            src = Path(row["path"])
            artist = _sanitize(row["artist"]) if row["artist"] else ""
            title = _sanitize(row["title"] or src.stem)

            # No usable artist tag (retag couldn't identify it): leave the
            # filename exactly as the user filed it -- prefixing
            # "Unknown - " inside a hand-sorted folder is worse than doing
            # nothing -- but still embed whatever tags retag did find and
            # mark the row done so it stops re-appearing.
            if not artist:
                stats["skipped_no_artist"] += 1
                logger.info(f"[RENAME] no artist tag, left as-is: {src.relative_to(library_root)}")
                if not dry_run:
                    _embed(row, src, stats)
                    _record(conn, row["path"], src, library_root)
                continue

            stem = _rename_stem(artist, title, src.suffix)
            dest = src.with_name(f"{stem}{src.suffix}")

            if dry_run:
                if dest == src:
                    stats["already_named"] += 1
                else:
                    stats["renamed"] += 1
                    logger.info(f"[RENAME] {src.relative_to(library_root)} -> {dest.name}")
                continue

            # Crash-resume: a prior run already renamed + embedded this
            # file but died before the commit.
            if not src.exists() and dest.exists():
                logger.info(f"[RENAME] already renamed (resuming after interruption): {dest.name}")
                _record(conn, row["path"], dest, library_root)
                continue

            _embed(row, src, stats)

            counter = 1
            while dest.exists() and dest.resolve() != src.resolve():
                dest = src.with_name(f"{stem}__{counter}{src.suffix}")
                counter += 1

            if dest != src:
                shutil.move(str(src), str(dest))
                stats["renamed"] += 1
                logger.info(f"[RENAME] {src.relative_to(library_root)} -> {dest.name}")
            else:
                stats["already_named"] += 1

            _record(conn, row["path"], dest, library_root)

    return stats
