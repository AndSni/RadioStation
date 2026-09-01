"""Replays the dedup undo log in reverse, moving losers back to their
original location. Safe to re-run: a move whose destination no longer
exists (already undone) is skipped, not errored.

Also resets the restored file's checkpoint row so the next `scan`/`dedup`
run treats it as fresh (unclustered) rather than leaving it pointed at a
now-nonexistent path with a stale 'loser' status. A `title_dedup_move`
entry additionally resets `title_dedup_done` back to 0 -- otherwise the
restored file would be permanently invisible to title_dedup.py's own
re-grouping query (`WHERE title_dedup_done=0`), so the exact duplicate
call being undone could never be re-evaluated without a full pipeline
`reset`. A `dedup_move` entry needs no such extra reset: fingerprint-dedup
runs before retag/intro_outro/title_dedup/energy, so a file re-promoted to
unique/keeper can safely reuse its already-computed tags/energy from
before demotion.

The keeper's `duplicate_of` field (a semicolon-joined list of relative
paths) is repaired by removing only the one entry matching this undo, not
by blanking the whole field -- a keeper that absorbed several losers must
keep the record of the ones that are still merged when only one of them is
being restored.
"""

import json
import shutil
from pathlib import Path


def undo(log_path: Path, library_root: Path, excluded_root: Path) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {"restored": 0, "skipped": 0}
    entries = [json.loads(line) for line in log_path.read_text().splitlines() if line.strip()]

    with checkpoint.connect(excluded_root) as conn:
        for entry in reversed(entries):
            dest = Path(entry["to"])
            original = Path(entry["from"])
            if not dest.exists():
                stats["skipped"] += 1
                continue
            original.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(dest), str(original))
            stats["restored"] += 1
            logger.info(f"[UNDO] {dest} -> {original}")

            extra_reset = ", title_dedup_done=0" if entry.get("action") == "title_dedup_move" else ""
            conn.execute(
                f"UPDATE files SET path=?, dedup_status=NULL, dedup_cluster_id=NULL, dedup_score=NULL{extra_reset} "
                "WHERE path=?",
                (str(original), str(dest)),
            )

            keeper_row = conn.execute(
                "SELECT duplicate_of FROM files WHERE path=?", (entry["keeper"],)
            ).fetchone()
            if keeper_row is None:
                logger.warning(
                    f"[UNDO] keeper {entry['keeper']} not found (moved/renamed since this log entry "
                    "was written) -- its duplicate_of list couldn't be cleaned up automatically"
                )
            elif keeper_row["duplicate_of"]:
                target = str(original.relative_to(library_root))
                remaining = [e for e in keeper_row["duplicate_of"].split(";") if e and e != target]
                conn.execute(
                    "UPDATE files SET duplicate_of=? WHERE path=?",
                    (";".join(remaining) if remaining else None, entry["keeper"]),
                )

    return stats
