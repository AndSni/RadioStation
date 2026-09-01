"""Stage 1: walk the library, cache probe data + fingerprint per file.

Safe to re-run: any file whose (size, mtime) matches what's already in the
checkpoint is skipped untouched. Must run after `exclude` (stage 0) has
relocated the wholesale-excluded folders, so this never even walks into
RAW/GTA_Radio/Jingles/Sleep/Audio Books/Playlists.

Files too short to be real audio (MIN_DURATION_SECONDS -- almost always a
broken/truncated download) are marked dedup_status='corrupt' immediately,
bypassing fingerprinting/dedup/retag/energy entirely -- they get relocated
to /Excluded Audio/Corrupt by restructure.py rather than silently stalling
later stages. Lossy files below MIN_BITRATE get the same treatment as
dedup_status='low_quality' -- an unconditional quality floor, not a dedup
outcome, so it applies even to a file with no duplicate to fall back on.
"""

from pathlib import Path

import acoustid

from . import checkpoint
from .audio_probe import AUDIO_EXTENSIONS, MIN_BITRATE, MIN_DURATION_SECONDS, probe_audio
from .classify import classify_type

EXCLUDED_NAMES = {"manifest.csv"}


def iter_audio_files(library_root: Path):
    for path in library_root.rglob("*"):
        if not path.is_file():
            continue
        if path.name in EXCLUDED_NAMES:
            continue
        if path.suffix.lower() not in AUDIO_EXTENSIONS:
            continue
        yield path


def fingerprint_file(path: Path) -> tuple[str | None, str | None]:
    """Returns the Chromaprint fingerprint as its compact base64-encoded
    string (exactly the form AcoustID's lookup API wants -- no re-encoding
    needed later), or None + an error message.

    BUG FIX: acoustid.fingerprint_file() returns this string as `bytes`
    (e.g. b'AQADtNK0SOGS...'), NOT a list of raw fingerprint integers.
    Earlier code did `[int(x) for x in fp]`, which for a bytes object
    iterates over individual ASCII byte values (0-255) -- silently
    "worked" (no exception) but produced meaningless data: confirmed by
    comparing against chromaprint.decode_fingerprint()'s real output,
    which is a ~948-int array of actual 32-bit fingerprint hashes, not
    ~3610 byte values that turned out to just be the ASCII codes for
    "AQAD...". Any comparison of two byte-identical files still "worked"
    by accident (identical strings trivially match under any method), but
    near-duplicates with different encodes would have been missed.
    """
    try:
        _duration, fp = acoustid.fingerprint_file(str(path))
        return fp.decode("ascii"), None
    except Exception as exc:  # noqa: BLE001 - keep scanning the rest of the library
        return None, str(exc)


def scan(library_root: Path, excluded_root: Path, dry_run: bool = False, min_bitrate: int = MIN_BITRATE) -> dict:
    """Returns a summary dict. In dry-run mode, nothing is written to the
    checkpoint DB -- the summary is computed from a throwaway in-memory pass."""
    from . import log

    logger = log.get()
    stats = {"total": 0, "new": 0, "unchanged": 0, "excluded": 0, "corrupt": 0, "low_quality": 0}

    with checkpoint.connect(excluded_root) as conn:
        # Computed in SQL rather than pulling the full fingerprint text
        # (~4.8KB/row, per fingerprint_file()'s own docstring on its size)
        # for every row just to check its first character -- at a
        # large-library scale that string alone would otherwise dominate
        # this dict's memory footprint for no reason, since all that's
        # ever needed here is the one-time-migration boolean below.
        existing = {
            row["path"]: (row["size"], row["mtime"], row["fingerprint_ok"], row["dedup_status"])
            for row in conn.execute(
                "SELECT path, size, mtime, "
                "(fingerprint IS NULL OR substr(fingerprint, 1, 1) != '[') AS fingerprint_ok, "
                "dedup_status FROM files"
            )
        }

        for path in iter_audio_files(library_root):
            stats["total"] += 1
            stat = path.stat()
            key = str(path)
            prior = existing.get(key)
            if prior is not None:
                p_size, p_mtime, fingerprint_ok, p_dedup_status = prior
                # One-time migration: fingerprints stored before the
                # byte-value bug fix are JSON lists (start with "["), not
                # the real base64 Chromaprint string -- force those to
                # re-fingerprint regardless of size/mtime match. A file
                # already past dedup (keeper/unique/loser) with a valid
                # fingerprint is left alone even if re-walked.
                if (p_size, p_mtime) == (stat.st_size, int(stat.st_mtime)) and fingerprint_ok:
                    stats["unchanged"] += 1
                    continue

            relative = path.relative_to(library_root)
            file_type = classify_type(relative)
            if file_type is None:
                stats["excluded"] += 1
                logger.info(f"[SCAN] skip (unrecognized folder): {relative}")
                continue

            probe = probe_audio(path)
            is_corrupt = probe.duration is None or probe.duration < MIN_DURATION_SECONDS
            # Unconditional quality floor -- excluded even with no duplicate
            # to fall back on. Lossless formats are exempt: their bitrate
            # reflects content/compression, not encode quality.
            is_low_quality = (
                not is_corrupt and not probe.is_lossless
                and probe.bitrate is not None and probe.bitrate < min_bitrate
            )
            fingerprint, fp_error = (None, None)

            # A re-fingerprint pass (migration case above) on a file that
            # already made it through dedup shouldn't reset that
            # classification back to NULL -- only a freshly-corrupt/
            # freshly-low-quality determination should override it.
            prior_dedup_status = prior[3] if prior is not None else None
            dedup_status = "corrupt" if is_corrupt else "low_quality" if is_low_quality else prior_dedup_status

            if is_corrupt:
                stats["corrupt"] += 1
                logger.info(f"[SCAN] corrupt/too short ({probe.duration}s): {relative} -> will relocate to Excluded Audio/Corrupt")
            elif is_low_quality:
                stats["low_quality"] += 1
                logger.info(f"[SCAN] low quality ({probe.bitrate}bps): {relative} -> will relocate to Excluded Audio/LowQuality")
            else:
                fingerprint, fp_error = fingerprint_file(path)
                logger.info(
                    f"[SCAN] {relative} -> type={file_type} duration={probe.duration:.1f}s "
                    f"codec={probe.codec} bitrate={probe.bitrate} lossless={probe.is_lossless}"
                    + ("" if fingerprint else f" (fingerprint failed: {fp_error})")
                )

            stats["new"] += 1

            if dry_run:
                continue

            # A rescan (triggered by mtime changing, e.g. an earlier
            # restructure pass embedding tags) can newly classify a file
            # that was already restructured into /Music as corrupt/
            # low_quality. restructure_done/final_relative_path must reset
            # here too, or that stale "already restructured" bookkeeping
            # permanently hides it from restructure.py's relocation query
            # (WHERE dedup_status=... AND restructure_done=0) -- confirmed
            # real bug: sub-120kbps files were correctly flagged here but
            # never physically moved because of exactly this.
            reset_restructure_clause = ""
            if dedup_status in ("corrupt", "low_quality"):
                reset_restructure_clause = ", restructure_done=0, final_relative_path=NULL"

            conn.execute(
                f"""INSERT INTO files (path, size, mtime, duration, codec, bitrate, is_lossless, fingerprint,
                                       scan_error, type, dedup_status)
                   VALUES (:path, :size, :mtime, :duration, :codec, :bitrate, :is_lossless, :fingerprint,
                           :scan_error, :type, :dedup_status)
                   ON CONFLICT(path) DO UPDATE SET
                       size=excluded.size, mtime=excluded.mtime, duration=excluded.duration,
                       codec=excluded.codec, bitrate=excluded.bitrate, is_lossless=excluded.is_lossless,
                       fingerprint=excluded.fingerprint, scan_error=excluded.scan_error, type=excluded.type,
                       dedup_status=excluded.dedup_status{reset_restructure_clause}""",
                {
                    "path": key,
                    "size": stat.st_size,
                    "mtime": int(stat.st_mtime),
                    "duration": probe.duration,
                    "codec": probe.codec,
                    "bitrate": probe.bitrate,
                    "is_lossless": int(probe.is_lossless),
                    "fingerprint": fingerprint,
                    "scan_error": fp_error or probe.error,
                    "type": file_type,
                    "dedup_status": dedup_status,
                },
            )
            if stats["new"] % 200 == 0:
                conn.commit()

    return stats
