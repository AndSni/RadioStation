#include "DeckEngine.h"
#include "GstSourcePipeline.h"
#include "MixEngine.h"

#include "core/Logging.h"

#include <algorithm>
#include <cmath>

namespace radio::audio {

DeckEngine::DeckEngine(QString id)
    : m_id(std::move(id))
{
}

DeckEngine::~DeckEngine()
{
    // Defensive: ensures no more pull() calls can reach this (about to be
    // destroyed) deck's GstSourcePipeline even if a caller didn't already
    // stop the whole MixEngine first. Idempotent — safe even if already
    // detached (e.g. by AudioEngine::shutdown() stopping MixEngine first,
    // the normal path, which clears every registered source itself).
    if (m_mixEngine)
        m_mixEngine->detachSource(m_id);
}

bool DeckEngine::build(MixEngine* mixEngine)
{
    m_mixEngine = mixEngine;
    m_pipeline = std::make_unique<GstSourcePipeline>();
    if (!m_pipeline->build()) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Deck %1: failed to build source pipeline").arg(m_id));
        return false;
    }

    GstSourcePipeline* rawPipeline = m_pipeline.get();
    m_pipeline->onEos = [this]() {
        if (onEos)
            onEos();
    };
    m_pipeline->onError = [this](QString message, QString debug) {
        if (onError)
            onError(std::move(message), std::move(debug));
    };
    m_pipeline->onStateChanged = [this](GstState newState) {
        if (onStateChanged)
            onStateChanged(newState);
    };
    m_pipeline->onReplayGainChanged = [this]() { recomputeTrim(); };

    // Registered once, permanently — an idle/unloaded deck simply pulls 0
    // frames (silence), no attach/detach lifecycle needed at all. This is
    // the direct replacement for the old attachToPipeline()/
    // detachFromPipeline() dance into a shared live mixer. withProcessing:
    // decks get the mixer panel's EQ/trim/metering chain; cart-wall clips
    // (which attach without it) don't.
    m_mixEngine->attachSource(
        m_id, [rawPipeline](float* out, size_t frameCount) { return rawPipeline->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);

    RS_LOG_INFO("audio.pipeline", QStringLiteral("Deck %1: built and registered with the mix engine").arg(m_id));
    return true;
}

void DeckEngine::loadUri(const QString& uri)
{
    m_pipeline->loadUri(uri);
    RS_LOG_INFO("audio.pipeline", QStringLiteral("Deck %1: loaded %2").arg(m_id, uri));
    // loadUri() already reset the pipeline's own ReplayGain state to
    // "none" — recompute now so trim drops any auto-compensation left over
    // from whatever was PREVIOUSLY loaded, rather than leaving it applied
    // until (if ever) this new track's own tags arrive.
    recomputeTrim();
}

void DeckEngine::play()
{
    RS_LOG_DEBUG("audio.pipeline", QStringLiteral("Deck %1: play() requested").arg(m_id));
    m_pipeline->play();
}

void DeckEngine::pause()
{
    RS_LOG_DEBUG("audio.pipeline", QStringLiteral("Deck %1: pause() requested").arg(m_id));
    m_pipeline->pause();
}

void DeckEngine::stop()
{
    RS_LOG_DEBUG("audio.pipeline", QStringLiteral("Deck %1: stop() requested").arg(m_id));
    m_pipeline->stop();
}

void DeckEngine::unload()
{
    RS_LOG_DEBUG("audio.pipeline", QStringLiteral("Deck %1: unload() requested").arg(m_id));
    m_pipeline->unload();
}

void DeckEngine::seek(qint64 positionMs)
{
    RS_LOG_DEBUG("audio.pipeline", QStringLiteral("Deck %1: seek() requested to %2ms").arg(m_id).arg(positionMs));
    m_pipeline->seek(positionMs);
}

qint64 DeckEngine::positionMs() const
{
    return m_pipeline->positionMs();
}

qint64 DeckEngine::durationMs() const
{
    return m_pipeline->durationMs();
}

GstState DeckEngine::gstState() const
{
    return m_pipeline->gstState();
}

void DeckEngine::setVolume(double linear)
{
    m_mixEngine->setGain(m_id, linear, 0);
}

double DeckEngine::volume() const
{
    return m_mixEngine->gain(m_id);
}

void DeckEngine::rampVolume(double /*from*/, double to, qint64 durationMs, RampCurve curve)
{
    // `from` is no longer needed as an explicit parameter: MixEngine's
    // ramp always starts from whatever gain is actually audible right now
    // (tracked internally on the audio thread), not a caller-supplied
    // starting point — this is what makes back-to-back ramps (e.g.
    // grabbing the manual crossfader mid-automatic-fade) blend
    // continuously instead of jumping. Kept in the signature to match
    // AudioEngine::rampVolume()'s existing public API exactly.
    RS_LOG_DEBUG("audio.crossfade", QStringLiteral("Deck %1: ramping volume to %2 over %3ms").arg(m_id).arg(to).arg(durationMs));
    m_mixEngine->setGain(m_id, to, durationMs, curve);
}

void DeckEngine::setTrimVolume(double linear)
{
    m_trimVolume = linear;
    recomputeTrim();
}

void DeckEngine::setAutoGainCompensation(bool enabled)
{
    m_autoGainCompensation = enabled;
    recomputeTrim();
}

void DeckEngine::setEqBandGain(int band, double gainDb)
{
    m_mixEngine->setEqBandGain(m_id, band, gainDb);
}

double DeckEngine::eqBandGain(int band) const
{
    return m_mixEngine->eqBandGain(m_id, band);
}

bool DeckEngine::peakLevel(float& outLeft, float& outRight) const
{
    return m_mixEngine->deckPeakLevel(m_id, outLeft, outRight);
}

bool DeckEngine::hasReplayGain() const
{
    return m_pipeline->hasReplayGain();
}

double DeckEngine::replayGainDb() const
{
    return m_pipeline->replayGainDb();
}

double DeckEngine::replayGainPeak() const
{
    return m_pipeline->replayGainPeak();
}

void DeckEngine::recomputeTrim()
{
    double autoFactor = 1.0;
    if (m_autoGainCompensation && m_pipeline->hasReplayGain()) {
        const double appliedDb = m_pipeline->replayGainDb() + m_mixEngine->masterTargetDb();
        autoFactor = std::pow(10.0, appliedDb / 20.0);
        // Cap only the AUTOMATIC portion against clipping — a manually-set
        // trimVolume above 1.0 is the user's own explicit choice (like
        // turning up a real mixer's gain knob, which can clip if pushed too
        // far — expected, not something to silently override), but
        // auto-compensation deriving its own boost from ReplayGain tags
        // should never be the thing that introduces clipping on its own.
        const double peak = m_pipeline->replayGainPeak();
        if (peak > 0.0)
            autoFactor = std::min(autoFactor, 1.0 / peak);
    }
    m_mixEngine->setTrim(m_id, m_trimVolume * autoFactor);
}

} // namespace radio::audio
