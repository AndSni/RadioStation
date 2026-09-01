#pragma once

#include "MixEngine.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <functional>

class QTimer;

namespace radio::audio {

class AudioEngine;

// Orchestrates crossfading between deck "A" and deck "B" on top of an
// AudioEngine: automatic, timed crossfade as the active deck nears the end
// of its track, and a manual equal-power fader for live mixing. The two are
// independent — touching the manual fader interrupts any in-progress
// automatic ramp (AudioEngine::setVolume() cancels a deck's active
// GstController binding before applying a literal value), but does not
// itself trigger auto-advance bookkeeping.
//
// When auto-advance is on, this is also what makes the station play
// hands-off: whenever a deck is empty (DeckState::Null) it's refilled from
// the library queue (PlaylistRepository), and a freshly-empty active deck
// is loaded and started immediately rather than waiting for a user click.
class CrossfadeController : public QObject {
    Q_OBJECT

public:
    explicit CrossfadeController(AudioEngine* engine, QObject* parent = nullptr);

    void setAutoAdvanceEnabled(bool enabled);
    void setCrossfadeLeadMs(qint64 leadMs); // how far from the end to start fading
    void setFadeDurationMs(qint64 durationMs);
    void setFadeCurve(RampCurve curve); // shape applied to every subsequent crossfade ramp, auto or manual

    qint64 crossfadeLeadMs() const { return m_crossfadeLeadMs; }
    qint64 fadeDurationMs() const { return m_fadeDurationMs; }
    RampCurve fadeCurve() const { return m_fadeCurve; }

    QString activeDeck() const { return m_activeDeck; }
    QString idleDeck() const { return m_idleDeck; }

    // Manual fader position, 0.0 (fully deck A) .. 1.0 (fully deck B).
    // Equal-power curve: volA = cos(t*pi/2), volB = sin(t*pi/2).
    void setManualPosition(double t);

    bool isManualOverride(const QString& deckId) const { return m_manualOverride.value(deckId, false); }

    // True iff requestManualFade() would actually start a fade right now —
    // the exact same guardrails onWatchTick()'s own lead-time trigger
    // checks (Auto DJ on, neither deck manually overridden, the idle deck
    // is actually cued) plus "not already mid-fade". The UI should gate its
    // "Fade Now" button on this rather than letting a click silently no-op.
    bool canRequestManualFade() const;

    // If set, consulted every watch tick right before the lead-time-based
    // trigger would call startAutoCrossfade(); returning true suppresses it
    // for that tick, leaving the active deck to keep playing toward its own
    // genuine EOS instead of an early ramped crossfade. Re-evaluated every
    // tick, not a one-shot latch — see autoCrossfadeSuppressed() for what
    // happens once the active deck actually reaches that genuine EOS.
    // CartAutomationEngine's "insert" cart-playback-type is the only current
    // user of this hook; CrossfadeController itself has no notion of carts
    // or schedule blocks — it just knows how to hold off and hand off.
    void setAutoCrossfadeSuppressor(std::function<bool()> suppressor);

    // Finishes a transition that autoCrossfadeSuppressed() announced:
    // starts the idle deck at full volume (no ramp — the two never blended)
    // and performs the same active/idle swap + old-deck-unload bookkeeping
    // the ramped path's onCrossfadeFinished() does. Call once whatever
    // caused the suppression (e.g. a cart) has finished.
    void completeSuppressedCrossfade();

    // Discards whatever's cued on the idle deck, if anything, and if it
    // isn't under manual override — used when the content that should play
    // next has categorically changed out from under an already-cued track
    // (e.g. a schedule block boundary, see BlockTransitionController), not
    // during normal playback. Safe no-op if the idle deck is already empty
    // or manually overridden.
    void discardIdleCue();

public slots:
    // Called whenever a human directly touches a deck's controls (load,
    // play, pause, stop, seek). Marks that deck as under manual control:
    // onWatchTick() will no longer auto-load/auto-play it, and it's
    // excluded from starting a new auto-crossfade (which touches both
    // decks at once, so either deck being overridden blocks it). Sticky
    // until resumeAutoAdvance() is called or the deck reaches genuine EOS
    // (see onDeckEos) — a timeout-based auto-resume would just reintroduce
    // "the app did something I didn't ask for" from the other direction.
    void notifyManualAction(const QString& deckId);
    void resumeAutoAdvance(const QString& deckId);

    // "Skip this song but keep Auto DJ running": starts the crossfade to
    // the idle deck immediately instead of waiting for the automatic
    // lead-time trigger. The idle deck is already cued (same as any other
    // transition), so once the fade completes, the just-vacated deck
    // refills from the queue exactly like any other auto-crossfade — no
    // special-casing needed, onWatchTick()'s existing idle-refill logic
    // just picks it up on its next tick. A no-op (logged, not asserted) if
    // canRequestManualFade() is false at call time — the UI is expected to
    // keep the button disabled instead of relying on this to reject.
    void requestManualFade();

signals:
    void autoCrossfadeStarted(const QString& fromDeck, const QString& toDeck);
    void autoCrossfadeFinished(const QString& newActiveDeck);
    void queueConsumed(); // a queue item was pulled onto a deck — UI should refresh
    void manualOverrideChanged(const QString& deckId, bool manual); // only on actual transition
    // Auto-DJ loaded a track — UI should reflect it. trackId/playlistItemId
    // are the underlying tracks.id and the queue entry (playlist_items.id)
    // it was pulled from, so a UI-layer consumer can look up further detail
    // (a star rating to display, say) or carry per-entry display state (e.g.
    // a pill color assigned when the track entered the queue) forward onto
    // the deck. CrossfadeController itself has no notion of what those ids
    // are used for — it just threads them through, same as title/artist.
    void trackLoaded(
        const QString& deckId, const QString& title, const QString& artist, qint64 trackId, qint64 playlistItemId);

    // Fires exactly once, at the moment control fully hands back to Auto DJ
    // (both decks simultaneously non-overridden) — not on every individual
    // per-deck resume when the other deck was already hands-off. Whatever
    // was queued before/during the manual interruption may no longer match
    // the currently-active schedule block, so this is the cue to discard it
    // and refill fresh (see AutoDjEngine::resetQueue()).
    void handsOffResumed();

    // Fires when the active deck reaches genuine EOS while
    // setAutoCrossfadeSuppressor()'s predicate is (still) returning true —
    // the normal ramped crossfade never started, on purpose, so nothing is
    // currently audibly transitioning. toDeck is already cued (loaded,
    // paused) and ready; call completeSuppressedCrossfade() once ready to
    // actually start it (e.g. once a cart finishes playing).
    void autoCrossfadeSuppressed(const QString& fromDeck, const QString& toDeck);

    // Fires only when canRequestManualFade()'s value actually changes
    // (checked once per watch tick, not emitted every tick) — the UI should
    // enable/disable its "Fade Now" button from this rather than polling.
    void manualFadeAvailabilityChanged(bool available);

private slots:
    void onWatchTick();
    void onCrossfadeFinished();
    void onDeckEos(const QString& deckId);
    // Reacts to a deck failing to load/play (e.g. a queued file deleted from
    // disk after being enqueued, or a corrupt/unsupported file that only
    // fails once GStreamer actually tries to decode it — the QFileInfo
    // check in maybeLoadNextFromQueue() only catches the "gone before we
    // even tried" case). Ignores errors whose source isn't one of our own
    // deck ids (e.g. "stream-sink"). Unloads the failed deck so it reports
    // DeckState::Null again, letting onWatchTick()'s idle-refill logic pick
    // up the next queue item on its next tick instead of stalling.
    void onPipelineError(const QString& source, const QString& message);

private:
    void startAutoCrossfade();
    // Recomputes canRequestManualFade() and emits manualFadeAvailabilityChanged()
    // if it changed since last checked. Called once at the top of every
    // onWatchTick(), unconditionally — onWatchTick() has several early
    // returns below this that must not skip it, or the UI could get stuck
    // with a stale enabled/disabled "Fade Now" button.
    void updateManualFadeAvailability();
    // pauseAfterLoad=true cues the deck silently (idle-deck case). false
    // leaves it exactly where the load lands it, for callers that are about
    // to immediately call play() themselves — issuing pause() then play()
    // back-to-back is not just redundant, it's genuinely racy (observed:
    // deck occasionally never reaches PLAYING at all).
    // Returns true iff a track was actually pulled from the queue and
    // loaded. AudioEngine::loadTrack() dispatches to the engine thread
    // asynchronously (fire-and-forget, no wait for completion) — callers
    // MUST branch on this return value rather than re-querying
    // AudioEngine::state() right after calling this, since that reads
    // whatever the engine thread has processed so far, which races the
    // just-queued load and can still read Null.
    bool maybeLoadNextFromQueue(const QString& deckId, bool pauseAfterLoad);

    // Shared tail of both transition paths: unload the just-finished deck,
    // swap active/idle, clear m_crossfading, emit autoCrossfadeFinished().
    // onCrossfadeFinished() (ramped path) and completeSuppressedCrossfade()
    // (hard-cut path) differ only in what happens *before* this — the
    // ramped path already has the idle deck playing at full volume by the
    // time its finalize timer fires, the hard-cut path has to start it here.
    void finishTransition();

    AudioEngine* m_engine;
    QTimer* m_watchTimer;
    QTimer* m_finalizeTimer;

    QString m_activeDeck = QStringLiteral("A");
    QString m_idleDeck = QStringLiteral("B");
    QHash<QString, qint64> m_deckTrackId; // deckId -> track id currently loaded, for play-history recording
    QHash<QString, bool> m_manualOverride; // deckId -> under manual control, see notifyManualAction()

    bool m_autoAdvanceEnabled = true;
    bool m_crossfading = false;
    qint64 m_crossfadeLeadMs = 5000;
    qint64 m_fadeDurationMs = 4000;
    // Equal-power (constant perceived loudness across the transition) is
    // the professional-standard default for an automated crossfade —
    // Linear/SlowDown/SpeedUp stay selectable for anyone who wants a
    // deliberate asymmetric dip (e.g. a hard-cut-style transition).
    RampCurve m_fadeCurve = RampCurve::EqualPower;
    bool m_lastManualFadeAvailable = false; // last value emitted via manualFadeAvailabilityChanged()

    std::function<bool()> m_autoCrossfadeSuppressor;
};

} // namespace radio::audio
