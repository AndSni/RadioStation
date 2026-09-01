"""Stage 2: fingerprint-based duplicate detection and holding-folder move.

Duration-bucketing (+-2s) is essential before any fingerprint comparison --
comparing fingerprints of wildly different lengths gives misleading high
scores (confirmed against this library's own RAW/ folder: a corrupt 1.8s
stub scored 0.67-1.0 against several unrelated full-length tracks purely
because there was almost nothing to compare). Only files within the same
duration bucket are ever compared to each other.

Duplicate losers move to <excluded_root>/Duplicates/, not anywhere inside
the library root -- /Music needs to stay clean for a direct AzuraCast copy.

Also cross-checks newly-scanned (dedup_status IS NULL) files against
already-settled (unique/keeper) survivors, not just against each other --
otherwise a file added in a later batch that duplicates something already
in the library would never be fingerprint-compared against it.
"""

import bisect
import json
import shutil
from datetime import datetime, timezone
from pathlib import Path

import chromaprint
import numpy as np

from . import checkpoint


def _decode_fingerprint(encoded: str) -> np.ndarray:
    """`fingerprint` is stored as the compact base64 string AcoustID's
    lookup API wants directly (see scan.py); local similarity comparison
    needs the actual raw 32-bit hash array, so decode it here."""
    raw_ints, _algorithm = chromaprint.decode_fingerprint(encoded.encode("ascii"))
    # int64 first, THEN mask+downcast to uint32 -- raw_ints can contain
    # negative Python ints (chromaprint's 32-bit hashes come back
    # signed-representation), and constructing a uint32 array directly
    # from those would overflow/raise. int64 safely holds any 32-bit
    # signed value; masking with 0xFFFFFFFF reinterprets the two's-
    # complement bit pattern as unsigned, after which every value is
    # guaranteed in [0, 2**32) and safe to downcast. The downcast (not
    # kept as int64) is the actual memory win -- at a large-library scale,
    # a full first run holds one of these arrays per file simultaneously
    # during clustering, so halving each one's footprint is a free saving.
    return (np.asarray(raw_ints, dtype=np.int64) & 0xFFFFFFFF).astype(np.uint32)

DURATION_TOLERANCE_SECONDS = 2.0
DEFAULT_THRESHOLD = 0.95

# Same constants pyacoustid's reference _match_fingerprints uses, but
# vectorized with numpy instead of a pure-Python triple-nested loop --
# confirmed 46x faster on real library fingerprints (0.0017 max score
# difference, irrelevant at this threshold). The pure-Python version does
# up to ~240 popcount() calls per fingerprint element via bin(x).count("1"),
# which does not scale to a 13k-file library: with many same-duration pairs
# (a normal music library has plenty), it was still running after 20+
# minutes of CPU time with no end in sight.
_MAX_ALIGN_OFFSET = 120
_MAX_BIT_ERROR = 2


def _match_fingerprints(a: np.ndarray, b: np.ndarray) -> float:
    asize, bsize = len(a), len(b)
    best = 0
    for delta in range(-_MAX_ALIGN_OFFSET + 1, _MAX_ALIGN_OFFSET):
        i_start = max(0, delta)
        i_end = min(asize, bsize + delta)
        if i_start >= i_end:
            continue
        xor = (a[i_start:i_end] ^ b[i_start - delta:i_end - delta]).astype(np.uint32)
        count = int(np.count_nonzero(np.bitwise_count(xor) <= _MAX_BIT_ERROR))
        if count > best:
            best = count
    return best / min(asize, bsize)


class UnionFind:
    def __init__(self, items):
        self.parent = {item: item for item in items}

    def find(self, item):
        while self.parent[item] != item:
            self.parent[item] = self.parent[self.parent[item]]
            item = self.parent[item]
        return item

    def union(self, a, b):
        ra, rb = self.find(a), self.find(b)
        if ra != rb:
            self.parent[ra] = rb


def _keeper_sort_key(row, library_root: Path):
    return (
        0 if row["is_lossless"] else 1,
        -(row["bitrate"] or 0),
        row["mtime"],
    )


def _cluster_candidates(rows, threshold, logger=None, new_paths=None):
    """Bucket rows by duration (+-tolerance) and union any pair above the
    similarity threshold. Returns {cluster_id: [row, ...]} for clusters with
    more than one member, and a per-pair score lookup for reporting.

    `new_paths`, when given, skips any pair where NEITHER side is a new
    (not-yet-classified) row -- old-vs-old pairs were already resolved in
    whatever earlier run first classified them, so re-comparing all of them
    on every incremental run is pure waste. Confirmed real cost: checking a
    single new file against a ~6,300-file already-processed library took
    ~8 minutes before this skip, almost entirely old-vs-old comparisons
    whose outcome could never change."""
    by_duration = sorted(rows, key=lambda r: r["duration"] or 0.0)
    fps = {
        r["path"]: _decode_fingerprint(r["fingerprint"])
        for r in by_duration if r["fingerprint"]
    }
    uf = UnionFind([r["path"] for r in by_duration])
    scores = {}

    n = len(by_duration)
    comparisons = 0
    for i in range(n):
        a = by_duration[i]
        if logger and i and i % 2000 == 0:
            logger.info(f"[DEDUP] clustering: {i}/{n} files compared, {comparisons} fingerprint pairs so far")
        if a["path"] not in fps:
            continue
        for j in range(i + 1, n):
            b = by_duration[j]
            if (b["duration"] or 0.0) - (a["duration"] or 0.0) > DURATION_TOLERANCE_SECONDS:
                break
            if b["path"] not in fps:
                continue
            if new_paths is not None and a["path"] not in new_paths and b["path"] not in new_paths:
                continue
            comparisons += 1
            score = _match_fingerprints(fps[a["path"]], fps[b["path"]])
            if score >= threshold:
                uf.union(a["path"], b["path"])
                scores[(a["path"], b["path"])] = score

    clusters: dict[str, list] = {}
    row_by_path = {r["path"]: r for r in by_duration}
    for path in row_by_path:
        root = uf.find(path)
        clusters.setdefault(root, []).append(row_by_path[path])

    return {cid: members for cid, members in clusters.items() if len(members) > 1}, scores


def dedup(library_root: Path, excluded_root: Path, threshold: float = DEFAULT_THRESHOLD, dry_run: bool = False) -> dict:
    from . import log as log_module

    logger = log_module.get()
    stats = {"unique": 0, "clusters": 0, "keepers": 0, "losers": 0, "moved": 0}
    report = {"clusters": [], "threshold": threshold}

    with checkpoint.connect(excluded_root) as conn:
        new_rows = [dict(r) for r in conn.execute("SELECT * FROM files WHERE dedup_status IS NULL")]
        logger.info(f"[DEDUP] clustering {len(new_rows)} new file(s) (duration-bucketed fingerprint comparison)...")

        # Cross-check against already-settled survivors too, so a newly-added
        # file that fingerprint-matches something already in the library
        # doesn't slip through just because this stage only ever compared
        # dedup_status IS NULL rows against each other. Confirmed real gap
        # when planning how to fold 20 new /Transfer folders into /Music.
        new_paths = {r["path"] for r in new_rows}
        existing_rows = []
        if new_rows:
            # Duration-prefilter before fingerprint-decoding a single
            # existing row: only rows within tolerance of some new row's
            # duration can ever be compared anyway (see _cluster_candidates'
            # own bucketing), so there's no reason to pull the rest of a
            # large existing library into the (much more expensive)
            # decode+compare step just to have new_paths skip them there.
            new_durations = sorted(r["duration"] or 0.0 for r in new_rows)
            # Only the columns _cluster_candidates/_keeper_sort_key actually
            # read -- not SELECT * -- since by this point in the pipeline
            # these rows' fingerprint text is already populated and this
            # query can span the whole already-settled library at a large
            # library's scale.
            all_existing = [
                dict(r) for r in conn.execute(
                    "SELECT path, duration, fingerprint, is_lossless, bitrate, mtime FROM files "
                    "WHERE dedup_status IN ('unique','keeper')"
                )
            ]
            for row in all_existing:
                d = row["duration"] or 0.0
                lo = bisect.bisect_left(new_durations, d - DURATION_TOLERANCE_SECONDS)
                hi = bisect.bisect_right(new_durations, d + DURATION_TOLERANCE_SECONDS)
                if lo < hi:
                    existing_rows.append(row)

        clusters, scores = _cluster_candidates(new_rows + existing_rows, threshold, logger=logger, new_paths=new_paths)
        # Discard any cluster made up entirely of already-settled rows --
        # shouldn't occur (they were already resolved against each other in
        # an earlier run) but costs nothing to guard against reprocessing.
        clusters = {cid: members for cid, members in clusters.items() if any(m["path"] in new_paths for m in members)}

        clustered_paths = {r["path"] for members in clusters.values() for r in members}
        for row in new_rows:
            if row["path"] not in clustered_paths:
                stats["unique"] += 1
                if not dry_run:
                    conn.execute("UPDATE files SET dedup_status='unique' WHERE path=?", (row["path"],))
                    # Periodic commit so a crash partway through a large
                    # batch doesn't lose all of it (same idiom scan.py
                    # already uses) -- these markings are idempotent to
                    # redo, but redoing means a full re-fingerprint-compare
                    # of everything, wasted CPU at library scale.
                    if stats["unique"] % 200 == 0:
                        conn.commit()

        for cluster_id, members in clusters.items():
            stats["clusters"] += 1
            ranked = sorted(members, key=lambda r: _keeper_sort_key(r, library_root))
            keeper, losers = ranked[0], ranked[1:]
            stats["keepers"] += 1
            stats["losers"] += len(losers)

            cluster_report = {"keeper": keeper["path"], "losers": []}
            logger.info(f"[DEDUP] cluster: keeper={Path(keeper['path']).relative_to(library_root)}")

            if not dry_run:
                conn.execute(
                    "UPDATE files SET dedup_status='keeper', dedup_cluster_id=? WHERE path=?",
                    (cluster_id, keeper["path"]),
                )

            for loser in losers:
                pair_score = scores.get((keeper["path"], loser["path"])) or scores.get((loser["path"], keeper["path"]))
                cluster_report["losers"].append({"path": loser["path"], "score": pair_score})
                loser_path = Path(loser["path"])
                loser_relative = loser_path.relative_to(library_root)

                if dry_run:
                    logger.info(f"[DEDUP]   loser (score={pair_score:.3f}): {loser_relative} -> would move to Excluded Audio/Duplicates/")
                    continue

                top_level = loser_relative.parts[0]
                dest_dir = excluded_root / checkpoint.DUPLICATES_FOLDER / top_level
                dest_dir.mkdir(parents=True, exist_ok=True)
                first_choice_dest = dest_dir / loser_path.name

                if not loser_path.exists():
                    # A prior run's move completed but crashed before the
                    # commit that would have marked this loser done --
                    # dedup_status was still NULL, so it got re-clustered.
                    # Recognize it at its expected destination instead of
                    # crashing on a FileNotFoundError or misfiling it as a
                    # fresh "collision".
                    if first_choice_dest.exists():
                        dest = first_choice_dest
                        logger.info(f"[DEDUP]   loser already moved (resuming after interruption): {loser_relative} -> {dest}")
                    else:
                        logger.info(f"[DEDUP]   loser missing at both source and destination, skipping: {loser_relative}")
                        continue
                else:
                    dest = first_choice_dest
                    counter = 1
                    while dest.exists():
                        dest = dest_dir / f"{loser_path.stem}__dup{counter}{loser_path.suffix}"
                        counter += 1
                    shutil.move(str(loser_path), str(dest))
                    stats["moved"] += 1
                    logger.info(f"[DEDUP]   loser (score={pair_score:.3f}): {loser_relative} -> {dest}")

                with checkpoint.undo_log_path(excluded_root).open("a") as undo_log_file:
                    undo_log_file.write(json.dumps({
                        "action": "dedup_move",
                        "from": str(loser_path),
                        "to": str(dest),
                        "keeper": keeper["path"],
                        "cluster_id": cluster_id,
                        "score": pair_score,
                        "timestamp": datetime.now(timezone.utc).isoformat(),
                    }) + "\n")

                # restructure_done/final_relative_path reset because a re-run
                # can demote a loser that was already restructured into
                # /Music in an earlier pass (e.g. newly-added music now
                # duplicates it) -- leaving those stale would let manifest.py
                # (or anything else trusting restructure_done) re-list a file
                # that no longer lives at that path.
                conn.execute(
                    "UPDATE files SET dedup_status='loser', dedup_cluster_id=?, dedup_score=?, path=?, "
                    "restructure_done=0, final_relative_path=NULL WHERE path=?",
                    (cluster_id, pair_score, str(dest), loser["path"]),
                )
                conn.execute(
                    "UPDATE files SET duplicate_of = COALESCE(duplicate_of || ';', '') || ? WHERE path=?",
                    (str(loser_relative), keeper["path"]),
                )
                conn.commit()

            report["clusters"].append(cluster_report)

    if dry_run:
        checkpoint.dry_run_report_path(excluded_root).write_text(json.dumps(report, indent=2))

    return stats
