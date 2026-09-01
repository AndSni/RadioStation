"""cleanup.py -- single entry point for the music library cleanup pipeline.

Stage order: exclude -> scan -> dedup -> retag -> lastfm-correct ->
intro-outro -> title-dedup -> energy -> artist-folder-merge -> restructure
-> embed -> manifest (see stages.py, the shared source of truth for this order and
each stage's gating, also used by the TUI). Each stage only processes
files the checkpoint DB
(<excluded_root>/_pipeline/state.sqlite) hasn't marked done yet, so any
subcommand is safe to re-run. Every stage logs per-file progress to both
the terminal and <excluded_root>/_pipeline/cleanup.log (see log.py) -- tail
that file if you background a long run (retag especially), or use `tui`
for a live view.
"""

from pathlib import Path

import click

from . import artist_folder_normalize
from . import checkpoint, dedup as dedup_mod, embed as embed_mod, energy as energy_mod, exclude as exclude_mod
from . import intro_outro as intro_outro_mod
from . import lastfm_correct as lastfm_correct_mod
from . import manifest as manifest_mod
from . import rename_in_place as rename_in_place_mod
from . import retag as retag_mod
from . import restructure as restructure_mod
from . import scan as scan_mod
from . import stages as stages_mod
from . import title_dedup as title_dedup_mod
from . import undo as undo_mod


def _root_options(f):
    f = click.option(
        "--root", "library_root", required=True, type=click.Path(exists=True, file_okay=False, path_type=Path),
        help="Library root directory to clean up (e.g. /mnt/data/library/Music).",
    )(f)
    f = click.option(
        "--excluded-root", "excluded_root", required=True, type=click.Path(file_okay=False, path_type=Path),
        help="Parallel directory exception content and pipeline bookkeeping move into "
             "(e.g. /mnt/data/library/Excluded Audio). Created if missing.",
    )(f)
    return f


def _setup(excluded_root: Path):
    from . import log as log_module
    excluded_root.mkdir(parents=True, exist_ok=True)
    return log_module.setup(excluded_root)


def _require_complete(excluded_root: Path, stage_key: str):
    remaining = stages_mod.blocking_count(stage_key, excluded_root)
    if remaining:
        prior_label = stages_mod.STAGES_BY_KEY[stages_mod.STAGES_BY_KEY[stage_key].prior_stage_key].label
        raise click.ClickException(
            f"{remaining} file(s) haven't finished the '{prior_label}' stage yet -- run it first."
        )


@click.group()
def main():
    """Music library cleanup pipeline: relocate exceptions, dedup, retag, energy-score, restructure."""


@main.command()
@_root_options
@click.option("--dry-run", is_flag=True, help="Report what would move; move nothing.")
def exclude(library_root, excluded_root, dry_run):
    """Stage 0: relocate RAW/GTA_Radio/Jingles/Sleep/Audio Books/Playlists to --excluded-root, untouched."""
    _setup(excluded_root)
    stats = exclude_mod.relocate_exceptions(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--min-bitrate", default=scan_mod.MIN_BITRATE, type=int, show_default=True,
              help="Lossy files below this (bits/sec) are relocated to Excluded Audio/LowQuality unconditionally.")
@click.option("--dry-run", is_flag=True, help="Report what would be scanned; write nothing.")
def scan(library_root, excluded_root, min_bitrate, dry_run):
    """Stage 1: walk the tree, cache probe data + fingerprint per file."""
    _setup(excluded_root)
    stats = scan_mod.scan(library_root, excluded_root, dry_run=dry_run, min_bitrate=min_bitrate)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--dedup-threshold", default=dedup_mod.DEFAULT_THRESHOLD, type=float, show_default=True)
@click.option("--dry-run", is_flag=True, help="Write dry_run_report.json; move nothing.")
def dedup(library_root, excluded_root, dedup_threshold, dry_run):
    """Stage 2: cluster fingerprint-duplicates, move losers to <excluded-root>/Duplicates/."""
    _setup(excluded_root)
    unfingerprinted = _count(excluded_root, "dedup_status IS NULL AND fingerprint IS NULL")
    if unfingerprinted:
        click.echo(
            f"note: {unfingerprinted} unfingerprinted file(s) will be treated as unique "
            "(scan_error explains why per-file)."
        )
    stats = dedup_mod.dedup(library_root, excluded_root, threshold=dedup_threshold, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--workers", default=retag_mod.ACOUSTID_WORKERS, type=int, show_default=True,
              help="Concurrent AcoustID lookups (fast path). The MusicBrainz fallback always runs sequentially.")
@click.option("--dry-run", is_flag=True)
def retag(library_root, excluded_root, workers, dry_run):
    """Stage 3: re-tag Type=Song survivors -- AcoustID first, MusicBrainz fallback for the rest."""
    _setup(excluded_root)
    _require_complete(excluded_root, "retag")
    stats = retag_mod.retag(library_root, excluded_root, dry_run=dry_run, workers=workers)
    click.echo(stats)


@main.command("lastfm-correct")
@_root_options
@click.option("--workers", default=lastfm_correct_mod.LASTFM_WORKERS, type=int, show_default=True,
              help="Concurrent Last.fm correction lookups.")
@click.option("--dry-run", is_flag=True)
def lastfm_correct_cmd(library_root, excluded_root, workers, dry_run):
    """Stage 4: cross-check retag's artist/title against Last.fm's own name database -- near-identical corrections (spelling/case/diacritics only) auto-apply, anything more different goes to lastfm_correction_report.json for review."""
    _setup(excluded_root)
    _require_complete(excluded_root, "lastfm_correct")
    stats = lastfm_correct_mod.correct_names(library_root, excluded_root, dry_run=dry_run, workers=workers)
    click.echo(stats)


@main.command("lastfm-apply-correction")
@_root_options
@click.option("--from-artist", "from_artist", required=True, help="Current artist string, as reviewed in lastfm_correction_report.json.")
@click.option("--from-title", "from_title", required=True, help="Current title string, as reviewed in lastfm_correction_report.json.")
@click.option("--to-artist", "to_artist", required=True, help="Last.fm's suggested artist to apply.")
@click.option("--to-title", "to_title", required=True, help="Last.fm's suggested title to apply.")
@click.option("--dry-run", is_flag=True)
def lastfm_apply_correction_cmd(library_root, excluded_root, from_artist, from_title, to_artist, to_title, dry_run):
    """Manually apply one reviewed entry from lastfm_correction_report.json (works before or after restructure)."""
    _setup(excluded_root)
    stats = lastfm_correct_mod.manual_apply(
        library_root, excluded_root, from_artist, from_title, to_artist, to_title, dry_run=dry_run
    )
    click.echo(stats)


@main.command("intro-outro")
@_root_options
@click.option("--dry-run", is_flag=True, help="Write intro_outro_report.json; move nothing.")
def intro_outro_cmd(library_root, excluded_root, dry_run):
    """Stage 5: flag intro/outro/filler tracks; confident name matches move to Excluded Audio/IntroOutro, ambiguous short tracks go to needs-review."""
    _setup(excluded_root)
    _require_complete(excluded_root, "intro_outro")
    stats = intro_outro_mod.intro_outro(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command("title-dedup")
@_root_options
@click.option("--dry-run", is_flag=True, help="Write title_dedup_report.json; move nothing.")
def title_dedup_cmd(library_root, excluded_root, dry_run):
    """Stage 6: collapse the same (artist, title) across different releases/lengths to the best-quality copy."""
    _setup(excluded_root)
    _require_complete(excluded_root, "title_dedup")
    stats = title_dedup_mod.title_dedup(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--workers", default=4, type=int, show_default=True)
@click.option("--dry-run", is_flag=True)
def energy(library_root, excluded_root, workers, dry_run):
    """Stage 7: score Type=Song survivors' energy + BPM via audio analysis."""
    _setup(excluded_root)
    _require_complete(excluded_root, "energy")
    stats = energy_mod.energy(library_root, excluded_root, workers=workers, dry_run=dry_run)
    click.echo(stats)


@main.command("artist-folder-merge")
@_root_options
@click.option("--fuzzy-threshold", default=artist_folder_normalize.DEFAULT_FUZZY_THRESHOLD, type=float, show_default=True)
@click.option("--dry-run", is_flag=True)
def artist_folder_merge_cmd(library_root, excluded_root, fuzzy_threshold, dry_run):
    """Stage 8: auto-merge exact-normalization artist-name variants; write ambiguous ones to a fuzzy report for review."""
    _setup(excluded_root)
    _require_complete(excluded_root, "artist_folder_merge")
    stats = artist_folder_normalize.auto_merge(library_root, excluded_root, fuzzy_threshold=fuzzy_threshold, dry_run=dry_run)
    click.echo(stats)


@main.command("artist-merge")
@_root_options
@click.option("--from-artist", "from_artist", required=True, help="Existing artist string to merge away.")
@click.option("--to-artist", "to_artist", required=True, help="Canonical artist name to merge into.")
@click.option("--dry-run", is_flag=True)
def artist_merge_cmd(library_root, excluded_root, from_artist, to_artist, dry_run):
    """Manually apply one reviewed entry from artist_folder_fuzzy_report.json (works before or after restructure)."""
    _setup(excluded_root)
    stats = artist_folder_normalize.manual_merge(library_root, excluded_root, from_artist, to_artist, dry_run=dry_run)
    click.echo(stats)


@main.command("collab-split")
@_root_options
@click.option("--artist", required=True, help="Full credit string to split, e.g. 'BABYMETAL, Electric Callboy'.")
@click.option("--dry-run", is_flag=True)
def collab_split_cmd(library_root, excluded_root, artist, dry_run):
    """Manually apply one reviewed entry from collab_credit_report.json (works before or after restructure)."""
    _setup(excluded_root)
    stats = artist_folder_normalize.manual_collab_split(library_root, excluded_root, artist, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--dry-run", is_flag=True)
def restructure(library_root, excluded_root, dry_run):
    """Stage 9: move survivors to /Music/<Artist>/<Song>.ext; relocate corrupt/needs-review files elsewhere."""
    _setup(excluded_root)
    _require_complete(excluded_root, "restructure")
    stats = restructure_mod.restructure(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--dry-run", is_flag=True, help="Report what would be renamed; rename nothing.")
def rename(library_root, excluded_root, dry_run):
    """Rename survivors to '<Artist> - <Title>.ext' IN PLACE and embed tags -- like `restructure` but never moves a file out of its folder or prunes empty directories. For a hand-curated layout (e.g. a portable-player copy sorted <Genre>/<Artist>/) that must be preserved. Run instead of `restructure`, any time after `retag`."""
    _setup(excluded_root)
    retag_remaining = stages_mod.remaining_count(stages_mod.STAGES_BY_KEY["retag"], excluded_root) or 0
    if retag_remaining:
        raise click.ClickException(
            f"{retag_remaining} file(s) haven't finished the 'retag' stage yet -- run it first."
        )
    stats = rename_in_place_mod.rename_in_place(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--dry-run", is_flag=True, help="Report what would be re-embedded; write nothing.")
def embed(library_root, excluded_root, dry_run):
    """Re-embed every current survivor's tags from its present DB values, regardless of restructure history. Always safe to re-run -- run standalone any time upstream metadata logic improves, to push corrected values into files restructured before the fix landed."""
    _setup(excluded_root)
    stats = embed_mod.embed_all(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
def manifest(library_root, excluded_root):
    """Render manifest.csv (inside --root) from current checkpoint state. Always safe to re-run."""
    _setup(excluded_root)
    stats = manifest_mod.write_manifest(library_root, excluded_root)
    click.echo(stats)


@main.command()
@_root_options
def undo(library_root, excluded_root):
    """Reverse every dedup/title-dedup move recorded in the undo log."""
    _setup(excluded_root)
    log_path = checkpoint.undo_log_path(excluded_root)
    if not log_path.exists():
        click.echo("No undo log found -- nothing to undo.")
        return
    stats = undo_mod.undo(log_path, library_root, excluded_root)
    click.echo(stats)


@main.command()
@_root_options
@click.option("--yes", is_flag=True, help="Skip the confirmation prompt.")
def reset(library_root, excluded_root, yes):
    """Back up and clear pipeline bookkeeping for a full fresh re-scan.

    Never touches /Music or Excluded Audio file contents -- files already
    relocated by a previous run (Duplicates/Corrupt/NeedsReview/restructured
    Artist folders) are NOT reverted. Run `undo` first if you also want to
    reverse previous dedup moves before rescanning.
    """
    _setup(excluded_root)
    if not yes:
        click.confirm(
            "This clears all scan/dedup/retag/energy/restructure progress tracking "
            "(backed up, not deleted). Previously-moved files stay where they are. Continue?",
            abort=True,
        )
    backup_dir = checkpoint.reset_pipeline_state(excluded_root)
    click.echo(f"Pipeline state backed up to {backup_dir}. Next `scan` starts fresh.")


@main.command("wipe-bpm-energy")
@_root_options
@click.option("--yes", is_flag=True, help="Skip the confirmation prompt.")
@click.option("--dry-run", is_flag=True, help="Report what would be reset; change nothing.")
def wipe_bpm_energy_cmd(library_root, excluded_root, yes, dry_run):
    """Strip BPM/Energy from every current survivor's file tags and DB columns, for a clean full-library recompute after a detector improvement. Follow with `energy` then `embed` to recompute and re-push corrected values."""
    _setup(excluded_root)
    if not dry_run and not yes:
        click.confirm(
            "This strips BPM/Energy tags from every current survivor's file and clears bpm/energy/energy_raw "
            "in the DB. Run `energy` afterward to recompute, then `embed` to push the results back into files. Continue?",
            abort=True,
        )
    stats = energy_mod.wipe_bpm_energy(library_root, excluded_root, dry_run=dry_run)
    click.echo(stats)


@main.command()
@_root_options
@click.argument("paths", nargs=-1, required=True, type=click.Path(exists=True, dir_okay=False, path_type=Path))
@click.option("--dry-run", is_flag=True, help="Report what would be re-analyzed; change nothing.")
def analyze(library_root, excluded_root, paths, dry_run):
    """On-demand re-analysis (BPM/Energy) for one or more specific files already tracked by the pipeline -- recomputes and immediately re-embeds, no separate `embed` run needed. The seed of the future `ourtool --run-bpm-scan filename.ext` capability for RadioStation to shell out to."""
    _setup(excluded_root)
    stats = energy_mod.analyze_paths(library_root, excluded_root, [str(p) for p in paths], dry_run=dry_run)
    click.echo(stats)


@main.command()
def tui():
    """Launch the interactive terminal UI (drag-and-drop-friendly folder entry, live progress/ETA)."""
    from . import tui as tui_mod
    tui_mod.run_tui()


def _count(excluded_root: Path, where: str) -> int:
    with checkpoint.connect(excluded_root) as conn:
        return conn.execute(f"SELECT COUNT(*) FROM files WHERE {where}").fetchone()[0]


if __name__ == "__main__":
    main()
