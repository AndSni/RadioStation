"""Shared logging setup: every stage logs per-file progress to both the
terminal and a log file, so a long-running stage (retag in particular can
run for hours) can be backgrounded and tailed.
"""

import logging
from pathlib import Path

_configured = False


def setup(excluded_root: Path) -> logging.Logger:
    global _configured
    logger = logging.getLogger("cleanup")
    if _configured:
        return logger

    log_dir = excluded_root / "_pipeline"
    log_dir.mkdir(parents=True, exist_ok=True)

    logger.setLevel(logging.INFO)
    formatter = logging.Formatter("%(asctime)s %(message)s", datefmt="%H:%M:%S")

    stream_handler = logging.StreamHandler()
    stream_handler.setFormatter(formatter)
    logger.addHandler(stream_handler)

    file_handler = logging.FileHandler(log_dir / "cleanup.log")
    file_handler.setFormatter(formatter)
    logger.addHandler(file_handler)

    _configured = True
    return logger


def get() -> logging.Logger:
    return logging.getLogger("cleanup")
