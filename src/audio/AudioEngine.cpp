#include "AudioEngine.h"
#include "CartWallEngine.h"
#include "DeckEngine.h"
#include "GstEngineThread.h"

#include "core/Logging.h"
#include "db/LoudnessHistoryRepository.h"

#include <QSemaphore>
#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <chrono>

namespace radio::audio {

namespace {

DeckState toDeckState(GstState state)
{
    switch (state) {
    case GST_STATE_PLAYING:
        return DeckState::Playing;
    case GST_STATE_PAUSED:
        return DeckState::Paused;
    case GST_STATE_READY:
        return DeckState::Ready;
    default:
        return DeckState::Null;
    }
}

}

AudioEngine::AudioEngine(QObject* parent)
    : QObject(parent)
{
}

AudioEngine::~AudioEngine()
{
    shutdown();
}

void AudioEngine::start()
{
    m_engineThread = new GstEngineThread(this);
    m_engineThread->start();

    QSemaphore ready(0);
    m_engineThread->invoke([this, &ready]() {
        buildPipelineOnEngineThread();
        ready.release();
    });
    ready.acquire();

    auto* positionTimer = new QTimer(this);
    positionTimer->setInterval(200);
    connect(positionTimer, &QTimer::timeout, this, &AudioEngine::onPositionTimerTick);
    positionTimer->start();

    // Deliberately a SEPARATE, much coarser timer from positionTimer above
    // — logging a loudness_history row every 200ms would be pointless
    // churn (LUFS meters don't need that resolution to review after the
    // fact) and needlessly grows the DB; 20s is a reasonable middle ground
    // (a multi-hour unattended broadcast still produces a manageable
    // history, but frequently enough to catch a real problem developing).
    auto* loudnessLogTimer = new QTimer(this);
    loudnessLogTimer->setInterval(20000);
    connect(loudnessLogTimer, &QTimer::timeout, this, &AudioEngine::onLoudnessLogTimerTick);
    loudnessLogTimer->start();

    m_duckingController = std::make_unique<DuckingController>(this, this);

    RS_LOG_INFO("audio.pipeline", QStringLiteral("AudioEngine started"));
}

void AudioEngine::buildPipelineOnEngineThread()
{
    // Every source (decks, cart instances, the streaming encoder) is its
    // own fully independent GstPipeline now — no shared live audiomixer
    // for any of them to dynamically attach into, which is what
    // eliminated the GStreamer STATE_LOCK/STREAM_LOCK deadlock this
    // rewrite exists to fix. MixEngine owns the actual mixing and local
    // device output (see MixEngine.h); GstEngineThread's GMainContext is
    // still shared across every pipeline's bus watch, exactly as before.
    m_mixEngine = std::make_unique<MixEngine>();
    m_mixEngine->start();

    // Populated before wireDeck() runs for either deck -- its lambdas
    // capture a raw PendingDeckEvents* looked up via .at(), so the entries
    // must already exist.
    m_pendingDeckEvents.emplace(QStringLiteral("A"), std::make_unique<PendingDeckEvents>());
    m_pendingDeckEvents.emplace(QStringLiteral("B"), std::make_unique<PendingDeckEvents>());

    auto deckA = std::make_unique<DeckEngine>(QStringLiteral("A"));
    deckA->build(m_mixEngine.get());
    wireDeck(deckA.get(), QStringLiteral("A"));
    m_decks.emplace(QStringLiteral("A"), std::move(deckA));

    auto deckB = std::make_unique<DeckEngine>(QStringLiteral("B"));
    deckB->build(m_mixEngine.get());
    wireDeck(deckB.get(), QStringLiteral("B"));
    m_decks.emplace(QStringLiteral("B"), std::move(deckB));

    m_cartWall = std::make_unique<CartWallEngine>(m_mixEngine.get(), m_engineThread);

    m_streamEncoder = std::make_unique<StreamEncoderBin>(m_mixEngine.get(), StreamEncoderBin::MountRole::Primary);
    m_streamEncoder->onError = [this](QString message, QString debug) {
        // Plain mutex-guarded write -- see PendingErrorEvent's doc comment
        // on AudioEngine.h: no Qt cross-thread call from inside
        // GstEngineThread's bus dispatch. Queued (not "latest wins") so a
        // fast-following message (e.g. Phase 2's "Reconnecting..." status,
        // or another raw error right behind it) can never silently clobber
        // an earlier one before drainEngineEvents() gets to it.
        std::lock_guard<std::mutex> lock(m_pendingStreamError.mutex);
        m_pendingStreamError.messages.emplace_back(std::move(message), std::move(debug));
    };

    m_backupStreamEncoder = std::make_unique<StreamEncoderBin>(m_mixEngine.get(), StreamEncoderBin::MountRole::Backup);
    m_backupStreamEncoder->onError = [this](QString message, QString debug) {
        std::lock_guard<std::mutex> lock(m_pendingBackupStreamError.mutex);
        m_pendingBackupStreamError.messages.emplace_back(std::move(message), std::move(debug));
    };

    RS_LOG_INFO("audio.pipeline", QStringLiteral("Audio engine started (decks A/B, cart wall, streaming via MixEngine)"));
}

void AudioEngine::wireDeck(DeckEngine* deck, const QString& deckId)
{
    // Forwarded from GstSourcePipeline's bus watch, called directly from
    // GstEngineThread's GLib dispatch -- so these do ONLY plain mutex-
    // guarded writes into this deck's PendingDeckEvents, never a Qt
    // cross-thread call (see AudioEngine.h's doc comment on
    // PendingDeckEvents for why). drainEngineEvents() (UI thread) is what
    // actually emits the public deckEos/pipelineError/deckStateChanged
    // signals, once per positionTimer tick.
    PendingDeckEvents* pending = m_pendingDeckEvents.at(deckId).get();

    deck->onEos = [pending]() {
        std::lock_guard<std::mutex> lock(pending->mutex);
        ++pending->eosCount;
    };
    deck->onError = [pending](QString message, QString debug) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->hasError = true;
        pending->errorMessage = std::move(message);
        pending->errorDebug = std::move(debug);
    };
    deck->onStateChanged = [pending](GstState newState) {
        std::lock_guard<std::mutex> lock(pending->mutex);
        pending->hasStateChange = true;
        pending->lastState = newState;
    };
}

void AudioEngine::shutdown()
{
    if (!m_engineThread)
        return;

    m_duckingController.reset(); // UI-thread-only, no engine-thread dependency -- safe to tear down anytime here

    QSemaphore done(0);
    m_engineThread->invoke([this, &done]() {
        m_streamEncoder.reset(); // disconnects streaming output first
        m_backupStreamEncoder.reset();
        m_cartWall.reset(); // stops/removes any still-playing cart instances (each detaches itself from MixEngine)

        // Stop MixEngine BEFORE destroying the decks it calls into: its
        // audio thread holds raw GstSourcePipeline* pointers inside each
        // deck's pull callback, and MixEngine::stop() blocks until every
        // registered source is genuinely detached (via ma_node_uninit) —
        // guaranteeing no more pull() calls can happen before we destroy
        // the DeckEngines below. Destroying decks first would risk a
        // use-after-free from a still-running audio callback thread.
        if (m_mixEngine)
            m_mixEngine->stop();
        m_decks.clear();
        done.release();
    });
    done.acquire();

    m_engineThread->stop();
    delete m_engineThread;
    m_engineThread = nullptr;

    RS_LOG_INFO("audio.pipeline", QStringLiteral("AudioEngine shut down"));
}

void AudioEngine::loadTrack(const QString& deckId, const QString& filePath)
{
    const QString uri = QUrl::fromLocalFile(filePath).toString();
    m_engineThread->invoke([this, deckId, uri]() {
        if (auto* deck = findDeck(deckId))
            deck->loadUri(uri);
    });
}

void AudioEngine::play(const QString& deckId)
{
    m_engineThread->invoke([this, deckId]() {
        if (auto* deck = findDeck(deckId))
            deck->play();
    });
}

void AudioEngine::pause(const QString& deckId)
{
    m_engineThread->invoke([this, deckId]() {
        if (auto* deck = findDeck(deckId))
            deck->pause();
    });
}

void AudioEngine::stopDeck(const QString& deckId)
{
    m_engineThread->invoke([this, deckId]() {
        if (auto* deck = findDeck(deckId))
            deck->stop();
    });
}

void AudioEngine::unloadDeck(const QString& deckId)
{
    m_engineThread->invoke([this, deckId]() {
        if (auto* deck = findDeck(deckId))
            deck->unload();
    });
}

void AudioEngine::seek(const QString& deckId, qint64 positionMs)
{
    m_engineThread->invoke([this, deckId, positionMs]() {
        if (auto* deck = findDeck(deckId))
            deck->seek(positionMs);
    });
}

void AudioEngine::setVolume(const QString& deckId, double linear)
{
    if (auto* deck = findDeck(deckId))
        deck->setVolume(linear);
}

void AudioEngine::rampVolume(const QString& deckId, double from, double to, qint64 durationMs, RampCurve curve)
{
    m_engineThread->invoke([this, deckId, from, to, durationMs, curve]() {
        if (auto* deck = findDeck(deckId))
            deck->rampVolume(from, to, durationMs, curve);
    });
}

QString AudioEngine::triggerCart(const QString& filePath)
{
    if (!m_cartWall)
        return QString();
    return m_cartWall->trigger(filePath);
}

void AudioEngine::stopAllCarts()
{
    if (m_cartWall)
        m_cartWall->stopAll();
}

void AudioEngine::connectStreaming(const StreamEncoderBin::Config& config)
{
    // "Integrated loudness" is scoped to "since this broadcast began" (see
    // MixEngine::resetIntegratedLoudnessMeasurement()'s doc comment) — the
    // primary mount connecting is what "going live" means here, so this is
    // the natural place to restart the measurement. Safe to call directly
    // (no engine-thread dispatch needed, unlike m_streamEncoder->connect()
    // itself) since MixEngine's own methods are documented safe from any
    // thread except its own audio callback.
    if (m_mixEngine)
        m_mixEngine->resetIntegratedLoudnessMeasurement();

    m_engineThread->invoke([this, config]() {
        if (m_streamEncoder)
            m_streamEncoder->connect(config);
    });
}

void AudioEngine::disconnectStreaming()
{
    m_engineThread->invoke([this]() {
        if (m_streamEncoder)
            m_streamEncoder->disconnect();
    });
}

bool AudioEngine::isStreamingConnected() const
{
    if (!m_streamEncoder)
        return false;
    bool connected = false;
    QSemaphore done(0);
    m_engineThread->invoke([this, &connected, &done]() {
        connected = m_streamEncoder->isConnected();
        done.release();
    });
    done.acquire();
    return connected;
}

void AudioEngine::connectBackupStreaming(const StreamEncoderBin::Config& config)
{
    m_engineThread->invoke([this, config]() {
        if (m_backupStreamEncoder)
            m_backupStreamEncoder->connect(config);
    });
}

void AudioEngine::disconnectBackupStreaming()
{
    m_engineThread->invoke([this]() {
        if (m_backupStreamEncoder)
            m_backupStreamEncoder->disconnect();
    });
}

bool AudioEngine::isBackupStreamingConnected() const
{
    if (!m_backupStreamEncoder)
        return false;
    bool connected = false;
    QSemaphore done(0);
    m_engineThread->invoke([this, &connected, &done]() {
        connected = m_backupStreamEncoder->isConnected();
        done.release();
    });
    done.acquire();
    return connected;
}

int AudioEngine::activeCartCount() const
{
    return m_cartWall ? static_cast<int>(m_cartWall->activeCount()) : 0;
}

bool AudioEngine::isCartTokenActive(const QString& token) const
{
    return m_cartWall && m_cartWall->isActive(token);
}

qint64 AudioEngine::position(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return deck->positionMs();
    return -1;
}

qint64 AudioEngine::duration(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return deck->durationMs();
    return -1;
}

DeckState AudioEngine::state(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return toDeckState(deck->gstState());
    return DeckState::Null;
}

double AudioEngine::volume(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return deck->volume();
    return 0.0;
}

void AudioEngine::setDeckTrimVolume(const QString& deckId, double linear)
{
    if (auto* deck = findDeck(deckId))
        deck->setTrimVolume(linear);
}

void AudioEngine::setDeckDuckGain(const QString& deckId, double linear)
{
    if (m_mixEngine)
        m_mixEngine->setDuckGain(deckId, linear);
}

void AudioEngine::setDeckAutoGainCompensation(const QString& deckId, bool enabled)
{
    if (auto* deck = findDeck(deckId))
        deck->setAutoGainCompensation(enabled);
}

bool AudioEngine::deckAutoGainCompensationEnabled(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return deck->autoGainCompensationEnabled();
    return false;
}

void AudioEngine::setDeckEqBandGain(const QString& deckId, int band, double gainDb)
{
    if (auto* deck = findDeck(deckId))
        deck->setEqBandGain(band, gainDb);
}

double AudioEngine::deckEqBandGain(const QString& deckId, int band) const
{
    if (auto* deck = findDeck(deckId))
        return deck->eqBandGain(band);
    return 0.0;
}

bool AudioEngine::deckPeakLevel(const QString& deckId, float& outLeft, float& outRight) const
{
    outLeft = 0.0f;
    outRight = 0.0f;
    if (auto* deck = findDeck(deckId))
        return deck->peakLevel(outLeft, outRight);
    return false;
}

bool AudioEngine::deckHasReplayGain(const QString& deckId) const
{
    if (auto* deck = findDeck(deckId))
        return deck->hasReplayGain();
    return false;
}

void AudioEngine::setMasterVolume(double linear)
{
    if (m_mixEngine)
        m_mixEngine->setMasterVolume(linear);
}

void AudioEngine::setMasterEqBandGain(int band, double gainDb)
{
    if (m_mixEngine)
        m_mixEngine->setMasterEqBandGain(band, gainDb);
}

double AudioEngine::masterEqBandGain(int band) const
{
    return m_mixEngine ? m_mixEngine->masterEqBandGain(band) : 0.0;
}

void AudioEngine::setLoudnessEnabled(bool enabled)
{
    if (m_mixEngine)
        m_mixEngine->setLoudnessEnabled(enabled);
}

void AudioEngine::setMasterTargetDb(double targetDb)
{
    if (m_mixEngine)
        m_mixEngine->setMasterTargetDb(targetDb);
}

double AudioEngine::masterTargetDb() const
{
    return m_mixEngine ? m_mixEngine->masterTargetDb() : 0.0;
}

void AudioEngine::masterPeakLevel(float& outLeft, float& outRight) const
{
    outLeft = 0.0f;
    outRight = 0.0f;
    if (m_mixEngine)
        m_mixEngine->masterPeakLevel(outLeft, outRight);
}

void AudioEngine::setMasterHighPassEnabled(bool enabled)
{
    if (m_mixEngine)
        m_mixEngine->setMasterHighPassEnabled(enabled);
}

void AudioEngine::setCompressorEnabled(bool enabled)
{
    if (m_mixEngine)
        m_mixEngine->setCompressorEnabled(enabled);
}

void AudioEngine::setBandThresholdDb(int band, double thresholdDb)
{
    if (m_mixEngine)
        m_mixEngine->setBandThresholdDb(band, thresholdDb);
}

double AudioEngine::bandThresholdDb(int band) const
{
    return m_mixEngine ? m_mixEngine->bandThresholdDb(band) : 0.0;
}

void AudioEngine::setBandRatio(int band, double ratio)
{
    if (m_mixEngine)
        m_mixEngine->setBandRatio(band, ratio);
}

double AudioEngine::bandRatio(int band) const
{
    return m_mixEngine ? m_mixEngine->bandRatio(band) : 1.0;
}

double AudioEngine::bandGainReductionDb(int band) const
{
    return m_mixEngine ? m_mixEngine->bandGainReductionDb(band) : 0.0;
}

void AudioEngine::setLimiterCeilingDb(double ceilingDb)
{
    if (m_mixEngine)
        m_mixEngine->setLimiterCeilingDb(ceilingDb);
}

double AudioEngine::limiterGainReductionDb() const
{
    return m_mixEngine ? m_mixEngine->limiterGainReductionDb() : 0.0;
}

void AudioEngine::setLevelerEnabled(bool enabled)
{
    if (m_mixEngine)
        m_mixEngine->setLevelerEnabled(enabled);
}

void AudioEngine::setLevelerRangeDb(double rangeDb)
{
    if (m_mixEngine)
        m_mixEngine->setLevelerRangeDb(rangeDb);
}

double AudioEngine::levelerGainDb() const
{
    return m_mixEngine ? m_mixEngine->levelerGainDb() : 0.0;
}

double AudioEngine::momentaryLoudnessLufs() const
{
    return m_mixEngine ? m_mixEngine->momentaryLoudnessLufs() : 0.0;
}

double AudioEngine::shortTermLoudnessLufs() const
{
    return m_mixEngine ? m_mixEngine->shortTermLoudnessLufs() : 0.0;
}

double AudioEngine::integratedLoudnessLufs() const
{
    return m_mixEngine ? m_mixEngine->integratedLoudnessLufs() : 0.0;
}

double AudioEngine::outputTruePeakDb() const
{
    return m_mixEngine ? m_mixEngine->outputTruePeakDb() : -100.0;
}

void AudioEngine::setPreferredPlaybackDeviceId(const QByteArray& id)
{
    if (m_mixEngine)
        m_mixEngine->setPreferredPlaybackDeviceId(id);
}

bool AudioEngine::switchPlaybackDevice(const QByteArray& id)
{
    return m_mixEngine && m_mixEngine->switchPlaybackDevice(id);
}

bool AudioEngine::startMonitorDevice(const QByteArray& id)
{
    return m_mixEngine && m_mixEngine->startMonitorDevice(id);
}

void AudioEngine::stopMonitorDevice()
{
    if (m_mixEngine)
        m_mixEngine->stopMonitorDevice();
}

bool AudioEngine::isMonitorDeviceRunning() const
{
    return m_mixEngine && m_mixEngine->isMonitorDeviceRunning();
}

bool AudioEngine::startMicInput(const QByteArray& id)
{
    return m_mixEngine && m_mixEngine->startMicInput(id);
}

void AudioEngine::stopMicInput()
{
    if (m_mixEngine)
        m_mixEngine->stopMicInput();
}

bool AudioEngine::isMicInputRunning() const
{
    return m_mixEngine && m_mixEngine->isMicInputRunning();
}

void AudioEngine::setMicGain(double linear)
{
    if (m_mixEngine)
        m_mixEngine->setGain(QStringLiteral("mic"), linear);
}

void AudioEngine::micPeakLevel(float& outLeft, float& outRight) const
{
    outLeft = 0.0f;
    outRight = 0.0f;
    if (m_mixEngine)
        m_mixEngine->deckPeakLevel(QStringLiteral("mic"), outLeft, outRight);
}

void AudioEngine::setDuckingEnabled(bool enabled)
{
    if (m_duckingController)
        m_duckingController->setEnabled(enabled);
}

bool AudioEngine::isDuckingEnabled() const
{
    return m_duckingController && m_duckingController->isEnabled();
}

void AudioEngine::setDuckingThresholdLinear(double threshold)
{
    if (m_duckingController)
        m_duckingController->setThresholdLinear(threshold);
}

void AudioEngine::setDuckingAmountDb(double amountDb)
{
    if (m_duckingController)
        m_duckingController->setDuckAmountDb(amountDb);
}

void AudioEngine::setDuckingReleaseHoldMs(qint64 holdMs)
{
    if (m_duckingController)
        m_duckingController->setReleaseHoldMs(holdMs);
}

void AudioEngine::setDeadAirThresholdSeconds(double seconds)
{
    m_deadAirThresholdSeconds = seconds;
}

DeckEngine* AudioEngine::findDeck(const QString& deckId) const
{
    const auto it = m_decks.find(deckId);
    return it != m_decks.end() ? it->second.get() : nullptr;
}

void AudioEngine::onPositionTimerTick()
{
    drainEngineEvents();

    // Neither of these is safe to do from inside MixEngine's own audio
    // callback or the miniaudio device-notification callback (see
    // MixEngine::pollForDeviceLoss()'s doc comment) — this 200ms UI-thread
    // tick is the established safe place for exactly that class of check,
    // same reasoning as drainEngineEvents() above.
    if (m_mixEngine && m_mixEngine->pollForDeviceLoss()) {
        const QString message = m_mixEngine->deviceLostRecoverySucceeded()
            ? QStringLiteral("Playback device lost — recovered using the system default device")
            : QStringLiteral("Playback device lost and recovery failed — no audio output");
        handleError(QStringLiteral("audio-device"), message, QString());
    }
    checkDeadAir();

    emit positionsChanged();
}

void AudioEngine::checkDeadAir()
{
    if (!m_mixEngine)
        return;

    float left = 0.0f;
    float right = 0.0f;
    m_mixEngine->masterPeakLevel(left, right);
    // -50dBFS linear (~0.00316) — an internal implementation detail, not a
    // user-exposed knob (unlike the threshold duration below): quiet
    // program material or a fade tail is well above this, so it only trips
    // on genuine silence.
    constexpr float kSilenceLinearThreshold = 0.00316f;
    const bool silent = std::max(left, right) < kSilenceLinearThreshold;

    const qint64 nowMs
        = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();

    if (!silent) {
        m_silenceStartMs = -1;
        if (m_deadAirAlerted) {
            m_deadAirAlerted = false;
            emit deadAirCleared();
        }
        return;
    }

    if (m_silenceStartMs < 0) {
        m_silenceStartMs = nowMs;
        return;
    }

    if (!m_deadAirAlerted && (nowMs - m_silenceStartMs) >= static_cast<qint64>(m_deadAirThresholdSeconds * 1000.0)) {
        m_deadAirAlerted = true;
        emit deadAirDetected();
    }
}

void AudioEngine::onLoudnessLogTimerTick()
{
    if (!m_mixEngine)
        return;

    radio::db::LoudnessHistoryRepository::recordMeasurement(m_mixEngine->integratedLoudnessLufs(),
        m_mixEngine->momentaryLoudnessLufs(), m_mixEngine->shortTermLoudnessLufs(), m_mixEngine->outputTruePeakDb());
}

void AudioEngine::drainEngineEvents()
{
    for (auto& [deckId, pending] : m_pendingDeckEvents) {
        int eosCount = 0;
        bool hasStateChange = false;
        GstState lastState = GST_STATE_NULL;
        bool hasError = false;
        QString errorMessage;
        QString errorDebug;
        {
            std::lock_guard<std::mutex> lock(pending->mutex);
            eosCount = pending->eosCount;
            pending->eosCount = 0;
            hasStateChange = pending->hasStateChange;
            pending->hasStateChange = false;
            lastState = pending->lastState;
            hasError = pending->hasError;
            pending->hasError = false;
            errorMessage = std::move(pending->errorMessage);
            errorDebug = std::move(pending->errorDebug);
        }

        // Error before state-changed: a load-time error now forces the
        // pipeline to NULL (see GstSourcePipeline::onBusMessage's
        // GST_MESSAGE_ERROR case), which shortly produces its own
        // STATE_CHANGED(NULL) -- reporting the error first is the causally
        // correct order. EOS last.
        if (hasError)
            handleError(deckId, errorMessage, errorDebug);
        if (hasStateChange)
            handleDeckStateChanged(deckId, lastState);
        for (int i = 0; i < eosCount; ++i)
            handleDeckEos(deckId);
    }

    std::vector<std::pair<QString, QString>> streamMessages;
    {
        std::lock_guard<std::mutex> lock(m_pendingStreamError.mutex);
        streamMessages = std::move(m_pendingStreamError.messages);
        m_pendingStreamError.messages.clear(); // messages is in a valid-but-unspecified state after the move above; explicitly reset it rather than relying on that
    }
    for (const auto& [message, debug] : streamMessages) {
        // "stream-" prefix preserved deliberately: MainWindow filters on it
        // to route streaming errors to the streaming status badge
        // specifically (see MainWindow::onPipelineError). Emitted in order
        // -- see PendingErrorEvent's doc comment on why this is a queue,
        // not a single "latest wins" slot.
        handleError(QStringLiteral("stream-sink"), message, debug);
    }

    std::vector<std::pair<QString, QString>> backupStreamMessages;
    {
        std::lock_guard<std::mutex> lock(m_pendingBackupStreamError.mutex);
        backupStreamMessages = std::move(m_pendingBackupStreamError.messages);
        m_pendingBackupStreamError.messages.clear();
    }
    for (const auto& [message, debug] : backupStreamMessages) {
        // Distinct source ("stream-sink-backup", not just "stream-sink")
        // so the primary and backup mounts' statuses stay distinguishable
        // -- MainWindow's single top-level status badge only tracks the
        // primary mount (exact match, not startsWith); the backup tab in
        // StreamingSettingsDialog is what shows this one.
        handleError(QStringLiteral("stream-sink-backup"), message, debug);
    }
}

void AudioEngine::handleError(const QString& source, const QString& message, const QString& debug)
{
    RS_LOG_ERROR("audio.pipeline", QStringLiteral("[%1] %2 (%3)").arg(source, message, debug));
    emit pipelineError(source, message);
}

void AudioEngine::handleDeckEos(const QString& deckId)
{
    // Each deck is now its own independent pipeline, so EOS is
    // unambiguous per-deck — no more guessing based on shared-mixer
    // forwarding semantics (the old audiomixer-based design could only
    // report EOS at the pipeline level once ALL connected decks had
    // EOS'd, requiring a loop-and-guess over every deck's current state).
    RS_LOG_INFO("audio.pipeline", QStringLiteral("EOS from deck %1").arg(deckId));
    emit deckEos(deckId);
}

void AudioEngine::handleDeckStateChanged(const QString& deckId, GstState newState)
{
    RS_LOG_INFO("audio.pipeline",
        QStringLiteral("Deck %1 state -> %2").arg(deckId, gst_element_state_get_name(newState)));
    emit deckStateChanged(deckId, toDeckState(newState));
}

} // namespace radio::audio
