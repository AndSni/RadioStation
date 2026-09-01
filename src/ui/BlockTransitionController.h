#pragma once

#include <QDateTime>
#include <QObject>

class QTimer;

namespace radio::audio {
class CrossfadeController;
}

namespace radio::scheduler {
class AutoDjEngine;
class ClockEngine;
}

namespace radio::ui {

// Forces a clean, radio-style cutover at schedule block boundaries. Left to
// itself, AutoDjEngine only tops up the queue on a low-watermark basis and
// CrossfadeController only crossfades based on how much of the current
// track is left — neither has any notion of a block's own end time, so a
// queue built while one block was active could otherwise keep playing out
// well past the next block's start with no hard edge.
//
// Every tick (1s, plenty fine against minute-granular block boundaries):
// shortly before the active block's end (see setLeadSeconds()), pre-stages
// the NEXT block's content — discards whatever's cued on the idle deck (see
// CrossfadeController::discardIdleCue()) and refills the queue targeting
// that block specifically (see AutoDjEngine::resetQueueForBlock()) — so the
// idle deck is already loaded with the new block's first track before the
// boundary arrives. The instant the boundary is actually crossed, forces
// the handoff via CrossfadeController::requestManualFade() (the same
// mechanism the Crossfader's own "Fade Now" button uses). Never touches
// either deck while it's under manual override (canRequestManualFade()
// already accounts for this) — the forced fade simply stays pending until
// control is handed back, same as handsOffResumed()'s own philosophy.
//
// Plain QObject, no UI of its own — lives in rs_ui rather than rs_scheduler
// or rs_audio because it depends on both AutoDjEngine and
// CrossfadeController, and neither of those modules depends on the other;
// MainWindow is where that kind of cross-domain wiring already happens
// (see its own handsOffResumed -> resetQueue connection). Constructed and
// owned by MainWindow alongside CartAutomationEngine.
class BlockTransitionController : public QObject {
    Q_OBJECT

public:
    explicit BlockTransitionController(radio::audio::CrossfadeController* crossfadeController,
        radio::scheduler::AutoDjEngine* autoDjEngine, QObject* parent = nullptr);

    // How many seconds before a block's end to pre-stage the next one.
    // Settable (default 5) so tests can force fast, deterministic
    // pre-staging, same as CrossfadeController::setCrossfadeLeadMs().
    void setLeadSeconds(int seconds);

    // Optional (nullptr by default -- with no injection, steady-state
    // clock-driven blocks are simply never checked for a hard cut, same as
    // before ClockEngine existed). Only MainWindow is expected to call
    // this. Not owned.
    void setClockEngine(radio::scheduler::ClockEngine* clockEngine) { m_clockEngine = clockEngine; }

    // Runs one evaluation pass against an explicit `now` instead of the
    // real wall clock — what onTick() calls internally every second.
    // Exposed directly so tests can drive deterministic, fast transitions
    // across a synthetic block boundary without waiting on a real minute to
    // tick over (block boundaries are minute-granular; BlockTimeResolver's
    // own tests use the same "pass an explicit QDateTime" approach for the
    // same reason).
    void evaluateAt(const QDateTime& now);

private slots:
    void onTick();

private:
    radio::audio::CrossfadeController* m_crossfadeController;
    radio::scheduler::AutoDjEngine* m_autoDjEngine;
    radio::scheduler::ClockEngine* m_clockEngine = nullptr;
    QTimer* m_timer;

    int m_leadSeconds = 5;

    // Sentinel: no tick has run yet, so there's nothing to compare against
    // — the first tick just observes and returns, rather than treating
    // "startup" itself as a transition.
    static constexpr qint64 kUninitialized = -3;
    qint64 m_lastKnownActiveBlockId = kUninitialized;

    bool m_preStaged = false;
    qint64 m_preStagedForBlockId = kUninitialized;
    bool m_fadePending = false;

    // Guards against the DST fall-back double-fire: BlockTimeResolver
    // resolves purely from local wall-clock time-of-day, so during the
    // repeated local hour the same (from, to) block-id transition can be
    // detected twice in one calendar day even though real elapsed time
    // only crossed it once. Tracks the last transition actually FIRED (not
    // just observed) so a repeat of the identical pair within a real
    // elapsed window comfortably covering any DST offset can be recognized
    // as the artifact and silently absorbed instead of re-running
    // discardIdleCue()/resetQueueForBlock()/a forced fade.
    static constexpr qint64 kNoFiredTransition = -3;
    qint64 m_lastFiredTransitionEpochSecs = -1;
    qint64 m_lastFiredFromId = kNoFiredTransition;
    qint64 m_lastFiredToId = kNoFiredTransition;

    // Dedup for ClockEngine::dueHardCutElementId(): the wheel does not
    // advance past a hard-timed element just because a fade was requested
    // (see that method's doc comment), so without this, the same due
    // element would keep re-requesting a fade every tick until the wheel
    // actually catches up. Reset to -1 whenever the active block itself
    // changes, so a later hour's element reusing a low id can't be
    // mistaken for "already handled".
    qint64 m_lastFiredHardCutElementId = -1;
};

} // namespace radio::ui
