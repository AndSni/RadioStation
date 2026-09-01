"""Stage: post-retag cross-release duplicate detection and holding-folder
move -- collapses the same song appearing under different releases
(different remaster/radio-edit/album, different length/encode) that
dedup.py's audio-fingerprint comparison (pre-retag, duration-bucketed) has
no way to catch, since two releases of the same song routinely differ in
duration/fingerprint by more than dedup.py's tolerance.

Confirmed real problem this fixes: many copies of the same song survived
the original fingerprint-only dedup pass because they came from different
albums/releases with different lengths -- dedup.py never even compared
them. This stage runs AFTER retag (needs clean artist/title to group on)
and groups purely by (artist, title) -- duration plays NO role here,
per explicit confirmation: "always collapse to one, ignore duration".

Reuses dedup.py's keeper-selection rule (_keeper_sort_key: lossless >
highest bitrate > earliest mtime) and its move/undo-log/crash-resume
pattern rather than reimplementing them.

Also cross-checks new (title_dedup_done=0) rows against already-settled
(title_dedup_done=1) survivors sharing the same (artist, title) key, not
just against each other -- otherwise a file added in a later batch that
duplicates something already in the library would never be compared
against it, since this stage's own gate excludes already-done rows from
grouping by construction.
"""

import json
import shutil
from collections import defaultdict
from datetime import datetime, timezone
from pathlib import Path

from . import checkpoint, text_normalize
from .dedup import _keeper_sort_key


def _group_key(row: dict) -> tuple[str, str]:
    return (text_normalize.exact_key(row["artist"] or ""), text_normalize.base_title_key(row["title"] or ""))


def title_dedup(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    from . import log as log_module

    logger = log_module.get()
    stats = {"considered": 0, "groups": 0, "keepers": 0, "losers": 0, "moved": 0}
    report = {"groups": []}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') "
                "AND needs_review=0 AND retag_done=1 AND title_dedup_done=0"
            )
        ]
        stats["considered"] = len(rows)
        logger.info(f"[TITLE-DEDUP] grouping {len(rows)} new retagged file(s) by (artist, title)...")

        groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
        for row in rows:
            groups[_group_key(row)].append(row)

        # Cross-check against already-settled survivors sharing a group key
        # with at least one new row -- otherwise a newly-added file that
        # duplicates something already in the library would never be
        # compared against it: this stage's own gate (title_dedup_done=0)
        # excludes already-done rows from grouping entirely, so new-vs-
        # existing pairs were silently never checked. Confirmed real gap
        # when planning how to fold 20 new /Transfer folders into /Music.
        candidate_keys = {key for key, members in groups.items() if members}
        if candidate_keys:
            # Only the columns _group_key/_keeper_sort_key actually read --
            # not SELECT * -- since by this stage these rows' fingerprint
            # text is already populated and this query can span the whole
            # already-settled library at a large library's scale.
            existing_rows = conn.execute(
                "SELECT path, artist, title, is_lossless, bitrate, mtime FROM files "
                "WHERE type='Song' AND dedup_status IN ('unique','keeper') "
                "AND needs_review=0 AND retag_done=1 AND title_dedup_done=1"
            )
            for row in existing_rows:
                row = dict(row)
                key = _group_key(row)
                if key in candidate_keys:
                    groups[key].append(row)

        for key, members in groups.items():
            artist_key, title_key = key
            if not artist_key or not title_key:
                # Nothing to group on yet -- leave considered, no grouping,
                # so this stage's own completion gate isn't perpetually
                # blocked by rows with missing artist/title.
                if not dry_run:
                    for row in members:
                        conn.execute("UPDATE files SET title_dedup_done=1 WHERE path=?", (row["path"],))
                continue

            if len(members) == 1:
                if not dry_run:
                    conn.execute("UPDATE files SET title_dedup_done=1 WHERE path=?", (members[0]["path"],))
                continue

            stats["groups"] += 1
            ranked = sorted(members, key=lambda r: _keeper_sort_key(r, library_root))
            keeper, losers = ranked[0], ranked[1:]
            stats["keepers"] += 1
            stats["losers"] += len(losers)

            group_report = {"artist": artist_key, "title": title_key, "keeper": keeper["path"], "losers": []}
            logger.info(f"[TITLE-DEDUP] group ({artist_key!r}, {title_key!r}): keeper={Path(keeper['path']).relative_to(library_root)}")

            if dry_run:
                for loser in losers:
                    group_report["losers"].append({"path": loser["path"]})
                    logger.info(f"[TITLE-DEDUP]   loser: {Path(loser['path']).relative_to(library_root)} -> would move to Excluded Audio/Duplicates/")
                report["groups"].append(group_report)
                continue

            conn.execute(
                "UPDATE files SET dedup_status='keeper', title_dedup_done=1 WHERE path=?", (keeper["path"],)
            )

            for loser in losers:
                group_report["losers"].append({"path": loser["path"]})
                loser_path = Path(loser["path"])
                loser_relative = loser_path.relative_to(library_root)
                top_level = loser_relative.parts[0]
                dest_dir = excluded_root / checkpoint.DUPLICATES_FOLDER / top_level
                dest_dir.mkdir(parents=True, exist_ok=True)
                first_choice_dest = dest_dir / loser_path.name

                if not loser_path.exists():
                    # Same crash-recovery shape as dedup.py: a prior run's
                    # move completed but crashed before the commit that
                    # would have marked this loser done.
                    if first_choice_dest.exists():
                        dest = first_choice_dest
                        logger.info(f"[TITLE-DEDUP]   loser already moved (resuming after interruption): {loser_relative} -> {dest}")
                    else:
                        logger.info(f"[TITLE-DEDUP]   loser missing at both source and destination, skipping: {loser_relative}")
                        conn.execute("UPDATE files SET title_dedup_done=1 WHERE path=?", (loser["path"],))
                        conn.commit()
                        continue
                else:
                    dest = first_choice_dest
                    counter = 1
                    while dest.exists():
                        dest = dest_dir / f"{loser_path.stem}__dup{counter}{loser_path.suffix}"
                        counter += 1
                    shutil.move(str(loser_path), str(dest))
                    stats["moved"] += 1
                    logger.info(f"[TITLE-DEDUP]   loser: {loser_relative} -> {dest}")

                with checkpoint.undo_log_path(excluded_root).open("a") as undo_log_file:
                    undo_log_file.write(json.dumps({
                        "action": "title_dedup_move",
                        "from": str(loser_path),
                        "to": str(dest),
                        "keeper": keeper["path"],
                        "artist": artist_key,
                        "title": title_key,
                        "timestamp": datetime.now(timezone.utc).isoformat(),
                    }) + "\n")

                # restructure_done/final_relative_path reset for the same
                # reason dedup.py's loser-demotion does: this loser may
                # already have been restructured into /Music in an earlier
                # pipeline pass (title_dedup runs after restructure in a
                # later re-run whenever newly-added music turns an existing
                # keeper into a cross-release duplicate).
                conn.execute(
                    "UPDATE files SET dedup_status='title_loser', path=?, title_dedup_done=1, "
                    "restructure_done=0, final_relative_path=NULL WHERE path=?",
                    (str(dest), loser["path"]),
                )
                conn.execute(
                    "UPDATE files SET duplicate_of = COALESCE(duplicate_of || ';', '') || ? WHERE path=?",
                    (str(loser_relative), keeper["path"]),
                )
                conn.commit()

            report["groups"].append(group_report)

    if dry_run:
        checkpoint.title_dedup_report_path(excluded_root).write_text(json.dumps(report, indent=2))

    return stats
