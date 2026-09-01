"""Cheap per-file audio probing shared by scan/dedup/energy stages."""

from dataclasses import dataclass
from pathlib import Path

import mutagen

AUDIO_EXTENSIONS = {
    ".mp3", ".flac", ".m4a", ".m4b", ".m4r", ".ogg", ".opus", ".wav", ".ape", ".webm",
}
LOSSLESS_EXTENSIONS = {".flac", ".wav", ".ape"}

# Files shorter than this are almost always broken/truncated downloads, not
# real duplicates -- flagged separately rather than fed into fingerprint
# clustering, where a near-empty fingerprint spuriously "matches" everything.
MIN_DURATION_SECONDS = 5.0

# Below this, a lossy file is below the quality floor for AzuraCast playout
# and gets excluded unconditionally, even if it's the only copy of the song
# -- not just a dedup tiebreak. Doesn't apply to lossless formats, whose
# reported bitrate reflects content/compression ratio, not encode quality.
MIN_BITRATE = 120_000


@dataclass
class ProbeResult:
    duration: float | None
    codec: str
    bitrate: int | None
    is_lossless: bool
    error: str | None = None


def probe_audio(path: Path) -> ProbeResult:
    codec = path.suffix.lower().lstrip(".")
    is_lossless = path.suffix.lower() in LOSSLESS_EXTENSIONS
    try:
        audio = mutagen.File(path)
    except Exception as exc:  # noqa: BLE001 - want to record and keep going
        return ProbeResult(None, codec, None, is_lossless, error=str(exc))
    if audio is None or audio.info is None:
        return ProbeResult(None, codec, None, is_lossless, error="mutagen could not parse file")
    duration = getattr(audio.info, "length", None)
    bitrate = getattr(audio.info, "bitrate", None)
    if bitrate is not None:
        bitrate = int(bitrate)
    return ProbeResult(duration, codec, bitrate, is_lossless)
