"""Stage: render manifest.csv from checkpoint state.

Always safe/idempotent -- just re-renders from current DB state, so it can
be re-run any time to get an up-to-date manifest as later stages complete
more files. Columns match the schema RadioStation's ImportDialog manifest
merge expects (see Part B, applyV7 plan). Written to <library_root>/manifest.csv
(inside /Music, deliberately) since it's the bridge file RadioStation's
import dialog looks for right next to the tree it describes -- everything
else pipeline-internal lives under <excluded_root>/_pipeline/.

Only dedup_status IN ('unique','keeper') rows are ever written -- everything
else (corrupt/low_quality/intro_outro/loser/title_loser/needs_review) has
been relocated out of /Music, so a manifest row for it would either point at
a path that no longer exists there or would re-list a file the dedup/quality
stages already decided /Music shouldn't have. A row surviving with
restructure_done=1 despite a later stage demoting it out of unique/keeper is
exactly the bug this filter guards against (see dedup.py/title_dedup.py,
which reset restructure_done back to 0 on demotion for the same reason).
"""

import csv
from pathlib import Path

MANIFEST_SCHEMA_VERSION = 3

FIELDS = ["relative_path", "type", "year", "energy", "bpm", "genre", "album", "artist", "title", "duplicate_of"]


def write_manifest(library_root: Path, excluded_root: Path) -> dict:
    from . import checkpoint

    stats = {"rows": 0}
    manifest_path = library_root / "manifest.csv"

    with checkpoint.connect(excluded_root) as conn:
        rows = conn.execute(
            "SELECT * FROM files WHERE restructure_done=1 AND dedup_status IN ('unique','keeper') AND needs_review=0 "
            "ORDER BY final_relative_path"
        ).fetchall()

    with manifest_path.open("w", newline="") as f:
        f.write(f"# manifest_schema_version={MANIFEST_SCHEMA_VERSION}\n")
        writer = csv.DictWriter(f, fieldnames=FIELDS)
        writer.writeheader()
        for row in rows:
            writer.writerow({
                "relative_path": row["final_relative_path"],
                "type": row["type"],
                "year": row["year"] or "",
                "energy": f"{row['energy']:.3f}" if row["energy"] is not None and row["energy"] >= 0 else "",
                "bpm": f"{row['bpm']:.1f}" if row["bpm"] is not None else "",
                "genre": row["genre"] or "",
                "album": row["album"] or "",
                "artist": row["artist"] or "",
                "title": row["title"] or "",
                "duplicate_of": row["duplicate_of"] or "",
            })
            stats["rows"] += 1

    return stats
