"""Shared SQLite checkpoint state for the cleanup pipeline.

One row per source file, one column per stage. Every stage selects rows
where its own column is unset, so re-running any subcommand only processes
what's left to do -- resumable/idempotent by construction, no separate
skip-list needed.

All pipeline bookkeeping (this DB, the undo log, dry-run reports) lives
under `<excluded_root>/_pipeline/`, NOT inside the library root -- /Music
needs to stay strictly audio + Artist folders for a clean copy into
AzuraCast. `<excluded_root>` is the parallel "/Excluded Audio" tree.
"""

import shutil
import sqlite3
from contextlib import contextmanager
from datetime import datetime, timezone
from pathlib import Path

SCHEMA = """
CREATE TABLE IF NOT EXISTS files (
    path TEXT PRIMARY KEY,
    size INTEGER NOT NULL,
    mtime INTEGER NOT NULL,
    duration REAL,
    codec TEXT,
    bitrate INTEGER,
    is_lossless INTEGER,
    fingerprint TEXT,
    scan_error TEXT,
    dedup_status TEXT,
    dedup_cluster_id TEXT,
    dedup_score REAL,
    duplicate_of TEXT,
    type TEXT,
    retag_done INTEGER NOT NULL DEFAULT 0,
    artist TEXT,
    title TEXT,
    album TEXT,
    genre TEXT,
    year INTEGER,
    energy_raw TEXT,
    energy REAL,
    bpm REAL,
    restructure_done INTEGER NOT NULL DEFAULT 0,
    final_relative_path TEXT,
    title_dedup_done INTEGER NOT NULL DEFAULT 0,
    artist_folder_done INTEGER NOT NULL DEFAULT 0,
    needs_review INTEGER NOT NULL DEFAULT 0,
    needs_review_reason TEXT,
    intro_outro_done INTEGER NOT NULL DEFAULT 0,
    lastfm_correct_done INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_files_dedup_status ON files(dedup_status);
CREATE INDEX IF NOT EXISTS idx_files_type ON files(type);
"""

# Process-local cache of DB paths whose schema has already been verified
# (CREATE TABLE IF NOT EXISTS + _migrate()'s ALTER TABLE checks) this
# process -- see connect()'s own docstring for why re-running that on every
# call is wasteful and, for the TUI's 1s progress-poll, actively works
# against the reason WAL was chosen in the first place. Simple set, no
# lock: CPython's GIL makes the single in/add/discard check atomic enough
# here -- worst case under a race is one redundant (idempotent) migration
# run, not corruption.
_schema_verified: set[str] = set()

PIPELINE_DIR_NAME = "_pipeline"
CHECKPOINT_NAME = "state.sqlite"
UNDO_LOG_NAME = "dedup_undo_log.jsonl"
DRY_RUN_REPORT_NAME = "dry_run_report.json"
TITLE_DEDUP_REPORT_NAME = "title_dedup_report.json"
INTRO_OUTRO_REPORT_NAME = "intro_outro_report.json"
ARTIST_FUZZY_REPORT_NAME = "artist_folder_fuzzy_report.json"
COLLAB_CREDIT_REPORT_NAME = "collab_credit_report.json"
ARTIST_MERGE_LOG_NAME = "artist_merge_log.jsonl"
NEEDS_REVIEW_REPORT_NAME = "needs_review_report.jsonl"
LASTFM_CORRECTION_REPORT_NAME = "lastfm_correction_report.json"

# Top-level folder name under excluded_root that duplicate losers move into
# (both the original fingerprint-based dedup.py and the post-retag
# title_dedup.py share this same folder -- they're both "duplicate audio",
# just identified by different signals).
DUPLICATES_FOLDER = "Duplicates"
# Top-level folder name under excluded_root that corrupt/unfingerprintable
# stub files (too short to be real audio) move into.
CORRUPT_FOLDER = "Corrupt"
# Top-level folder name under excluded_root that low-confidence
# filename-derived artist/title guesses (see artist_normalize.is_garbled_name)
# move into instead of ever becoming a real Artist folder.
NEEDS_REVIEW_FOLDER = "NeedsReview"
# Top-level folder name under excluded_root that files below
# audio_probe.MIN_BITRATE move into -- unconditional quality floor, not a
# dedup outcome.
LOW_QUALITY_FOLDER = "LowQuality"
# Top-level folder name under excluded_root that confidently-identified
# intro/outro/filler tracks (see filler.py) move into.
INTRO_OUTRO_FOLDER = "IntroOutro"


def pipeline_dir(excluded_root: Path) -> Path:
    path = excluded_root / PIPELINE_DIR_NAME
    path.mkdir(parents=True, exist_ok=True)
    return path


def checkpoint_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / CHECKPOINT_NAME


def undo_log_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / UNDO_LOG_NAME


def dry_run_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / DRY_RUN_REPORT_NAME


def title_dedup_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / TITLE_DEDUP_REPORT_NAME


def intro_outro_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / INTRO_OUTRO_REPORT_NAME


def artist_fuzzy_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / ARTIST_FUZZY_REPORT_NAME


def collab_credit_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / COLLAB_CREDIT_REPORT_NAME


def artist_merge_log_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / ARTIST_MERGE_LOG_NAME


def needs_review_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / NEEDS_REVIEW_REPORT_NAME


def lastfm_correction_report_path(excluded_root: Path) -> Path:
    return pipeline_dir(excluded_root) / LASTFM_CORRECTION_REPORT_NAME


def _migrate(conn: sqlite3.Connection):
    """CREATE TABLE IF NOT EXISTS won't add columns to a table that already
    exists on disk -- this adds any column present in SCHEMA but missing
    from an existing DB, so a schema change never requires deleting (and
    losing) already-recorded scan/dedup/retag progress."""
    existing = {row[1] for row in conn.execute("PRAGMA table_info(files)")}
    column_defs = {
        "album": "TEXT",
        "bpm": "REAL",
        "title_dedup_done": "INTEGER NOT NULL DEFAULT 0",
        "artist_folder_done": "INTEGER NOT NULL DEFAULT 0",
        "needs_review": "INTEGER NOT NULL DEFAULT 0",
        "needs_review_reason": "TEXT",
        "intro_outro_done": "INTEGER NOT NULL DEFAULT 0",
        "lastfm_correct_done": "INTEGER NOT NULL DEFAULT 0",
    }
    for name, sql_type in column_defs.items():
        if name not in existing:
            conn.execute(f"ALTER TABLE files ADD COLUMN {name} {sql_type}")


def reset_pipeline_state(excluded_root: Path) -> Path:
    """Moves every file currently under <excluded_root>/_pipeline/ (the
    checkpoint DB, undo log, cleanup.log, every report) into a timestamped
    backup subfolder, so the next connect() call creates a brand-new empty
    DB. Never deletes anything; never touches library_root or any
    already-relocated Duplicates/Corrupt/NeedsReview content -- those are
    real file moves already made, not bookkeeping to discard."""
    pdir = pipeline_dir(excluded_root)
    stamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    backup_dir = pdir / f"reset_backup_{stamp}"
    backup_dir.mkdir(parents=True)
    for entry in list(pdir.iterdir()):
        if entry == backup_dir:
            continue
        shutil.move(str(entry), str(backup_dir / entry.name))
    # The DB file itself just got moved away -- the next connect() call
    # will create a brand-new empty file at this same path and genuinely
    # needs schema creation, so the "already verified" cache entry (if any)
    # must not survive a reset.
    _schema_verified.discard(str(checkpoint_path(excluded_root).resolve()))
    return backup_dir


@contextmanager
def connect(excluded_root: Path):
    """WAL journal mode (rather than SQLite's rollback-journal default) is
    what makes this safe for the TUI's usage pattern: one long-lived writer
    connection held open for an entire stage's duration (retag.py in
    particular, potentially for hours), while the TUI's 1s progress-poll
    timer opens its own short-lived connection on the main thread to read
    row counts. Under the default journal mode a writer's in-progress
    transaction can make a concurrent reader's connection raise "database
    is locked" (confirmed real crash) even well within the busy timeout;
    WAL is specifically designed so readers never block on a writer.
    Persisted in the DB file's own header, so this only needs to run once
    per file, but is cheap/idempotent to set on every connect().

    Schema creation (executescript(SCHEMA) + _migrate()) is only actually
    run once per DB path per process, cached in _schema_verified -- each
    pipeline stage already only calls connect() once for its whole run (a
    real subprocess of `cleanup.py <command>`, per tui.py's own docstring),
    so that's unaffected. But the TUI's 1s progress-poll timer calls this
    fresh every second for the life of a potentially hours-long running
    stage, purely to read a row count -- without the cache, that means
    schema DDL statements once a second indefinitely, which also runs
    against the very reason WAL was chosen above (a poller issuing writes
    isn't the non-contending "reader" that reasoning assumes). See
    reset_pipeline_state(), which must invalidate this cache since it moves
    the DB file away out from under it."""
    conn = sqlite3.connect(checkpoint_path(excluded_root), timeout=10.0)
    conn.row_factory = sqlite3.Row
    conn.execute("PRAGMA journal_mode=WAL")
    db_key = str(checkpoint_path(excluded_root).resolve())
    if db_key not in _schema_verified:
        conn.executescript(SCHEMA)
        _migrate(conn)
        _schema_verified.add(db_key)
    try:
        yield conn
        conn.commit()
    finally:
        conn.close()
