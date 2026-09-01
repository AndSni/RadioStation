# Console UI — bringing the rest of the app up to the Mixer's look

*Status: steps A–C3 implemented (Mixer fader/knob rework, `ConsoleTheme`,
`ConsoleButton` + `DotMatrixDisplay`, the Deck and Crossfader reworks).
C4–C6 (the app‑wide Fusion/palette/chrome pass) are still proposal only —
see the rollout table.*

## Why

The Mixer panel now looks like a real hardware channel strip: recessed
faders with knurled caps and an etched scale (`ConsoleFader`), gradient
rotary knobs (`RotaryKnob`), LED‑ladder meters (`LedMeterPainter`), glowing
LCD readouts (`LcdReadout`), round LED‑lit switch caps
(`RoundButton` / `IndicatorButton`), and a metal bezel around each strip
(`ConsoleTheme::consoleBezelStyle()`).

Every *other* surface — the Decks, the Crossfader, Library / Queue / Cart
Wall / Playlists / Clocks docks, every dialog, the menus, the tables — is
still stock native Qt. Opening the app, the Mixer looks like a different
program bolted onto a generic one. This proposal takes the console look
app‑wide, and reworks the two most‑used surfaces (Decks, Crossfader) into
proper DJ‑console controls in the process.

Scale of the styling job is small: only **three** `QSlider`s exist in the
whole app (Mixer EQ/faders, `DeckWidget` seek bar, `CrossfaderWidget`
crossfade), and ~18 files construct `QPushButton`s.

## Foundation (already in place)

`src/ui/ConsoleTheme.h/.cpp` is now the single source of the palette and the
shared cap painter (`paintConsoleCap()`), replacing the RGB triples that were
copy‑pasted into `RoundButton`, `RotaryKnob`, `IndicatorButton`,
`LcdReadout`, `LedMeterPainter`, `MixerSliderHelpers` and `MixerPanelWidget`.
Everything below builds on it — no new colour constants anywhere else.

## The plan

### 1. Pin a predictable base style

*Partly done:* `main.cpp` now calls
`qApp->setStyleSheet(theme::appStyleSheet())` — a small global QSS that
gives every ordinary (`:!flat`, so not the hand‑painted
`ConsoleButton`/`RoundButton`/`IndicatorButton`) `QPushButton` the warm
console cap look, plus a matching `QToolBar` / `QStatusBar` tone. Fusion +
the full `QPalette` + scrollbar/header/tab/menu chrome below are still
pending.

`RoundButton.h` documents why this codebase avoids QSS: it sets no
`QApplication::setStyle()`, so it renders under whatever native platform
style is present, and a styled corner radius behaves differently under each.
The fix is to stop leaving that to chance:

- In `src/app/main.cpp`, `QApplication::setStyle(QStringLiteral("Fusion"))`.
  Fusion is the same renderer CI already falls back to (offscreen), so this
  aligns local and CI rendering and removes the reason the no‑QSS rule
  exists. Hand‑painted primitives keep painting; QSS becomes safe for the
  flat chrome.
- A warm‑dark `QPalette` built from `ConsoleTheme` colours
  (`Window`/`Base` ≈ `kBezelBottom`, `Text`/`WindowText` ≈ `kCapText`,
  `Highlight` ≈ a dimmed `kLedRed`, `Button` ≈ `kCapMid`). Tables, trees,
  dialogs, labels and spin boxes inherit it for free. **One** window
  background tone everywhere — see §3.
- One small global `qApp->setStyleSheet(...)` (kept in `ConsoleTheme`) for
  the chrome the palette alone doesn't sell: `QScrollBar` (thin, dark,
  rounded handle), `QHeaderView::section` (bezel‑toned), `QTabBar::tab`,
  `QMenu` / `QMenuBar`, `QToolTip`, `QGroupBox` (reuse
  `consoleBezelStyle()`), and `QDockWidget::title`.

### 2. Promote the painted controls app‑wide

- **`ConsoleFader`** (built orientation‑aware for exactly this) replaces the
  `QSlider` in `DeckWidget` (seek bar — horizontal) and `CrossfaderWidget`
  (crossfade — horizontal). The crossfader wants `setCenterDetent(true)` and
  a wider cap; the seek bar wants a slim cap and no detent.
- **`ConsoleButton : QPushButton`** — new. A rectangular metal cap drawn
  with the shared `paintConsoleCap()` plus a light bar that lights when the
  button is `:checked` / `setLit()` / `setBlinking()`, in a per‑button
  `setAccent()` colour. Off = a *dark filament* of the accent (so a lit LED
  reads as a real change); blinking pulses the bar 1.0↔0.5 (never dark) with
  a harder glow pulse; a disabled button gets a dark veil over the *cap
  only*, so a disabled‑but‑blinking Fade Now still shows a bright pulse.
  Used for the Deck transport (§5), `CrossfaderWidget`'s curve buttons +
  Fade Now, the Controls‑bar Auto‑DJ toggle + PANIC (§6).
  `RoundButton` / `IndicatorButton` stay for the Mixer's unlabeled switch
  caps.
- **`AudioPillPainter::kNeutralColor`** changed from the cool `#5b6472` to a
  warm `#4a443b` so Library / Queue / Playlist pills match the bezel.
- **`LcdReadout`** for the numeric/time displays that are currently plain
  `QLabel`s: `DeckWidget` elapsed / remaining time and bitrate, the queue
  length, stream status counters. `RadioStatisticsPanel`'s clock already
  uses the same LCD font.
- **`paintLedMeterBar()`** for any other level or progress indicator that
  turns up.

### 3. One chassis, one background

Wrap every top‑level dock's content `QGroupBox` in `consoleBezelStyle()` so
the whole main window reads as one console face — the docks already exist and
are enumerated in `MainWindow.cpp` (`decksDock`, `crossfaderDock`,
`autoDjDock`, `radioStatisticsDock`, `libraryDock`, `playlistsDock`,
`smartPlaylistDock`, `clocksDock`, `queueDock`, `cartDock`, `mixerDock`).
Give `QMainWindow` itself the warm‑dark background and dark dock separators.

*Done so far:* the Mixer strips, the Crossfader, and now the **Deck** strips
(`m_box` → `consoleBezelStyle()`) and the **Radio Statistics** panel (a
`QFrame` → `consolePanelStyle()`, since it isn't a `QGroupBox`) all carry
the warm metal bezel. The Radio Statistics **clock** now sits on a dark
"LCD screen" `QFrame` (`lcdScreenFrameStyle()` — `kLcdScreen` fill,
`kLcdRim` border) like every `LcdReadout` / `DotMatrixDisplay`; the clock
label itself stays transparent so its drop‑shadow glow keeps hugging the
digits.

**Docks do not change colour with content.** Remove
`DeckWidget::applyBackgroundTint()` and its `m_baseGroupBoxColor` /
`applyLoadedTrackDisplay(... pillColor)` background path entirely — the
per‑track pill/chip colour now lives *only* in the deck's track display
(§7), not bled 10 % into the panel background. Every dock sits on the same
unified bezel background regardless of what's loaded.

### 4. Typography

One condensed, uppercase, letter‑spaced caption style (an extension of the
existing scale‑label style in `ConsoleTheme::scaleLabelStyle()`) for panel
titles and control captions, so silkscreen‑style labels are consistent.

One embedded font: `lcd_att_phone_time_date.ttf` (7‑segment,
`lcdFontFamily()`) — clocks, the Mixer's numeric readouts, and the deck's
**Duration** `LcdReadout`. The deck's dot‑matrix display (§7) uses **no
font at all** — it renders a real 5×7 character ROM.

### 5. Decks — full rework

`DeckWidget` today is a stack of native widgets: a `QSlider` seek bar, four
text `QPushButton`s (`Load…` / `Play` / `Pause` / `Stop`), a `State: …`
`QLabel`, an `AUTO DJ` / `MANUAL` override badge, a `Resume Auto‑DJ` button,
and a plain info line. Target: a DJ‑deck panel.

**Transport — DJ‑console buttons (`ConsoleButton`):**

- **PLAY / PAUSE** becomes **one** toggling button. Its state is shown *in
  the button itself*: lit (engraved LED on, brighter cap) = playing,
  unlit = paused/stopped. No separate `State:` label — delete `m_stateLabel`
  and the `State: …` text (`onDeckStateChanged` just drives the button's lit
  state and enable; `onDeckEos` unlits it). Error text moves to a small
  transient line or a tooltip on the display.
- **CUE** — new. A momentary button that **blinks** (reuse
  `CrossfaderWidget`'s existing `QTimer` blink pattern, ~400 ms) whenever a
  track is loaded but has never been played — i.e. prerolled/paused at 0.
  Solid‑unlit once playback has started; pressing CUE returns the deck to
  the cue point (0 for now) and re‑arms the blink. This is the "song loaded,
  not yet played" signal DJs expect.
- **STOP** stays, as a `ConsoleButton`.
- **No LOAD button.** Drag‑and‑drop from Library/Queue/Playlists already
  covers loading (`DeckWidget::dropEvent`). Delete `m_loadButton`,
  `onLoadClicked()`, and the `QFileDialog` include.

**Remove all Auto‑DJ surface from the deck.** Delete `m_overrideBadge`,
`m_resumeAutoButton`, `onResumeAutoClicked()`, `onManualOverrideChanged()`,
the `resumeAutoRequested` signal, and the `overrideRow`. The manual/auto
override still exists in `CrossfadeController`; it just isn't shown or
driven from the deck any more. `MainWindow` currently wires
`DeckWidget::manualActionTaken` / `resumeAutoRequested` /
`onManualOverrideChanged` to the controller — drop those connections for the
deck (the Mixer/toolbar remain the place Auto‑DJ is controlled and shown,
see §6). `onTrackLoaded()` (the Auto‑DJ queue‑pull display update) stays —
it's just "a track landed on this deck", not an Auto‑DJ control.

**Seek bar** → horizontal `ConsoleFader`, slim cap, no detent. Same
`sliderReleased` → `m_engine->seek()` wiring.

**Track display** → the dot‑matrix component in §7, replacing the
`AudioPillWidget` title line.

**Tags line** — extend `applyTrackInfoDisplay()` to also show **genre** and
**release year**. Both are already on `TrackRecord` (`genre`, `year` — 0 =
unknown, `src/db/TrackRecord.h`) and already populated by
`TrackRepository::trackById()`, so this is display‑only. Rendered as the
display's secondary line (§7) or a `scaleLabelStyle()` caption strip beneath
it: `GENRE · YEAR · 320 kbps · BPM 128 · −7.3 dB`.

**Star rating** stays; restyle to the console palette with the rest.

Tests: `DeckWidgetTest` and `MainWindowTest` reference `stateLabel`,
`overrideBadge`, the Load/Play/Pause/Stop buttons and `resumeAutoRequested`
by `objectName` / signal — they need updating alongside this. Keep
`titleLabel` as the stable handle for the new display widget.

### 6. Crossfader

Layout is a vertical stack, padded from the bezel edges: the full‑width
`ConsoleFader` (`setCenterDetent(true)`, centre = both decks equal), then an
`LedIndicator` under each end, then the `A` / `B` caption under each LED,
then **one** controls row.

- That row is **centred** (stretch either side) and holds `Fade Now`, a
  bordered `QGroupBox("Fade Curve")` around the four ramp‑curve buttons, and
  the Auto‑advance toggle + caption. Fade Now and the auto‑advance controls
  are `Qt::AlignBottom` so every button sits on the same baseline as the
  ones inside the group box, despite the box's taller title area.
- Curve buttons + Fade Now → `ConsoleButton`; exclusive `QButtonGroup`
  unchanged. Fade Now's blink is `setBlinking()` — its bar stays fully lit,
  only the glow pulses, matching a selected curve button's brightness —
  rather than a stylesheet swap.
- **Auto‑advance** → a checkable `ConsoleButton` cap (unlabelled) plus a
  separate `Auto‑advance` caption, replacing the plain `QCheckBox`.
- A / B are plain silkscreen captions; the active deck shows on a new
  standalone **`LedIndicator`** widget (the `IndicatorButton` LED dot +
  glow, factored out; carries a real `minimumSize` so a tight dock can't
  collapse it) above each — `kLedRed` lit, a visible dark‑glass dome when
  off.
- The Auto‑DJ status line is gone from this widget — it lives on the
  **Controls toolbar** in `MainWindow`, reworked as: `[Auto DJ toggle]`
  `[PANIC]` `[status text]`. Both buttons are `ConsoleButton`s lit in their
  own colour (green = Auto DJ running, red = PANIC armed). The status text
  is one rich string, rebuilt on a 1s timer and immediately on any input
  change (`refreshStationStatus()`): `AUTO DJ: hands-off` /
  `AUTO DJ: MANUAL (A)` · `Block: <name> (<mm:ss> left)` ·
  `Now Playing: <artist> - <title> (<album>, <year>) <genre>, <kbps>k` ·
  `Streaming: <state>`. "Now Playing" is fed by a new
  `DeckWidget::trackDisplayed` signal (every load path) cached per deck.

### 7. Dot‑matrix track display (shared component)

New `DotMatrixDisplay` widget (hand‑painted, house style) — a **real
character‑LCD**, modelled on the Adafruit 399 RGB‑negative 16×2 module. It
uses **no display font**: every printable ASCII glyph is a fixed **5×7
bitmap** from the classic `glcdfont` character ROM (`kFont5x7`, the same
table a real HD44780 ships — an earlier attempt at sampling an arbitrary
TTF into 5×7 looked mushy, and Nokian / DisplayOTF pixel fonts still don't
give the fixed‑cell, visible‑unlit‑dot look). The two lines are drawn dot
by dot onto a fixed grid; every dot — lit or not — is a little square, so
the whole thing reads as an LED matrix. Overflowing text scrolls through
the grid a whole dot‑column at a time.

- **Colour matched to the module**: deep maroon‑black screen that glows
  brighter toward the centre (`kScreen` + a radial bloom), intense
  orange‑red lit dots (`kDotOn`) each with a 1px halo, and a dim‑but‑visible
  unlit‑dot grid (`dotOff()`). Plus an inner vignette. (The per‑track
  "chip" colour idea is gone — `QueueColorRegistry` and
  `AudioPillPainter::randomColor()` removed; every pill is `kNeutralColor`.)
- **Two lines**: line 1 `Artist - Title` (marquee when it overflows), line 2
  the *technical* strip (`320k / 128 BPM / -8.1 dB`) — descriptive fields
  moved to the metadata block. Non‑ASCII (em‑dash, `·`, curly quotes, `…`)
  is folded to ASCII in `sanitize()` before rendering.
- **Two "dot size" metrics** (px per grid dot, one per line) in QSettings
  (`station_settings::kDeckLine1FontPx` / `kDeckLine2FontPx` — key names kept
  for back‑compat), editable from **Station Settings → Deck Display**,
  applied live via `reloadMetricsFromSettings()`.
- **Colour by state** (`kDeckDisplayColourByState`, default on, same tab): a
  deck's whole panel glows **red while playing / played, yellow while cued**
  (loaded, not yet started) — `DotMatrixDisplay::setLampColor()` re‑derives
  screen / dots / bloom from one lamp colour; `DeckWidget::refreshCueBlink()`
  drives it off the same state as the CUE blink.

**Metadata block** (§5) — under the Duration readout, small muted label
font, two columns. Left column is **`FadingLabel`s** — `Artist - …` / `Title - …` /
`Album - …, <year>` / `Genre - …` — a capped‑width label that paints its
overflow fading out under the right column instead of stretching the deck or
showing an ellipsis. Right: `Rating:` + the still‑interactive
`StarRatingWidget`, and `Play count: <n>`. Filled by
`applyTrackInfoDisplay()` from the `TrackRecord`.

## Rollout order & risk

| Step | Change | Risk |
|------|--------|------|
| A ✅ | `ConsoleTheme` extraction | none — pure refactor, tests unchanged |
| B ✅ | `ConsoleFader` + `RotaryKnob` polish in the Mixer | contained to one panel |
| C1 ✅ | `ConsoleButton`, `LedIndicator`, `DotMatrixDisplay` (procedural, area‑averaged dot grid; QSettings‑tunable via Station Settings → Deck Display — see §7) primitives | new isolated widgets with their own tests (`rs_consolebutton_tests`, `rs_dotmatrixdisplay_tests`) |
| C2 ✅ | **Deck rework** (§5): single lit Play/Pause, blinking CUE, no Load button, all Auto‑DJ surface removed, horizontal `ConsoleFader` seek, `DotMatrixDisplay` (chip‑tinted, genre/year tags) replacing the pill title, `LcdReadout` "Duration" replacing the time label, `applyBackgroundTint()` gone | `DeckWidgetTest` + `AutoDjIntegrationTest` retargeted to the new surfaces |
| C3 ✅ | **Crossfader** (§6): stacked layout (fader / LEDs / A‑B captions / controls row, edge‑padded), centre‑detent `ConsoleFader`, `Fade Curve` group box, `ConsoleButton` curve/Fade‑Now/Auto‑advance, `LedIndicator` active‑deck lights, Auto‑DJ status moved to a new always‑visible `autoDjStatusLabel` on the Controls toolbar | `CrossfaderWidgetTest` deck‑label / blink asserts updated; `AutoDjIntegrationTest` now checks the toolbar status |
| C4 | Fusion + `QPalette` + global chrome QSS in `main.cpp` (§1) | **highest** — every screen at once; visual‑review checkpoint + table/tree readability pass |
| C5 | Dock bezels + one unified `QMainWindow` background (§3) | low |
| C6 | Audit the ~15 per‑widget `setStyleSheet("color: #…")` call sites for cascade interaction with the global sheet; migrate ad‑hoc colours to `ConsoleTheme` | low, tedious |

Do C4 on its own branch/commit so it can be reverted independently if the
dark palette turns up a readability problem in a screen this proposal didn't
anticipate. Confirm the offscreen CI renderer still matches after each step
(the UI tests already run with `QT_QPA_PLATFORM=offscreen`).

## Out of scope here

New panels, per‑screen layout work beyond the Deck and Crossfader reworks
above, and any audio‑engine behavioural change. The CUE point is 0 for now
(no waveform/cue‑marker UI in this pass).
