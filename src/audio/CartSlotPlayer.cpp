#include "CartSlotPlayer.h"
#include "GstEngineThread.h"
#include "GstSourcePipeline.h"
#include "MixEngine.h"

#include "core/Logging.h"

#include <algorithm>
#include <cmath>

namespace radio::audio {

CartSlotPlayer::CartSlotPlayer(MixEngine* mixEngine, GstEngineThread* engineThread, QString uri, QString token,
    std::function<void(CartSlotPlayer*)> onFinished)
    : m_mixEngine(mixEngine)
    , m_engineThread(engineThread)
    , m_uri(std::move(uri))
    , m_onFinished(std::move(onFinished))
    // Doubles as the MixEngine source id (still needs to be unique per
    // instance — multiple overlapping instances of the SAME clip are a
    // deliberate, legitimate case) and the externally-visible completion
    // token callers correlate against CartWallEngine::trigger()'s
    // onFinished callback.
    , m_mixSourceId(std::move(token))
{
}

CartSlotPlayer::~CartSlotPlayer()
{
    teardown();
}

bool CartSlotPlayer::start()
{
    m_pipeline = std::make_unique<GstSourcePipeline>();
    if (!m_pipeline->build()) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Cart: failed to build source pipeline for %1").arg(m_uri));
        m_pipeline.reset();
        return false;
    }

    GstSourcePipeline* rawPipeline = m_pipeline.get();
    m_pipeline->onEos = [this]() {
        // Deferred, not called inline: this callback runs from INSIDE
        // GstSourcePipeline's own bus-dispatch call stack — destroying
        // that same pipeline object here (which teardown() does) would be
        // unsafe self-destruction mid-callback. Queue the actual teardown
        // for the engine thread's next loop iteration instead, matching
        // the equivalent safety consideration the previous pad-probe-based
        // design already handled the same way.
        m_engineThread->invoke([this]() {
            teardown();
            if (m_onFinished)
                m_onFinished(this);
        });
    };
    m_pipeline->onError = [this](const QString& message, const QString& debug) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Cart error (%1): %2 (%3)").arg(m_uri, message, debug));
        // Phase 2 watchdog: mirrors onEos's exact deferred-teardown shape
        // above (same reasoning — this callback runs from inside
        // GstSourcePipeline's own bus-dispatch call stack, so destruction
        // has to be queued, not inline). Without this, a cart that errors
        // mid-playback stayed "active" in CartWallEngine::m_active forever
        // — isCartTokenActive() kept reporting true, activeCount() stayed
        // inflated, and the MixEngine source attachment leaked, since
        // onError previously only logged and never called teardown()/
        // m_onFinished the way onEos always has.
        m_engineThread->invoke([this]() {
            teardown();
            if (m_onFinished)
                m_onFinished(this);
        });
    };
    m_pipeline->onReplayGainChanged = [this]() { recomputeTrim(); };

    // withProcessing=true gives the cart the same 5-band EQ chain a deck
    // has (previously it had none at all — flat, unshaped audio next to a
    // deck's own EQ'd signal is what made carts sound comparatively too
    // loud/harsh). Seeded from Deck A's current curve at trigger time, not
    // continuously synced or independently tunable — a cart is a short,
    // one-shot clip, not something a user rides an EQ on mid-playback.
    m_mixEngine->attachSource(
        m_mixSourceId, [rawPipeline](float* out, size_t frameCount) { return rawPipeline->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    for (int band = 0; band < 5; ++band)
        m_mixEngine->setEqBandGain(m_mixSourceId, band, m_mixEngine->eqBandGain(QStringLiteral("A"), band));

    m_pipeline->loadUri(m_uri);
    m_pipeline->play();

    RS_LOG_INFO("audio.pipeline", QStringLiteral("Cart triggered: %1").arg(m_uri));
    return true;
}

void CartSlotPlayer::recomputeTrim()
{
    double autoFactor = 1.0;
    if (m_pipeline->hasReplayGain()) {
        const double appliedDb = m_pipeline->replayGainDb() + m_mixEngine->masterTargetDb();
        autoFactor = std::pow(10.0, appliedDb / 20.0);
        // Cap only against clipping, same as DeckEngine::recomputeTrim() --
        // ReplayGain-derived boost should never introduce clipping on its
        // own, even though carts have no manual trim knob whose deliberate
        // clipping this would otherwise need to leave alone.
        const double peak = m_pipeline->replayGainPeak();
        if (peak > 0.0)
            autoFactor = std::min(autoFactor, 1.0 / peak);
    }
    m_mixEngine->setTrim(m_mixSourceId, autoFactor);
}

void CartSlotPlayer::teardown()
{
    if (m_tornDown)
        return;
    m_tornDown = true;

    if (m_mixEngine)
        m_mixEngine->detachSource(m_mixSourceId);
    m_pipeline.reset();
}

} // namespace radio::audio
