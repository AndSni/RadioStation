"""Small shared networking helpers used by every catalog-lookup source
(AcoustID/Discogs in retag.py, Last.fm in lastfm_correct.py) -- kept in one
place so pacing and API-key loading don't triplicate across modules that
all do the exact same thing against a different host.
"""

import os
import threading
import time
from pathlib import Path

# Same directory AcoustID's/Discogs' key files already live in (see
# retag.py's own history) -- not beets-specific in meaning, just a
# convenient already-gitignored spot for small local secret files.
KEYS_DIR = Path(__file__).resolve().parent.parent / ".beets"


class RateLimiter:
    """Thread-safe minimum-interval pacer -- shared across every caller
    (a ThreadPoolExecutor's workers, or a sequential loop) so concurrent or
    rapid requests can't outrun a source's documented rate limit. Not a
    retry/backoff mechanism -- just prevents this code from causing the
    429s in the first place."""

    def __init__(self, min_interval: float):
        self._min_interval = min_interval
        self._lock = threading.Lock()
        self._last_call = 0.0

    def wait(self) -> None:
        with self._lock:
            now = time.monotonic()
            delay = self._min_interval - (now - self._last_call)
            if delay > 0:
                time.sleep(delay)
            self._last_call = time.monotonic()


def read_api_key(key_file: Path, env_var: str) -> str | None:
    """An env var always wins (handy for CI/one-off overrides without
    touching a file on disk); otherwise reads the key file, stripped of
    surrounding whitespace/newline. None if neither is set."""
    key = os.environ.get(env_var)
    if key:
        return key
    if key_file.exists():
        return key_file.read_text().strip()
    return None
