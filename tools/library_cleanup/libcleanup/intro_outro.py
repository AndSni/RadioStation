"""Stage: post-retag intro/outro/filler-track detection.

Runs after retag (needs clean title/artist) and before title_dedup (so
filler tracks never enter the (artist, title) grouping there). A confident
name-hint match (see filler.py) moves straight to dedup_status='intro_outro'
for restructure.py to relocate, same shape as corrupt/low_quality. A short
track with no name hint is ambiguous -- real short songs exist -- so it goes
through the existing needs_review path instead of being guessed at either
way.
"""

import json
from pathlib import Path

from . import checkpoint, filler


def intro_outro(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    from . import log as log_module

    logger = log_module.get()
    stats = {"considered": 0, "filler": 0, "needs_review": 0, "kept": 0}
    report = {"filler": [], "needs_review": []}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT * FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') "
                "AND needs_review=0 AND retag_done=1 AND intro_outro_done=0"
            )
        ]
        stats["considered"] = len(rows)
        logger.info(f"[INTRO-OUTRO] checking {len(rows)} retagged file(s) for filler tracks...")

        for row in rows:
            path = Path(row["path"])
            relative = path.relative_to(library_root)

            if filler.is_filler_name(row["title"], row["artist"], path.stem):
                stats["filler"] += 1
                report["filler"].append({"path": row["path"], "title": row["title"], "artist": row["artist"]})
                suffix = " -> would move to Excluded Audio/IntroOutro/" if dry_run else ""
                logger.info(f"[INTRO-OUTRO] filler: {relative}{suffix}")
                if not dry_run:
                    # restructure_done/final_relative_path reset for the same
                    # reason dedup.py/title_dedup.py reset it on demotion:
                    # this row can already have been restructured into
                    # /Music in an earlier pass, before intro/outro
                    # detection existed or before this file's tags/mtime
                    # changed enough to get rescanned -- without the reset,
                    # restructure.py's relocation query (restructure_done=0)
                    # would never see it again.
                    conn.execute(
                        "UPDATE files SET dedup_status='intro_outro', intro_outro_done=1, "
                        "restructure_done=0, final_relative_path=NULL WHERE path=?",
                        (row["path"],),
                    )
                continue

            duration = row["duration"] or 0.0
            if duration < filler.SHORT_TRACK_SECONDS:
                stats["needs_review"] += 1
                report["needs_review"].append({"path": row["path"], "duration": duration})
                logger.info(f"[INTRO-OUTRO] short, no name hint ({duration:.1f}s): {relative} -> flagged for review")
                if not dry_run:
                    # Same restructure_done reset as the filler branch above.
                    conn.execute(
                        "UPDATE files SET needs_review=1, needs_review_reason='short_unlabeled_track', "
                        "intro_outro_done=1, restructure_done=0, final_relative_path=NULL WHERE path=?",
                        (row["path"],),
                    )
                continue

            stats["kept"] += 1
            if not dry_run:
                conn.execute("UPDATE files SET intro_outro_done=1 WHERE path=?", (row["path"],))

    if dry_run:
        checkpoint.intro_outro_report_path(excluded_root).write_text(json.dumps(report, indent=2))

    return stats
