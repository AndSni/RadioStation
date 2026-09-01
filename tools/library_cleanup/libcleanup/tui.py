r"""Interactive terminal UI for the cleanup pipeline -- Ubuntu-server-setup
/ nmtui style: one flat main screen, no deep menu nesting.

Every stage runs as a real subprocess of `cleanup.py <command>` -- NOT an
in-process function call. This was a deliberate change after two separate
crashes: energy.py's ProcessPoolExecutor, called directly from inside the
TUI's own process, hit multiprocessing internals that don't get along with
a Textual app's own threads/terminal-fd state (confirmed twice, by direct
reproduction, two different failure modes -- a `-m`-launch/forkserver
__main__-reimport issue, then a resource_tracker "bad fds_to_keep" issue
even after fixing the first one). A fresh `cleanup.py <command>` subprocess
has none of that baggage -- it's exactly the same process shape as a user
typing the command by hand, which has always worked. This also means
"Stop" can now genuinely terminate the running stage immediately, rather
than only taking effect between stages -- safe because every stage already
commits its checkpoint DB progress per-file/per-row (proven crash-resumable
design throughout this codebase), so killing a stage subprocess mid-run is
no different from any other interruption it's already built to tolerate.

Progress/ETA come from polling the checkpoint DB's own row counts on a
timer (stages.remaining_count) -- the subprocess doesn't need to report
progress itself. The subprocess's own log output goes to the shared
cleanup.log file (same path cli.py's `_setup()` always uses) rather than
its stdout/stderr, which are discarded (redirected to DEVNULL) so a
subprocess writing to the terminal can't corrupt the TUI's own rendering.

Folder-path entry is a plain Input fed by dragging a folder onto the
terminal window. That paste is NOT always bare text: konsole / GNOME
Terminal wrap a path containing spaces in single quotes
('/mnt/data/library/Excluded Audio'), xterm backslash-escapes each space
(/mnt/data/library/Excluded\ Audio). A plain Input keeps those literally,
and Path() then treats a leading quote as a real path character --
confirmed real: a run launched that way created a directory literally
named `'` inside the repo and used a throwaway checkpoint DB. Both quoting
styles are undone in _clean_dropped_path(), called at the single point
_roots_quiet() reads the two fields.

Known, deliberate limitation: dedup.py's clustering phase and energy.py's
raw-decode phase don't produce a steadily-shrinking DB count during their
most expensive work, so the progress bar can look static there -- the log
pane (tailing cleanup.log) is the honest "is it stuck or working" signal
during those phases.
"""

import re
import subprocess
import sys
import threading
import time
from pathlib import Path

from textual.app import App, ComposeResult
from textual.containers import Horizontal, Vertical, VerticalScroll
from textual.coordinate import Coordinate
from textual.screen import ModalScreen
from textual.widgets import Button, DataTable, Footer, Header, Input, Label, Log, ProgressBar

from . import stages as stages_mod

_STATUS_COL = 1
_REMAINING_COL = 2

_CLEANUP_SCRIPT = Path(__file__).resolve().parent.parent / "cleanup.py"


class ConfirmScreen(ModalScreen[bool]):
    """Generic yes/no confirmation modal -- shared by Reset and Wipe
    BPM/Energy (and any future destructive-ish utility), each supplying its
    own message/button wording rather than each needing its own screen
    subclass just to change two strings."""

    DEFAULT_CSS = """
    ConfirmScreen {
        align: center middle;
    }
    #confirm-dialog {
        width: 60; height: auto; border: thick $warning; padding: 1 2; background: $surface;
    }
    """

    def __init__(self, message: str, confirm_label: str):
        super().__init__()
        self._message = message
        self._confirm_label = confirm_label

    def compose(self) -> ComposeResult:
        with Vertical(id="confirm-dialog"):
            yield Label(self._message)
            with Horizontal():
                yield Button("Cancel", id="cancel")
                yield Button(self._confirm_label, id="confirm", variant="error")

    def on_button_pressed(self, event: Button.Pressed) -> None:
        self.dismiss(event.button.id == "confirm")


class CleanupTUI(App):
    CSS = """
    /* Horizontal defaults to height: 1fr, not auto -- inside a scrolling
       container that collapses to far less than its children's real
       height (confirmed: 3-tall buttons inside a height=1 row), and the
       next widget then draws right over the clipped-off remainder. Every
       plain button/input row in this app needs auto instead. */
    Horizontal { height: auto; }
    Button { height: 1; min-width: 10; border: none; margin-right: 1; }
    #inputs { height: auto; padding: 0 1; }
    #utilities { height: auto; padding: 0 1; border: solid $accent-darken-1; }
    #utilities Horizontal { align: left middle; margin-bottom: 1; }
    #utilities Input { width: 30; height: 1; border: none; margin-right: 1; }
    #progress-row { height: 3; padding: 0 1; }
    #log { height: 20; border: solid $accent; }
    """
    BINDINGS = [("q", "quit", "Quit")]
    TITLE = "Music Library Cleanup"

    def __init__(self):
        super().__init__()
        self._pipeline_running = False
        self._stop_requested = threading.Event()
        self._current_process: subprocess.Popen | None = None
        self._log_offset = 0
        self._stage_start_time: float | None = None
        self._stage_start_remaining: int | None = None
        self._current_stage_key: str | None = None
        self._stage_row_index = {stage.key: i for i, stage in enumerate(stages_mod.STAGES)}

    def compose(self) -> ComposeResult:
        yield Header()
        # Everything else lives inside one scrollable body -- the stage
        # table (now 12 rows) plus the Utilities section together are
        # reliably taller than a default-size terminal, and unlike the
        # per-widget height fixes elsewhere in this file, there's no fixed
        # row count to size a container to here: this needs to just scroll
        # (confirmed via a headless Textual pilot test: without this, the
        # lower Utilities buttons/inputs render completely outside the
        # visible viewport with no on-screen indication anything is missing
        # -- the exact same failure shape the stage-table height bug had).
        with VerticalScroll(id="body"):
            with Vertical(id="inputs"):
                yield Label("Library root (drag a folder onto this window, or paste the path):")
                yield Input(placeholder="/mnt/data/library/Music", id="root_input")
                yield Label("Excluded root:")
                yield Input(placeholder="/mnt/data/library/Excluded Audio", id="excluded_input")
            table = DataTable(id="stage-table", cursor_type="row")
            table.add_columns("Stage", "Status", "Remaining")
            yield table
            with Horizontal(id="button-row"):
                yield Button("Run Full Pipeline", id="run-full", variant="primary")
                yield Button("Run Selected Stage", id="run-selected")
                yield Button("Stop", id="stop", disabled=True)
                yield Button("Reset", id="reset", variant="warning")
            with Vertical(id="utilities"):
                yield Label("Utilities:")
                with Horizontal(id="util-row-simple"):
                    yield Button("Undo Duplicate Moves", id="util-undo")
                    yield Button("Wipe BPM/Energy", id="util-wipe", variant="warning")
                with Horizontal(id="util-row-rename"):
                    yield Button("Rename In Place (preview)", id="util-rename-dry")
                    yield Button("Rename In Place", id="util-rename")
                with Horizontal(id="util-row-analyze"):
                    yield Input(placeholder="File path(s) to re-analyze, space-separated", id="util_analyze_paths")
                    yield Button("Analyze", id="util-analyze")
                with Horizontal(id="util-row-artist-merge"):
                    yield Input(placeholder="From artist", id="util_artist_merge_from")
                    yield Input(placeholder="To artist", id="util_artist_merge_to")
                    yield Button("Artist Merge", id="util-artist-merge")
                with Horizontal(id="util-row-collab-split"):
                    yield Input(placeholder="Full credit string, e.g. 'BABYMETAL, Electric Callboy'", id="util_collab_split_artist")
                    yield Button("Collab Split", id="util-collab-split")
                with Horizontal(id="util-row-lastfm-1"):
                    yield Input(placeholder="From artist", id="util_lastfm_from_artist")
                    yield Input(placeholder="From title", id="util_lastfm_from_title")
                with Horizontal(id="util-row-lastfm-2"):
                    yield Input(placeholder="To artist", id="util_lastfm_to_artist")
                    yield Input(placeholder="To title", id="util_lastfm_to_title")
                    yield Button("Apply Last.fm Correction", id="util-lastfm-apply")
            with Vertical(id="progress-row"):
                yield ProgressBar(id="progress", show_eta=False)
                yield Label("", id="eta-label")
            yield Log(id="log")
        yield Footer()

    def on_mount(self) -> None:
        table = self.query_one("#stage-table", DataTable)
        for stage in stages_mod.STAGES:
            table.add_row(stage.label, "pending", "-")
        # Sized to the actual stage count (header row + one per stage)
        # rather than a hardcoded CSS height -- a fixed number silently
        # clips the last row(s) out of view every time a stage gets added
        # (confirmed real: STAGES grew from ~9 to 12 across two unrelated
        # additions, and manifest -- the last stage -- disappeared below
        # the table's fixed height=12 with no visible sign anything was
        # missing, no scrollbar prominent enough to notice at a glance).
        table.styles.height = len(stages_mod.STAGES) + 1
        self.set_interval(1.0, self._poll_progress)
        self.set_interval(0.5, self._tail_log)

    # --- root path helpers -------------------------------------------------

    @staticmethod
    def _clean_dropped_path(text: str) -> str:
        """Undo the shell quoting a terminal adds when a dragged folder path
        contains spaces (see module docstring): whole-string single/double
        quotes, or xterm-style backslash-escaped shell specials. A bare
        path is returned untouched."""
        text = text.strip()
        if len(text) >= 2 and text[0] == text[-1] and text[0] in ("'", '"'):
            text = text[1:-1]
        else:
            text = re.sub(r"\\([ ()&'\"`$!;\\])", r"\1", text)
        return text.strip()

    def _roots_quiet(self):
        root_text = self._clean_dropped_path(self.query_one("#root_input", Input).value)
        excluded_text = self._clean_dropped_path(self.query_one("#excluded_input", Input).value)
        if not root_text or not excluded_text:
            return None
        return Path(root_text), Path(excluded_text)

    def _roots(self):
        roots = self._roots_quiet()
        if roots is None:
            self.notify("Set both Library root and Excluded root first.", severity="warning")
        return roots

    # --- run state -----------------------------------------------------

    _UTILITY_BUTTON_IDS = (
        "util-undo", "util-wipe", "util-rename-dry", "util-rename", "util-analyze",
        "util-artist-merge", "util-collab-split", "util-lastfm-apply",
    )

    def _set_running(self, running: bool) -> None:
        self._pipeline_running = running
        self.query_one("#run-full", Button).disabled = running
        self.query_one("#run-selected", Button).disabled = running
        self.query_one("#reset", Button).disabled = running
        self.query_one("#stop", Button).disabled = not running
        for button_id in self._UTILITY_BUTTON_IDS:
            self.query_one(f"#{button_id}", Button).disabled = running

    def on_button_pressed(self, event: Button.Pressed) -> None:
        if event.button.id == "run-full":
            self._start_full_pipeline()
        elif event.button.id == "run-selected":
            self._start_selected_stage()
        elif event.button.id == "stop":
            self._stop_requested.set()
            if self._current_process is not None:
                self._current_process.terminate()
                self.notify("Stopping the current stage...")
            else:
                self.notify("Stop requested.")
        elif event.button.id == "reset":
            self._start_reset()
        elif event.button.id == "util-undo":
            self._start_undo()
        elif event.button.id == "util-wipe":
            self._start_wipe_bpm_energy()
        elif event.button.id == "util-rename-dry":
            self._start_utility("rename", ["--dry-run"], "Rename In Place (preview)")
        elif event.button.id == "util-rename":
            self._start_utility("rename", [], "Rename In Place")
        elif event.button.id == "util-analyze":
            self._start_analyze()
        elif event.button.id == "util-artist-merge":
            self._start_artist_merge()
        elif event.button.id == "util-collab-split":
            self._start_collab_split()
        elif event.button.id == "util-lastfm-apply":
            self._start_lastfm_apply()

    def _run_subprocess(self, cli_command: str, extra_args: list[str], excluded_root: Path) -> None:
        """Runs `cleanup.py <cli_command> --root ... --excluded-root ...
        <extra_args>` as a real subprocess (see module docstring for why).
        Raises RuntimeError on a nonzero exit or if Stop terminated it, so
        callers can use the same try/except they'd use for an in-process
        call. Shared by both pipeline stages and the Utilities section --
        same process shape either way."""
        excluded_root.mkdir(parents=True, exist_ok=True)
        process = subprocess.Popen(
            [sys.executable, str(_CLEANUP_SCRIPT), cli_command, *extra_args],
            cwd=str(_CLEANUP_SCRIPT.parent), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        )
        self._current_process = process
        returncode = process.wait()
        self._current_process = None
        if self._stop_requested.is_set():
            raise RuntimeError(f"'{cli_command}' stopped by user")
        if returncode != 0:
            raise RuntimeError(f"'{cli_command}' exited with status {returncode} -- see cleanup.log for detail")

    def _run_stage_subprocess(self, stage: stages_mod.Stage, library_root: Path, excluded_root: Path) -> None:
        self._run_subprocess(
            stage.cli_command, ["--root", str(library_root), "--excluded-root", str(excluded_root)], excluded_root,
        )

    def _start_full_pipeline(self) -> None:
        roots = self._roots()
        if not roots or self._pipeline_running:
            return
        library_root, excluded_root = roots
        self._stop_requested.clear()
        self._set_running(True)

        def worker():
            for stage in stages_mod.STAGES:
                if self._stop_requested.is_set():
                    self.call_from_thread(self.notify, "Stopped.")
                    break
                self._begin_stage_tracking(stage.key, excluded_root)
                try:
                    self._run_stage_subprocess(stage, library_root, excluded_root)
                    self.call_from_thread(self._mark_stage_status, stage.key, "done")
                except Exception as exc:  # noqa: BLE001 -- surface to the UI, don't crash the app
                    self._log_stage_exception(stage, exc)
                    self.call_from_thread(self._mark_stage_status, stage.key, "error" if not self._stop_requested.is_set() else "stopped")
                    if not self._stop_requested.is_set():
                        self.call_from_thread(self.notify, f"{stage.label} failed: {exc}", severity="error")
                    break
            self.call_from_thread(self._set_running, False)

        self.run_worker(worker, thread=True, exclusive=True, group="pipeline")

    def _start_selected_stage(self) -> None:
        roots = self._roots()
        if not roots or self._pipeline_running:
            return
        table = self.query_one("#stage-table", DataTable)
        if table.cursor_row is None:
            self.notify("Select a stage row first.", severity="warning")
            return
        stage = stages_mod.STAGES[table.cursor_row]
        library_root, excluded_root = roots
        self._stop_requested.clear()
        self._set_running(True)

        def worker():
            self._begin_stage_tracking(stage.key, excluded_root)
            try:
                self._run_stage_subprocess(stage, library_root, excluded_root)
                self.call_from_thread(self._mark_stage_status, stage.key, "done")
            except Exception as exc:  # noqa: BLE001
                self._log_stage_exception(stage, exc)
                self.call_from_thread(self._mark_stage_status, stage.key, "error" if not self._stop_requested.is_set() else "stopped")
                if not self._stop_requested.is_set():
                    self.call_from_thread(self.notify, f"{stage.label} failed: {exc}", severity="error")
            self.call_from_thread(self._set_running, False)

        self.run_worker(worker, thread=True, exclusive=True, group="pipeline")

    @staticmethod
    def _log_stage_exception(stage: stages_mod.Stage, exc: Exception) -> None:
        """A stage failure used to only ever surface as an ephemeral toast
        notification (self.notify()) -- gone the moment it auto-dismissed,
        with no record anywhere. Every failure now gets logged too, via the
        same logger every stage's own per-file progress lines already go
        through. The subprocess's own stdout/stderr are discarded (see
        _run_stage_subprocess), so its full traceback lives in cleanup.log
        (written by the subprocess itself via its own logger); this line is
        just the TUI-side marker of *which* stage failed and why the
        subprocess was considered to have failed."""
        from . import log as log_module

        logger = log_module.get()
        logger.error(f"[TUI] {stage.label} failed: {exc}")

    # --- progress tracking -----------------------------------------------

    def _mark_stage_status(self, stage_key: str, status: str) -> None:
        table = self.query_one("#stage-table", DataTable)
        row = self._stage_row_index[stage_key]
        table.update_cell_at(Coordinate(row, _STATUS_COL), status)

    def _begin_stage_tracking(self, stage_key: str, excluded_root: Path) -> None:
        self._current_stage_key = stage_key
        self._stage_start_time = time.monotonic()
        stage = stages_mod.STAGES_BY_KEY[stage_key]
        # progress_remaining_count, not remaining_count: the "Remaining"
        # column/ETA are a live-progress display, not a gating check -- for
        # energy specifically these two differ (see Stage.progress_where's
        # own docstring), and start/current must use the same metric or
        # "processed" comes out wrong.
        self._stage_start_remaining = stages_mod.progress_remaining_count(stage, excluded_root)
        self.call_from_thread(self._mark_stage_status, stage_key, "running")

    def _poll_progress(self) -> None:
        if not self._current_stage_key:
            return
        roots = self._roots_quiet()
        if not roots:
            return
        _, excluded_root = roots
        stage = stages_mod.STAGES_BY_KEY[self._current_stage_key]
        try:
            remaining = stages_mod.progress_remaining_count(stage, excluded_root)
        except Exception:  # noqa: BLE001 -- polling is best-effort, never worth crashing the app over
            return
        table = self.query_one("#stage-table", DataTable)
        row = self._stage_row_index[stage.key]
        table.update_cell_at(Coordinate(row, _REMAINING_COL), str(remaining) if remaining is not None else "n/a")

        progress = self.query_one("#progress", ProgressBar)
        eta_label = self.query_one("#eta-label", Label)
        if remaining is None or not self._stage_start_remaining:
            progress.update(total=None)
            eta_label.update("Progress not row-tracked for this phase -- watch the log below.")
            return

        processed = self._stage_start_remaining - remaining
        elapsed = time.monotonic() - self._stage_start_time
        progress.update(total=self._stage_start_remaining, progress=processed)
        if elapsed > 2 and processed > 0:
            rate = processed / elapsed
            eta_seconds = remaining / rate if rate > 0 else None
            eta_label.update(f"{processed}/{self._stage_start_remaining} done -- ETA {self._format_eta(eta_seconds)}")
        else:
            eta_label.update(f"{processed}/{self._stage_start_remaining} done -- estimating ETA...")

    @staticmethod
    def _format_eta(seconds: float | None) -> str:
        if seconds is None:
            return "unknown"
        seconds = int(seconds)
        if seconds < 60:
            return f"{seconds}s"
        minutes, seconds = divmod(seconds, 60)
        if minutes < 60:
            return f"{minutes}m{seconds:02d}s"
        hours, minutes = divmod(minutes, 60)
        return f"{hours}h{minutes:02d}m"

    # --- log tailing -------------------------------------------------------

    def _tail_log(self) -> None:
        roots = self._roots_quiet()
        if not roots:
            return
        _, excluded_root = roots
        log_path = excluded_root / "_pipeline" / "cleanup.log"
        if not log_path.exists():
            return
        size = log_path.stat().st_size
        if size < self._log_offset:
            self._log_offset = 0  # log shrank (e.g. after Reset moved it away) -- rebase
        with log_path.open("r") as f:
            f.seek(self._log_offset)
            new_lines = f.read()
            self._log_offset = f.tell()
        if new_lines:
            self.query_one("#log", Log).write(new_lines)

    # --- reset ---------------------------------------------------------

    def _start_reset(self) -> None:
        roots = self._roots()
        if not roots or self._pipeline_running:
            return
        _, excluded_root = roots

        def on_confirm(confirmed: bool | None) -> None:
            if not confirmed:
                return
            from . import checkpoint

            backup_dir = checkpoint.reset_pipeline_state(excluded_root)
            self._log_offset = 0
            self._current_stage_key = None
            table = self.query_one("#stage-table", DataTable)
            for row_index in range(len(stages_mod.STAGES)):
                table.update_cell_at(Coordinate(row_index, _STATUS_COL), "pending")
                table.update_cell_at(Coordinate(row_index, _REMAINING_COL), "-")
            self.notify(f"Pipeline state backed up to {backup_dir}.")

        self.push_screen(
            ConfirmScreen(
                "This clears all scan/dedup/retag/energy/restructure progress\n"
                "tracking (backed up, not deleted). Previously-moved files stay\n"
                "where they are. Continue?",
                "Reset",
            ),
            on_confirm,
        )

    # --- utilities -------------------------------------------------------

    def _start_utility(self, cli_command: str, extra_args: list[str], label: str) -> None:
        """Shared runner for every Utilities-section button: same
        background-worker/subprocess shape _start_selected_stage uses for a
        pipeline stage, just without a stage-table row to track progress
        against (these are one-shot commands, not gated pipeline steps)."""
        roots = self._roots()
        if not roots or self._pipeline_running:
            return
        library_root, excluded_root = roots
        self._stop_requested.clear()
        self._set_running(True)

        def worker():
            try:
                self._run_subprocess(
                    cli_command, ["--root", str(library_root), "--excluded-root", str(excluded_root), *extra_args],
                    excluded_root,
                )
                self.call_from_thread(self.notify, f"{label} finished.")
            except Exception as exc:  # noqa: BLE001 -- surface to the UI, don't crash the app
                if not self._stop_requested.is_set():
                    self.call_from_thread(self.notify, f"{label} failed: {exc}", severity="error")
            self.call_from_thread(self._set_running, False)

        self.run_worker(worker, thread=True, exclusive=True, group="pipeline")

    def _start_undo(self) -> None:
        self._start_utility("undo", [], "Undo")

    def _start_wipe_bpm_energy(self) -> None:
        roots = self._roots()
        if not roots or self._pipeline_running:
            return

        def on_confirm(confirmed: bool | None) -> None:
            if confirmed:
                self._start_utility("wipe-bpm-energy", ["--yes"], "Wipe BPM/Energy")

        self.push_screen(
            ConfirmScreen(
                "This strips BPM/Energy tags from every current survivor's file\n"
                "and clears bpm/energy/energy_raw in the DB. Run the Energy stage\n"
                "afterward to recompute, then Re-embed to push results back into\n"
                "files. Continue?",
                "Wipe BPM/Energy",
            ),
            on_confirm,
        )

    def _start_analyze(self) -> None:
        paths_text = self.query_one("#util_analyze_paths", Input).value.strip()
        if not paths_text:
            self.notify("Enter at least one file path first.", severity="warning")
            return
        self._start_utility("analyze", paths_text.split(), "Analyze")

    def _start_artist_merge(self) -> None:
        from_artist = self.query_one("#util_artist_merge_from", Input).value.strip()
        to_artist = self.query_one("#util_artist_merge_to", Input).value.strip()
        if not from_artist or not to_artist:
            self.notify("Fill in both From artist and To artist first.", severity="warning")
            return
        self._start_utility(
            "artist-merge", ["--from-artist", from_artist, "--to-artist", to_artist], "Artist Merge",
        )

    def _start_collab_split(self) -> None:
        artist = self.query_one("#util_collab_split_artist", Input).value.strip()
        if not artist:
            self.notify("Enter the full credit string first.", severity="warning")
            return
        self._start_utility("collab-split", ["--artist", artist], "Collab Split")

    def _start_lastfm_apply(self) -> None:
        from_artist = self.query_one("#util_lastfm_from_artist", Input).value.strip()
        from_title = self.query_one("#util_lastfm_from_title", Input).value.strip()
        to_artist = self.query_one("#util_lastfm_to_artist", Input).value.strip()
        to_title = self.query_one("#util_lastfm_to_title", Input).value.strip()
        if not all((from_artist, from_title, to_artist, to_title)):
            self.notify("Fill in all four Last.fm correction fields first.", severity="warning")
            return
        self._start_utility(
            "lastfm-apply-correction",
            [
                "--from-artist", from_artist, "--from-title", from_title,
                "--to-artist", to_artist, "--to-title", to_title,
            ],
            "Apply Last.fm Correction",
        )


def run_tui() -> None:
    CleanupTUI().run()
