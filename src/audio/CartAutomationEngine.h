#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

namespace radio::audio {

class AudioEngine;
class CrossfadeController;

}

namespace radio::scheduler {
class ClockEngine;
}

namespace radio::audio {

// Fires a Cart Wall clip every Nth song within the currently-active
// schedule block (see BlockTimeResolver in rs_scheduler for "which block is
// active", reused as-is rather than duplicated), timed a configurable
// number of seconds after a deck's automatic crossfade finishes. Two
// playback types, per block:
//  - "overlay": plain parallel playback via AudioEngine::triggerCart(), no
//    ducking/mixing changes (see CartWallEngine's own doc comment:
//    retriggers are meant to overlap, not interrupt anything). Reacts to
//    CrossfadeController::autoCrossfadeFinished — i.e. after the fact.
//  - "insert" (a.k.a. "In Between" in the UI): a genuine hard cut, not a
//    pause. Rather than let the normal ramped crossfade blend the two decks
//    and then pause the newly-active one, this installs an
//    AutoCrossfadeSuppressor on CrossfadeController: when this block's cart
//    is due, the ramp is skipped entirely for that transition, the active
//    deck plays out to its own genuine EOS untouched, and only then does
//    the cart play solo — the idle deck stays cued-but-silent the whole
//    time (CrossfadeController's existing idle-refill behavior already cues
//    it well in advance) and starts at full volume, no ramp, once the cart
//    finishes. See CrossfadeController::setAutoCrossfadeSuppressor()/
//    autoCrossfadeSuppressed()/completeSuppressedCrossfade() for the
//    mechanism this drives.
// Both paths confirm cart completion by polling
// AudioEngine::isCartTokenActive() on a QTimer rather than reacting to a
// pushed completion signal — an earlier version used a signal emitted via
// QMetaObject::invokeMethod(..., Qt::QueuedConnection) from inside
// CartWallEngine's EOS-driven teardown path (itself running on
// GstEngineThread), which reliably corrupted the heap (reproduced
// deterministically; see CartWallEngine::trigger()'s doc comment for what's
// understood about why). Polling from this engine's own thread, the same
// way AudioEngine::activeCartCount() already safely works, sidesteps the
// whole hazard.
// Self-wires to CrossfadeController in its own constructor, same shape as
// CrossfadeController itself. Per-block song counters and Sequence/Color-
// Sequence cursors are in-memory only, same lifetime as AutoDjEngine's own
// m_categoryCredits — reset on app restart, not a new persistence story.
class CartAutomationEngine : public QObject {
    Q_OBJECT

public:
    CartAutomationEngine(AudioEngine* audioEngine, CrossfadeController* crossfadeController, QObject* parent = nullptr);

    // Optional (nullptr by default -- with no injection every block behaves
    // exactly as before clocks existed). Only MainWindow is expected to call
    // this. Not owned. Once set, a block with clockId >= 0 is driven
    // entirely by the clock's own cart elements (see shouldSuppressAutoCrossfade()/
    // onAutoCrossfadeSuppressed()/onAutoCrossfadeFinished()) -- its legacy
    // cartFrequency/cartPlaybackType/cartMode/cartColor columns are ignored.
    void setClockEngine(radio::scheduler::ClockEngine* clockEngine) { m_clockEngine = clockEngine; }

signals:
    // Fired the moment a cart clip is actually triggered by this engine
    // (both "overlay" and "insert" modes) — lets a UI-layer consumer (see
    // CartGridWidget::onExternalCartTriggered()) reflect a schedule-driven
    // fire the same way it already reflects a direct click, e.g. blinking
    // the matching Cart Wall button. This engine has no notion of buttons
    // or widgets — it just identifies which clip and which triggered
    // instance (token), same as CartButton::triggerRequested()'s own shape.
    void cartTriggered(qint64 clipId, const QString& token);

private slots:
    // "overlay" mode only — "insert" mode is handled entirely by the two
    // slots below, since it has to intercept before the ramp even starts
    // rather than react after one finishes. See shouldSuppressAutoCrossfade().
    void onAutoCrossfadeFinished(const QString& newActiveDeck);

    // The CrossfadeController::autoCrossfadeSuppressed() hand-off moment for
    // "insert" mode: the active deck just reached its own EOS with the ramp
    // suppressed. Commits the peeked counter increment, picks a clip, and
    // triggers it (or, if no clip/token is available, completes the
    // transition immediately rather than leaving the station stuck).
    void onAutoCrossfadeSuppressed(const QString& fromDeck, const QString& toDeck);

    void onResumePollTimeout();

private:
    // The predicate handed to CrossfadeController::setAutoCrossfadeSuppressor().
    // While a cutaway is already in flight, unconditionally suppresses
    // (keeps holding off) regardless of counter state, so the ramp can't
    // sneak in during the window between the EOS hand-off and
    // completeSuppressedCrossfade() actually running (a counter that's
    // already been committed/reset by then would otherwise read as "not
    // due" and let a normal ramped crossfade start mid-cart). Otherwise,
    // peeks (without committing) whether the currently-active block is
    // "insert" mode and its cart would become due once the song now playing
    // ends.
    bool shouldSuppressAutoCrossfade() const;

    AudioEngine* m_audioEngine;
    CrossfadeController* m_crossfadeController;
    radio::scheduler::ClockEngine* m_clockEngine = nullptr;
    QTimer* m_resumePollTimer;
    QHash<qint64, int> m_blockCartCounters; // blockId -> songs played since this block's cart last fired
    QHash<qint64, int> m_blockSequenceCursors; // blockId -> CartPicker's cursor for Sequence/Color Sequence modes

    // "insert" cutaway state. At most one cutaway can ever be in flight at
    // once (there is only ever one active deck), so a single pending token
    // rather than a per-deck hash.
    bool m_insertCutawayInFlight = false;
    QString m_pendingCutawayToken; // empty while not waiting on a specific cart instance
};

} // namespace radio::audio
