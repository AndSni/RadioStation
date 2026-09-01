"""Stage 5: energy + BPM scoring for Type=Song survivors.

Two passes for `energy`, so the score is relative to this specific library
rather than an absolute scale:
  1. decode + extract raw features per file (RMS loudness, tempo, spectral
     centroid/rolloff), cached in `energy_raw` -- the expensive part. `bpm`
     is written straight from this pass -- it's an absolute value (unlike
     the composite energy score), so it doesn't need cross-library
     normalization and is available as soon as extraction finishes. A file
     that fails to decode gets `energy_raw='{}'` and `energy=-1` (the "not
     analyzed" sentinel, matching Part B's TrackRecord.energy convention)
     immediately -- NOT left NULL, which would otherwise permanently stall
     restructure.py's "is energy resolved yet?" guard on any file librosa
     can't handle.
  2. min-max normalize each feature across EVERY successfully-analyzed file
     in the library (not just ones extracted in this particular run) and
     rewrite every row's combined [0,1] `energy` score from that shared
     range -- every single run, not only the first time a row is scored.
     This is what makes scores comparable across incremental runs (a file
     scored today and a file scored six months ago sit on the same scale)
     and is what makes "re-combining after a weight tweak only needs this
     pass, not a re-decode" actually true: a normalize-only rerun revisits
     every row's cached `energy_raw`, not just newly-decoded ones. Cheap at
     even a large library's scale -- `energy_raw` is three small floats per
     row, so parsing+recombining the whole library is a light in-memory
     pass; commits are batched (not one per row) so the DB round-trip cost
     stays low too.

`bpm` is tempo alone; `energy` is a broader composite that tempo only
partially drives (see FEATURE_WEIGHTS) -- a loud, dense slow track can
outscore a quiet fast one on energy despite the lower BPM. Exposing both
lets a smart playlist filter on either independently.

Confirmed on a real library file: ~1.7s decode+analyze per ~70s track
single-threaded -- the single most CPU-expensive stage across ~10k+ songs,
hence --workers.

BPM specifically uses the real Queen Mary tempo tracker (qm-vamp-plugins,
the same DSP algorithm Mixxx itself runs, via the `vamp` Python host
binding) rather than librosa's own tempo estimators -- confirmed against
real Mixxx-verified BPM values to be both fast (~2-3s/file, not the
20x-slower cost a neural-net tempo tracker like madmom would add) and
accurate (matched Mixxx within ~1 BPM on both tested tracks, including a
rebetiko/zeibekiko track no librosa-only approach got right). Falls back to
librosa's beat_track (a smaller, second-best confirmed improvement over the
plain autocorrelation estimator) when the system qm-vamp-plugins/kiss-fft
packages aren't installed, so the pipeline still works, just less
accurately, on a machine without them.
"""

import ctypes
import ctypes.util
import glob
import json
import os
from collections import Counter
from concurrent.futures import ProcessPoolExecutor, as_completed
from pathlib import Path

import librosa
import numpy as np

SAMPLE_RATE = 44100  # matches what the Vamp/QM tempo tracker expects; also used for rms/centroid below
FAILED_SENTINEL = "{}"

# Named so the combination formula is easy to retune without re-decoding
# audio -- only stage-2 normalization needs to re-run after a weight change.
FEATURE_WEIGHTS = {"rms": 0.5, "tempo": 0.25, "centroid": 0.25}

# A raw estimate outside this range is virtually always a gross tracking
# failure (silence, noise, a corrupt decode) rather than a genuinely
# sub-70 or 300+ BPM track -- folding it back in is a cheap safety net for
# the librosa fallback path specifically. Doesn't apply to the QM tracker,
# which doesn't need it.
PLAUSIBLE_BPM_RANGE = (70.0, 180.0)


def _fold_to_plausible_range(bpm: float) -> float:
    if bpm <= 0:
        return bpm
    lo, hi = PLAUSIBLE_BPM_RANGE
    while bpm < lo:
        bpm *= 2
    while bpm > hi:
        bpm /= 2
    return bpm


def _load_qm_tempo_tracker():
    """One-time setup for the real Queen Mary tempo tracker: locate and
    dlopen kiss-fft with RTLD_GLOBAL (qm-vamp-plugins.so references its
    kiss_fft symbol without declaring a shared-library dependency on it --
    a packaging gap on at least Fedora -- so the symbol has to already be
    loaded into the process before the plugin does), point VAMP_PATH at
    wherever the system installed the Vamp plugin .so files, then import
    the `vamp` host binding. Returns the vamp module, or None if any part
    of this isn't available (e.g. qm-vamp-plugins/kiss-fft not installed) --
    callers fall back to librosa's own tempo estimator in that case."""
    kissfft_lib = ctypes.util.find_library("kissfft-float")
    if not kissfft_lib:
        candidates = glob.glob("/usr/lib64/libkissfft-float.so*") + glob.glob("/usr/lib/libkissfft-float.so*")
        kissfft_lib = candidates[0] if candidates else None
    if not kissfft_lib:
        return None
    try:
        ctypes.CDLL(kissfft_lib, mode=ctypes.RTLD_GLOBAL)
    except OSError:
        return None

    if "VAMP_PATH" not in os.environ:
        for candidate in ("/usr/lib64/vamp", "/usr/lib/vamp", "/usr/local/lib/vamp"):
            if os.path.isdir(candidate):
                os.environ["VAMP_PATH"] = candidate
                break

    try:
        import vamp
    except ImportError:
        return None
    if "qm-vamp-plugins:qm-tempotracker" not in vamp.list_plugins():
        return None
    return vamp


_qm_vamp = _load_qm_tempo_tracker()


def _tempo_from_qm(y: np.ndarray, sr: int) -> float | None:
    try:
        results = _qm_vamp.collect(y, sr, "qm-vamp-plugins:qm-tempotracker")
    except Exception:  # noqa: BLE001 - fall back to librosa for this one file
        return None
    labels = [f["label"] for f in results.get("list", []) if f.get("label")]
    if not labels:
        return None
    bpms = [float(label.split()[0]) for label in labels]
    # Mode, not mean/median -- the tracker's frame-to-frame estimate jitters
    # in a tight cluster around the true tempo with occasional outlier
    # frames; the single most common reading is the most robust summary.
    return Counter(round(b, 2) for b in bpms).most_common(1)[0][0]


def _extract_features(path: str) -> dict | None:
    try:
        y, sr = librosa.load(path, sr=SAMPLE_RATE, mono=True)
        if y.size == 0:
            return None
        rms = float(np.mean(librosa.feature.rms(y=y)))
        tempo = _tempo_from_qm(y, sr) if _qm_vamp is not None else None
        if tempo is None:
            tempo, _beat_frames = librosa.beat.beat_track(y=y, sr=sr)
            tempo = _fold_to_plausible_range(float(np.asarray(tempo).item()))
        centroid = float(np.mean(librosa.feature.spectral_centroid(y=y, sr=sr)))
        return {"rms": rms, "tempo": tempo, "centroid": centroid}
    except Exception:  # noqa: BLE001 - one bad file shouldn't stop the batch
        return None


def _normalize(values: dict[str, list[float]]) -> dict[str, tuple[float, float]]:
    ranges = {}
    for key, series in values.items():
        lo, hi = min(series), max(series)
        ranges[key] = (lo, hi if hi > lo else lo + 1e-9)
    return ranges


def _renormalize_energy(conn, dry_run: bool, logger) -> int:
    """Pass 2, shared by energy() and analyze_paths(): normalize across
    EVERY file that has raw features, every call -- not just rows scored in
    this particular run -- so the range (and therefore every row's score)
    reflects the whole library and stays comparable across incremental
    runs. Excludes the FAILED_SENTINEL rows, which already have energy=-1."""
    to_normalize = [
        dict(r) for r in conn.execute(
            "SELECT path, energy_raw FROM files WHERE energy_raw IS NOT NULL AND energy_raw != ?",
            (FAILED_SENTINEL,),
        )
    ]
    if not to_normalize:
        return 0

    parsed = {row["path"]: json.loads(row["energy_raw"]) for row in to_normalize}
    ranges = _normalize({key: [f[key] for f in parsed.values()] for key in FEATURE_WEIGHTS})

    normalized = 0
    for i, (path, features) in enumerate(parsed.items(), start=1):
        score = 0.0
        for key, weight in FEATURE_WEIGHTS.items():
            lo, hi = ranges[key]
            score += weight * (features[key] - lo) / (hi - lo)
        score = max(0.0, min(1.0, score))
        normalized += 1
        if not dry_run:
            conn.execute("UPDATE files SET energy=? WHERE path=?", (score, path))
            if i % 500 == 0:
                conn.commit()

    if not dry_run:
        logger.info(f"[ENERGY] normalized {normalized} file(s) (range recomputed library-wide)")
    return normalized


def energy(library_root: Path, excluded_root: Path, workers: int = 4, dry_run: bool = False) -> dict:
    from . import checkpoint
    from . import log as log_module

    logger = log_module.get()
    stats = {"analyzed": 0, "failed": 0, "normalized": 0}

    with checkpoint.connect(excluded_root) as conn:
        pending = [
            dict(r) for r in conn.execute(
                "SELECT path FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') "
                "AND needs_review=0 AND retag_done=1 AND title_dedup_done=1 AND energy_raw IS NULL"
            )
        ]

        if pending and not dry_run:
            with ProcessPoolExecutor(max_workers=workers) as pool:
                futures = {pool.submit(_extract_features, row["path"]): row["path"] for row in pending}
                for future in as_completed(futures):
                    path = futures[future]
                    relative = Path(path).relative_to(library_root)
                    features = future.result()
                    if features is None:
                        stats["failed"] += 1
                        conn.execute(
                            "UPDATE files SET energy_raw=?, energy=-1 WHERE path=?", (FAILED_SENTINEL, path)
                        )
                        conn.commit()
                        logger.info(f"[ENERGY] {relative} -> decode failed, energy left unset")
                        continue
                    stats["analyzed"] += 1
                    conn.execute(
                        "UPDATE files SET energy_raw=?, bpm=? WHERE path=?",
                        (json.dumps(features), features["tempo"], path),
                    )
                    conn.commit()
                    logger.info(f"[ENERGY] {relative} -> bpm={features['tempo']:.1f}")
        elif dry_run:
            stats["analyzed"] = len(pending)

        stats["normalized"] = _renormalize_energy(conn, dry_run, logger)

    return stats


def wipe_bpm_energy(library_root: Path, excluded_root: Path, dry_run: bool = False) -> dict:
    """Clean-slate reset for a full-library recompute after a detector
    improvement (e.g. switching to the real Queen Mary tracker): strips
    BPM/Energy from every current survivor's file tags and clears their
    DB columns, including energy_raw -- which is `energy`'s own gate, so
    the very next `energy` run naturally recomputes everyone with the
    improved detector rather than skipping already-analyzed rows."""
    from . import checkpoint
    from . import embed as embed_mod
    from . import log as log_module

    logger = log_module.get()
    stats = {"stripped": 0, "unchanged": 0}

    with checkpoint.connect(excluded_root) as conn:
        rows = [
            dict(r) for r in conn.execute(
                "SELECT path FROM files WHERE type='Song' AND dedup_status IN ('unique','keeper') "
                "AND needs_review=0"
            )
        ]
        logger.info(f"[WIPE-BPM-ENERGY] resetting {len(rows)} current survivor(s)...")

        for row in rows:
            path = Path(row["path"])
            try:
                relative = path.relative_to(library_root)
            except ValueError:
                relative = path

            if dry_run:
                stats["stripped"] += 1
                logger.info(f"[WIPE-BPM-ENERGY] would strip: {relative}")
                continue

            if path.exists():
                stripped = embed_mod.strip_bpm_energy(path)
                stats["stripped" if stripped else "unchanged"] += 1
            else:
                stats["unchanged"] += 1

            conn.execute("UPDATE files SET bpm=NULL, energy=NULL, energy_raw=NULL WHERE path=?", (row["path"],))
            conn.commit()
            logger.info(f"[WIPE-BPM-ENERGY] {relative} reset")

    return stats


def analyze_paths(library_root: Path, excluded_root: Path, paths: list[str], dry_run: bool = False) -> dict:
    """On-demand re-analysis for one or a few specific files already tracked
    by the pipeline -- the seed of the `ourtool --run-bpm-scan filename.ext`
    capability RadioStation is meant to shell out to later, for redoing a
    single track without a full-library recompute. Ignores energy_raw's
    normal "already analyzed" gate (that's the whole point: redo THIS file
    now), recomputes, updates the DB, re-normalizes energy across the
    library same as a normal energy() run, then immediately re-embeds the
    file's tags -- one invocation both recomputes and pushes the result,
    no separate `embed` run needed."""
    from . import checkpoint
    from . import embed as embed_mod
    from . import log as log_module

    logger = log_module.get()
    stats = {"analyzed": 0, "failed": 0, "not_tracked": 0, "embedded": 0}

    with checkpoint.connect(excluded_root) as conn:
        for raw_path in paths:
            path = Path(raw_path).resolve()
            row = conn.execute("SELECT * FROM files WHERE path = ?", (str(path),)).fetchone()
            if row is None:
                stats["not_tracked"] += 1
                logger.info(f"[ANALYZE] not tracked by the pipeline, skipping: {path}")
                continue
            row = dict(row)

            if dry_run:
                stats["analyzed"] += 1
                logger.info(f"[ANALYZE] would re-analyze + re-embed: {path}")
                continue

            features = _extract_features(str(path))
            if features is None:
                stats["failed"] += 1
                logger.info(f"[ANALYZE] decode failed: {path}")
                continue

            stats["analyzed"] += 1
            conn.execute(
                "UPDATE files SET energy_raw=?, bpm=? WHERE path=?",
                (json.dumps(features), features["tempo"], str(path)),
            )
            conn.commit()
            logger.info(f"[ANALYZE] {path} -> bpm={features['tempo']:.1f}")

            _renormalize_energy(conn, dry_run=False, logger=logger)

            row = dict(conn.execute("SELECT * FROM files WHERE path = ?", (str(path),)).fetchone())
            embedded = embed_mod.write_tags(
                path, file_type=row["type"], energy=row["energy"], bpm=row["bpm"], year=row["year"],
                genre=row["genre"], album=row["album"],
            )
            stats["embedded"] += int(embedded)
            logger.info(f"[ANALYZE] {path} -> embedded={embedded}")

    return stats
