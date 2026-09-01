"""Single source of truth for pipeline stage order + gating, read by both
cli.py's `_require_complete` checks and the TUI's progress/status display --
one definition of "what's done/pending/blocked", not two hand-written
copies that could silently drift apart.

Stage order: exclude -> scan -> dedup -> retag -> lastfm_correct ->
intro_outro -> title_dedup -> energy -> artist_folder_merge -> restructure
-> embed -> manifest.

embed sits after restructure and has no gate (remaining_where/
prior_stage_key both None, like manifest) -- it's not a one-shot step in the
normal chain but an always-safe-to-rerun resync: restructure.py already
embeds tags itself at first placement, so on a normal fresh run embed is a
harmless no-op re-write. Its real purpose is reaching files that were
already restructured *before* some later metadata improvement landed (e.g.
a better BPM detector) -- run it standalone any time upstream data changes,
without needing to touch restructure_done or redo the physical move.

`rename` (cli.py) is an out-of-band ALTERNATIVE to `restructure`, not a
member of STAGES: it renames survivors to "<Artist> - <Title>.ext" in
place and embeds tags, but never moves a file into an <Artist>/ folder or
prunes directories -- for a library whose folder layout is hand-curated
and must be preserved (e.g. a portable-player copy sorted <Genre>/<Artist>/).
It gates on `retag` alone and is run by hand instead of `restructure`;
keeping it out of STAGES is deliberate, so the TUI's "Run All" never runs
both movers over the same library.

lastfm_correct sits right after retag (needs an artist/title to cross-check
against Last.fm's independent name database) and before intro_outro/
title_dedup, so both of those group/match on Last.fm-corrected names rather
than clean-but-possibly-wrong ones -- a corrected title changes which
filler-name patterns match, and a corrected artist/title pair changes which
cross-release duplicates group together. intro_outro sits right after that
(needs clean title/artist to match filler-name patterns against) and before
title_dedup, so filler tracks never enter that stage's (artist, title)
grouping. title_dedup sits right after intro_outro and before energy (no
point running ~1.7s/file librosa analysis on a file about to be discarded
as a cross-release duplicate). artist_folder_merge sits right before
restructure (must fix the artist column before folders get created from it
-- no post-hoc filesystem-level folder merging ever needed for the
auto-merge path).

Stage.cli_command names the click subcommand (see cli.py) rather than
holding a direct Python callable -- the TUI runs every stage as a real
subprocess of `cleanup.py`, not an in-process function call (see tui.py's
own docstring for why: energy.py's ProcessPoolExecutor crashed twice when
called from inside the TUI's own process, both times some interaction
between Python 3.14's multiprocessing internals and the Textual app's own
threads/terminal-fd state -- a fresh, plain `cleanup.py <command>` process
has none of that baggage, and is exactly what already works when a user
types the same command by hand).
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional


@dataclass
class Stage:
    key: str
    label: str
    cli_command: str  # click subcommand name, e.g. "title-dedup"
    # SQL predicate (against the `files` table) counting this stage's own
    # "not yet done" rows -- None means there's no meaningful per-row count
    # before this stage runs (exclude/scan create the rows in the first
    # place; manifest is always safe to re-run, no gate needed after it).
    # Must reflect TRUE completion (other stages' gating depends on this
    # reaching 0) -- see progress_where below for why that disqualifies it
    # from also being the live progress signal for every stage.
    remaining_where: Optional[str]
    # Stage.key of the stage that must be fully done (remaining_where count
    # == 0) before this one is allowed to run. None means no gate.
    prior_stage_key: Optional[str]
    # UI-display-only override of remaining_where, for a stage whose real
    # completion column only gets its final write in a late batch pass --
    # energy.py's Pass 2 (library-wide normalization) only sets `energy`
    # after Pass 1 (the actual per-file audio analysis, i.e. the expensive
    # part visible in the log) has already finished for every row, so
    # `energy IS NULL` barely moves during the entire run and then drops to
    # 0 in one jump at the end. Confirmed real: a user watching the TUI
    # during a 7,382-file energy run saw "23/7382 done" stay stuck for many
    # minutes while the log scrolled through hundreds of completed files.
    # progress_where tracks the column Pass 1 itself writes per-file
    # (energy_raw), so the displayed count/ETA actually moves with the
    # visible work. None means "same as remaining_where" (every other
    # stage). Never used for gating -- only remaining_count feeds
    # blocking_count/_require_complete.
    progress_where: Optional[str] = None


STAGES: list[Stage] = [
    Stage("exclude", "Exclude exception folders", "exclude", None, None),
    Stage("scan", "Scan + fingerprint", "scan", None, "exclude"),
    Stage("dedup", "Fingerprint dedup", "dedup", "dedup_status IS NULL", "scan"),
    Stage(
        "retag", "Retag (AcoustID/Discogs/MusicBrainz)", "retag",
        "type='Song' AND dedup_status IN ('unique','keeper') AND retag_done=0", "dedup",
    ),
    Stage(
        "lastfm_correct", "Name correction (Last.fm)", "lastfm-correct",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND retag_done=1 AND lastfm_correct_done=0",
        "retag",
    ),
    Stage(
        "intro_outro", "Intro/outro/filler detection", "intro-outro",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND retag_done=1 AND intro_outro_done=0",
        "lastfm_correct",
    ),
    Stage(
        "title_dedup", "Title-based cross-release dedup", "title-dedup",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND retag_done=1 AND title_dedup_done=0",
        "intro_outro",
    ),
    Stage(
        "energy", "Energy + BPM analysis", "energy",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND title_dedup_done=1 AND energy IS NULL",
        "title_dedup",
        progress_where=(
            "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
            "AND title_dedup_done=1 AND energy_raw IS NULL"
        ),
    ),
    Stage(
        "artist_folder_merge", "Artist-folder-name normalization", "artist-folder-merge",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND energy IS NOT NULL AND artist_folder_done=0",
        "energy",
    ),
    Stage(
        "restructure", "Restructure into Artist folders", "restructure",
        "type='Song' AND dedup_status IN ('unique','keeper') AND needs_review=0 "
        "AND artist_folder_done=1 AND restructure_done=0",
        "artist_folder_merge",
    ),
    Stage("embed", "Re-embed tags into files", "embed", None, None),
    Stage("manifest", "Render manifest.csv", "manifest", None, "restructure"),
]

STAGES_BY_KEY = {s.key: s for s in STAGES}


def remaining_count(stage: Stage, excluded_root: Path) -> Optional[int]:
    """None if this stage has no meaningful per-row count (see
    Stage.remaining_where's own docstring). Feeds gating (blocking_count) --
    always reflects true completion. Use progress_remaining_count instead
    for UI display/ETA, which can differ for a stage like energy whose
    real completion column only gets its final write in a late batch pass."""
    if stage.remaining_where is None:
        return None
    from . import checkpoint
    with checkpoint.connect(excluded_root) as conn:
        return conn.execute(f"SELECT COUNT(*) FROM files WHERE {stage.remaining_where}").fetchone()[0]


def progress_remaining_count(stage: Stage, excluded_root: Path) -> Optional[int]:
    """Like remaining_count, but uses Stage.progress_where when the stage
    has one -- purely for UI display (stage-table "Remaining" column, ETA
    calculation), never for gating. Falls back to remaining_where when no
    override is set (every stage except energy, today)."""
    where = stage.progress_where or stage.remaining_where
    if where is None:
        return None
    from . import checkpoint
    with checkpoint.connect(excluded_root) as conn:
        return conn.execute(f"SELECT COUNT(*) FROM files WHERE {where}").fetchone()[0]


def blocking_count(stage_key: str, excluded_root: Path) -> int:
    """How many rows still block stage_key's prior stage from counting as
    done -- 0 means clear to run. Used by both cli.py (raise if nonzero)
    and the TUI (gray out the Run button if nonzero)."""
    stage = STAGES_BY_KEY[stage_key]
    if stage.prior_stage_key is None:
        return 0
    prior = STAGES_BY_KEY[stage.prior_stage_key]
    return remaining_count(prior, excluded_root) or 0
