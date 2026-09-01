#include "CrossfadeController.h"
#include "AudioEngine.h"

#include "core/Logging.h"
#include "db/PlayHistoryRepository.h"
#include "db/PlaylistRepository.h"

#include <QFileInfo>
#include <QTimer>
#include <algorithm>
#include <cmath>

namespace radio::audio {

CrossfadeController::CrossfadeController(AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_watchTimer = new QTimer(this);
    m_watchTimer->setInterval(200);
    connect(m_watchTimer, &QTimer::timeout, this, &CrossfadeController::onWatchTick);
    m_watchTimer->start();

    m_finalizeTimer = new QTimer(this);
    m_finalizeTimer->setSingleShot(true);
    connect(m_finalizeTimer, &QTimer::timeout, this, &CrossfadeController::onCrossfadeFinished);

    connect(m_engine, &AudioEngine::deckEos, this, &CrossfadeController::onDeckEos);
    connect(m_engine, &AudioEngine::pipelineError, this, &CrossfadeController::onPipelineError);
}

void CrossfadeController::setAutoAdvanceEnabled(bool enabled)
{
    m_autoAdvanceEnabled = enabled;
}

void CrossfadeController::setCrossfadeLeadMs(qint64 leadMs)
{
    m_crossfadeLeadMs = leadMs;
}

void CrossfadeController::setFadeDurationMs(qint64 durationMs)
{
    m_fadeDurationMs = durationMs;
}

void CrossfadeController::setFadeCurve(RampCurve curve)
{
    m_fadeCurve = curve;
}

void CrossfadeController::setAutoCrossfadeSuppressor(std::function<bool()> suppressor)
{
    m_autoCrossfadeSuppressor = std::move(suppressor);
}

void CrossfadeController::completeSuppressedCrossfade()
{
    // No ramp on this path (that's the whole point of a suppressed/insert
    // cutaway), but the idle deck's crossfade volume is still wherever its
    // LAST ramp left it -- 0, from fading out the last time it was active.
    // Jump it straight to full volume instead of ramping there.
    m_engine->setVolume(m_idleDeck, 1.0);
    m_engine->play(m_idleDeck);
    if (const qint64 trackId = m_deckTrackId.value(m_idleDeck, -1); trackId >= 0)
        radio::db::PlayHistoryRepository::recordPlay(trackId, m_idleDeck);

    RS_LOG_INFO("audio.crossfade",
        QStringLiteral("Suppressed crossfade completing: starting %1 at full volume (no ramp)").arg(m_idleDeck));
    finishTransition();
}

void CrossfadeController::setManualPosition(double t)
{
    t = std::clamp(t, 0.0, 1.0);
    const double volA = std::cos(t * M_PI / 2.0);
    const double volB = std::sin(t * M_PI / 2.0);
    m_engine->setVolume(QStringLiteral("A"), volA);
    m_engine->setVolume(QStringLiteral("B"), volB);
}

bool CrossfadeController::canRequestManualFade() const
{
    if (m_crossfading || !m_autoAdvanceEnabled)
        return false;
    // A crossfade touches both decks at once — if a human has taken over
    // either one, don't force a fade underneath them (same rule
    // onWatchTick()'s own automatic trigger follows).
    if (isManualOverride(m_activeDeck) || isManualOverride(m_idleDeck))
        return false;
    if (m_engine->state(m_activeDeck) != DeckState::Playing)
        return false;
    // Nothing cued on the other deck — nothing to fade into.
    if (m_engine->state(m_idleDeck) == DeckState::Null)
        return false;
    return true;
}

void CrossfadeController::requestManualFade()
{
    if (!canRequestManualFade()) {
        RS_LOG_DEBUG("audio.crossfade", QStringLiteral("Manual fade requested but not currently possible — ignored"));
        return;
    }
    RS_LOG_INFO(
        "audio.crossfade", QStringLiteral("Manual fade requested: skipping %1, Auto DJ keeps running").arg(m_activeDeck));
    startAutoCrossfade();
}

void CrossfadeController::discardIdleCue()
{
    if (isManualOverride(m_idleDeck) || m_engine->state(m_idleDeck) == DeckState::Null)
        return;
    RS_LOG_INFO("audio.crossfade", QStringLiteral("Discarding stale cue on idle deck %1").arg(m_idleDeck));
    m_engine->unloadDeck(m_idleDeck);
    m_deckTrackId.remove(m_idleDeck);
}

void CrossfadeController::updateManualFadeAvailability()
{
    const bool available = canRequestManualFade();
    if (available == m_lastManualFadeAvailable)
        return;
    m_lastManualFadeAvailable = available;
    emit manualFadeAvailabilityChanged(available);
}

void CrossfadeController::onWatchTick()
{
    updateManualFadeAvailability();

    if (m_autoAdvanceEnabled) {
        if (m_engine->state(m_idleDeck) == DeckState::Null) {
            if (isManualOverride(m_idleDeck))
                RS_LOG_DEBUG("audio.crossfade", QStringLiteral("Deck %1: idle-refill skipped (manual override)").arg(m_idleDeck));
            else
                maybeLoadNextFromQueue(m_idleDeck, /*pauseAfterLoad=*/true);
        }

        if (m_engine->state(m_activeDeck) == DeckState::Null) {
            if (isManualOverride(m_activeDeck)) {
                RS_LOG_DEBUG("audio.crossfade", QStringLiteral("Deck %1: active-refill skipped (manual override)").arg(m_activeDeck));
            } else {
                // Branch on the synchronous return value, not a re-query of
                // engine state — see maybeLoadNextFromQueue()'s doc comment.
                if (maybeLoadNextFromQueue(m_activeDeck, /*pauseAfterLoad=*/false)) {
                    m_engine->play(m_activeDeck);
                    if (const qint64 trackId = m_deckTrackId.value(m_activeDeck, -1); trackId >= 0)
                        radio::db::PlayHistoryRepository::recordPlay(trackId, m_activeDeck);
                }
            }
            return; // give it a moment to actually start before evaluating crossfade timing
        }
    }

    if (m_crossfading || !m_autoAdvanceEnabled)
        return;

    // A crossfade inherently touches both decks at once (fades one out,
    // brings the other up) — if a human has taken over either one, don't
    // half-execute it. No half-measure here: skip starting entirely.
    if (isManualOverride(m_activeDeck) || isManualOverride(m_idleDeck)) {
        RS_LOG_DEBUG("audio.crossfade",
            QStringLiteral("Crossfade-start skipped (manual override active on %1)")
                .arg(isManualOverride(m_activeDeck) ? m_activeDeck : m_idleDeck));
        return;
    }

    if (m_engine->state(m_activeDeck) != DeckState::Playing)
        return;

    // Nothing cued on the other deck — let the active track just play out
    // naturally rather than crossfading into silence.
    if (m_engine->state(m_idleDeck) == DeckState::Null)
        return;

    const qint64 duration = m_engine->duration(m_activeDeck);
    const qint64 position = m_engine->position(m_activeDeck);
    if (duration <= 0 || position < 0)
        return;

    if (duration - position <= m_crossfadeLeadMs) {
        // Re-checked every tick, not latched — see setAutoCrossfadeSuppressor().
        // While this holds, the active deck keeps playing toward its own
        // genuine EOS instead of an early ramped crossfade; onDeckEos()
        // picks up from there.
        if (m_autoCrossfadeSuppressor && m_autoCrossfadeSuppressor()) {
            RS_LOG_DEBUG("audio.crossfade",
                QStringLiteral("Crossfade-start suppressed for %1 -> %2 (letting %1 play to its own EOS)")
                    .arg(m_activeDeck, m_idleDeck));
            return;
        }
        startAutoCrossfade();
    }
}

void CrossfadeController::notifyManualAction(const QString& deckId)
{
    if (m_manualOverride.value(deckId, false))
        return; // already overridden — no-op, keep emissions transition-only
    m_manualOverride[deckId] = true;
    RS_LOG_INFO("audio.crossfade", QStringLiteral("Deck %1: manual override engaged (auto-advance/crossfade paused for this deck)").arg(deckId));
    emit manualOverrideChanged(deckId, true);
}

void CrossfadeController::resumeAutoAdvance(const QString& deckId)
{
    if (!m_manualOverride.value(deckId, false))
        return;
    m_manualOverride[deckId] = false;
    RS_LOG_INFO("audio.crossfade", QStringLiteral("Deck %1: manual override released (auto-advance resumed)").arg(deckId));
    emit manualOverrideChanged(deckId, false);

    if (!isManualOverride(m_activeDeck) && !isManualOverride(m_idleDeck))
        emit handsOffResumed();
}

void CrossfadeController::onDeckEos(const QString& deckId)
{
    // The one deliberate exception to "override clears only on explicit
    // resume": GStreamer doesn't auto-transition state on EOS, so a
    // manually-overridden ACTIVE deck that plays to genuine completion
    // would otherwise sit there indefinitely — override suppresses the
    // crossfade-start check that would normally recover from this,
    // producing silent dead air. Treat "the human's chosen track visibly
    // finished" as a legitimate implicit signal to hand control back.
    if (deckId == m_activeDeck && m_manualOverride.value(deckId, false)) {
        resumeAutoAdvance(deckId);
        return;
    }

    // The active deck reached its own genuine end while a suppressor was
    // holding off the normal ramped crossfade (see
    // setAutoCrossfadeSuppressor()/onWatchTick()) — this is the hand-off
    // moment: nothing is currently playing/transitioning, and the idle deck
    // is sitting cued and ready. Let the caller (e.g. CartAutomationEngine)
    // decide when to actually bring it up via completeSuppressedCrossfade().
    if (deckId == m_activeDeck && !isManualOverride(deckId) && m_autoAdvanceEnabled
        && m_engine->state(m_idleDeck) != DeckState::Null && m_autoCrossfadeSuppressor && m_autoCrossfadeSuppressor()) {
        RS_LOG_INFO("audio.crossfade",
            QStringLiteral("Deck %1 reached its own EOS under suppression — handing off to %2").arg(m_activeDeck, m_idleDeck));
        emit autoCrossfadeSuppressed(m_activeDeck, m_idleDeck);
    }
}

void CrossfadeController::onPipelineError(const QString& source, const QString& message)
{
    // "source" is either a deck id or "stream-sink" (see
    // AudioEngine::handleError) — only decks are this controller's concern.
    if (source != m_activeDeck && source != m_idleDeck)
        return;

    RS_LOG_ERROR("audio.crossfade",
        QStringLiteral("Deck %1 failed to load/play (%2) — unloading so Auto DJ can retry the next queue item")
            .arg(source, message));
    m_engine->unloadDeck(source);
    m_deckTrackId.remove(source);
}

bool CrossfadeController::maybeLoadNextFromQueue(const QString& deckId, bool pauseAfterLoad)
{
    const auto items = radio::db::PlaylistRepository::queueItems();

    // Skip (and drop) any queue entries whose file no longer exists on disk
    // — a track can be moved/deleted from the library after being queued.
    // Without this check, loadTrack() would hand GStreamer a dead path,
    // which fails asynchronously on the engine thread well after this
    // function has already returned; better to catch the common case here
    // and keep walking the queue than to cue a load doomed to fail (a
    // remaining race — deleted between this check and the actual decode —
    // is still covered by onPipelineError()).
    int skipIndex = 0;
    while (skipIndex < items.size() && !QFileInfo::exists(items[skipIndex].filePath)) {
        const auto& missing = items[skipIndex];
        RS_LOG_ERROR("scheduler.autodj",
            QStringLiteral("Queue item '%1' skipped — file missing on disk: %2").arg(missing.title, missing.filePath));
        radio::db::PlaylistRepository::removeItem(missing.playlistItemId);
        ++skipIndex;
    }
    if (skipIndex >= items.size())
        return false;

    const auto& next = items[skipIndex];
    m_engine->loadTrack(deckId, next.filePath);
    if (pauseAfterLoad) {
        // A freshly-attached deck bin, without this, would default to
        // whatever the pipeline is doing — explicitly pausing is what
        // actually guarantees "cued means silent" for a deck that isn't
        // about to be played immediately. Callers that ARE about to call
        // play() right after must pass false: issuing pause() then play()
        // back-to-back on the same deck isn't just redundant, it's
        // genuinely racy (observed: the deck occasionally never reached
        // PLAYING at all, stuck mid-transition).
        m_engine->pause(deckId);
    }
    m_deckTrackId[deckId] = next.trackId;
    radio::db::PlaylistRepository::removeItem(next.playlistItemId);

    RS_LOG_INFO(
        "scheduler.autodj", QStringLiteral("Cued '%1' onto deck %2 from queue").arg(next.title, deckId));
    // trackLoaded before queueConsumed, deliberately: a UI-layer consumer of
    // trackLoaded (DeckWidget, via QueueColorRegistry::takeColor()) needs to
    // claim this entry's per-queue-item display state (e.g. its pill color)
    // before queueConsumed's own UI-layer consumer (QueueWidget::refresh(),
    // which prunes anything no longer in the queue) can treat the
    // now-already-removed entry as abandoned and discard it out from under
    // the deck that's mid-transition to owning it.
    emit trackLoaded(deckId, next.title, next.artist, next.trackId, next.playlistItemId);
    emit queueConsumed();
    return true;
}

void CrossfadeController::startAutoCrossfade()
{
    m_crossfading = true;

    RS_LOG_INFO("audio.crossfade",
        QStringLiteral("Auto crossfade starting: %1 -> %2 over %3ms").arg(m_activeDeck, m_idleDeck).arg(m_fadeDurationMs));
    emit autoCrossfadeStarted(m_activeDeck, m_idleDeck);

    m_engine->play(m_idleDeck);
    if (const qint64 trackId = m_deckTrackId.value(m_idleDeck, -1); trackId >= 0)
        radio::db::PlayHistoryRepository::recordPlay(trackId, m_idleDeck);
    m_engine->rampVolume(m_activeDeck, 1.0, 0.0, m_fadeDurationMs, m_fadeCurve);
    m_engine->rampVolume(m_idleDeck, 0.0, 1.0, m_fadeDurationMs, m_fadeCurve);

    m_finalizeTimer->start(m_fadeDurationMs + 300);
}

void CrossfadeController::onCrossfadeFinished()
{
    finishTransition();
}

void CrossfadeController::finishTransition()
{
    // Fully unload (not just stop) the deck that just finished: unlike
    // stop(), this reports as "nothing cued" (DeckState::Null) to
    // onWatchTick()'s guard, so the newly-active deck's watch cycle doesn't
    // immediately see something on the idle deck and re-trigger a runaway
    // crossfade straight back — a real bug this exact scenario exposed
    // during testing, not just a test artifact: it would also fire for any
    // real track shorter than crossfadeLeadMs.
    m_engine->unloadDeck(m_activeDeck);

    const QString finishedDeck = m_activeDeck;
    std::swap(m_activeDeck, m_idleDeck);
    m_crossfading = false;

    RS_LOG_INFO("audio.crossfade",
        QStringLiteral("Transition complete: active deck is now %1 (was %2)").arg(m_activeDeck, finishedDeck));
    emit autoCrossfadeFinished(m_activeDeck);
}

} // namespace radio::audio
