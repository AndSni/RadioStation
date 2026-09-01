#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <map>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

#include <gst/gst.h>

#include "DuckingController.h"
#include "MixEngine.h"
#include "StreamEncoderBin.h"

namespace radio::audio {

class GstEngineThread;
class DeckEngine;
class CartWallEngine;

enum class DeckState { Null, Ready, Paused, Playing };

// Owns MixEngine (the real-time mixing/output layer — see MixEngine.h) and
// the dedicated engine thread that runs every independent GstSourcePipeline
// instance's bus/main loop (decks, cart instances, and the streaming
// encoder each have their own fully independent GstPipeline now — no more
// shared live audiomixer for dynamic bins to deadlock against). Lives on
// the UI thread. Control methods that mutate a pipeline graph are
// dispatched asynchronously to the engine thread; positionMs()/
// durationMs()/state() call directly into GStreamer, which documents those
// as thread-safe.
class AudioEngine : public QObject {
    Q_OBJECT

public:
    explicit AudioEngine(QObject* parent = nullptr);
    ~AudioEngine() override;

    // Builds the pipeline (mixer + tee + local sink + deck "A") on the
    // engine thread and blocks until that one-time construction completes,
    // so decks are guaranteed to exist by the time this returns.
    void start();
    void shutdown();

    void loadTrack(const QString& deckId, const QString& filePath);
    void play(const QString& deckId);
    void pause(const QString& deckId);
    void stopDeck(const QString& deckId);
    void unloadDeck(const QString& deckId);
    void seek(const QString& deckId, qint64 positionMs);
    void setVolume(const QString& deckId, double linear);
    void rampVolume(const QString& deckId, double from, double to, qint64 durationMs, RampCurve curve = RampCurve::Linear);

    // Returns a token identifying this specific triggered instance — see
    // isCartTokenActive(). Callers that don't care when it ends can just
    // ignore the return value, same as before this returned anything.
    QString triggerCart(const QString& filePath);
    void stopAllCarts();
    int activeCartCount() const; // test/debug use; see CartWallEngine::activeCount()

    // True if the instance triggerCart() returned this token for is still
    // playing — poll this (e.g. from a QTimer) to find out when a specific
    // triggered cart finishes. Cheap: reads a mutex-guarded snapshot rather
    // than round-tripping through the engine thread (see
    // CartWallEngine::isActive()'s doc comment) — safe to poll frequently
    // (CartGridWidget/CartAutomationEngine each do, every 150ms per active
    // token). Deliberately NOT a push notification (no "cart finished"
    // signal exists) — see CartWallEngine::trigger()'s doc comment for why.
    bool isCartTokenActive(const QString& token) const;

    void connectStreaming(const StreamEncoderBin::Config& config);
    void disconnectStreaming();
    bool isStreamingConnected() const; // blocks briefly for an engine-thread-accurate read

    // A second, fully independent streaming mount for redundancy (see
    // StreamEncoderBin::MountRole) -- deliberately a parallel API trio
    // rather than parameterizing the three methods above with an index, so
    // the existing single-mount call sites (StreamingSettingsDialog,
    // MainWindow's status label) stay untouched.
    void connectBackupStreaming(const StreamEncoderBin::Config& config);
    void disconnectBackupStreaming();
    bool isBackupStreamingConnected() const;

    qint64 position(const QString& deckId) const;
    qint64 duration(const QString& deckId) const;
    DeckState state(const QString& deckId) const;
    double volume(const QString& deckId) const;

    // --- Mixer panel: a separate gain stage from setVolume()/rampVolume()
    // above (which drive the crossfade's A/B balance) — see DeckEngine::
    // setTrimVolume()'s doc comment. All safe to call directly from the UI
    // thread (no engine-thread dispatch needed), matching setVolume()/
    // volume()'s existing pattern: everything these reach is already
    // thread-safe (MixEngine's atomics, GstSourcePipeline's ReplayGain
    // atomics).
    void setDeckTrimVolume(const QString& deckId, double linear);

    // A FOURTH gain stage (see MixEngine::setDuckGain()'s doc comment),
    // deliberately bypassing DeckEngine (unlike setDeckTrimVolume() above,
    // which routes through DeckEngine::setTrimVolume() to combine volume%
    // and auto-gain-compensation into one number) — DeckEngine has no
    // reason to know about ducking, and going through it would mean this
    // and setDeckTrimVolume() clobber each other's writes to the same
    // value. Only DuckingController is expected to call this.
    void setDeckDuckGain(const QString& deckId, double linear);

    void setDeckAutoGainCompensation(const QString& deckId, bool enabled);
    bool deckAutoGainCompensationEnabled(const QString& deckId) const;
    void setDeckEqBandGain(const QString& deckId, int band, double gainDb); // see MixEngine::setEqBandGain()'s doc comment for band indices
    double deckEqBandGain(const QString& deckId, int band) const; // see MixEngine::eqBandGain()
    bool deckPeakLevel(const QString& deckId, float& outLeft, float& outRight) const;
    bool deckHasReplayGain(const QString& deckId) const;

    void setMasterVolume(double linear);
    void setMasterEqBandGain(int band, double gainDb); // see MixEngine::setMasterEqBandGain()'s doc comment for band indices
    double masterEqBandGain(int band) const; // see MixEngine::masterEqBandGain()
    void setLoudnessEnabled(bool enabled);
    void setMasterTargetDb(double targetDb);
    double masterTargetDb() const;
    void masterPeakLevel(float& outLeft, float& outRight) const;
    void setMasterHighPassEnabled(bool enabled);
    // --- Multiband compressor (Phase 3). Thin passthroughs to MixEngine —
    // see MixEngine::setCompressorEnabled()'s doc comment for the full
    // picture. band: 0=Low, 1=Mid, 2=High.
    void setCompressorEnabled(bool enabled);
    void setBandThresholdDb(int band, double thresholdDb);
    double bandThresholdDb(int band) const;
    void setBandRatio(int band, double ratio);
    double bandRatio(int band) const;
    double bandGainReductionDb(int band) const;

    void setLimiterCeilingDb(double ceilingDb);
    double limiterGainReductionDb() const;
    void setLevelerEnabled(bool enabled);
    void setLevelerRangeDb(double rangeDb);
    double levelerGainDb() const;

    // --- EBU R128/LUFS loudness metering (Phase 3). Thin passthroughs to
    // MixEngine, same "no engine-thread dispatch needed" reasoning as
    // masterPeakLevel()/limiterGainReductionDb() above.
    double momentaryLoudnessLufs() const;
    double shortTermLoudnessLufs() const;
    double integratedLoudnessLufs() const;
    double outputTruePeakDb() const;

    // --- Output device management (Phase 2). Thin passthroughs to
    // MixEngine — none of these need GstEngineThread::invoke() dispatch
    // (MixEngine's own methods are documented safe from any thread except
    // its own audio callback), but they're kept here rather than exposed
    // directly so AudioEngine stays the sole gateway into audio internals
    // for UI code, matching its existing role.
    static std::vector<AudioDeviceInfo> enumeratePlaybackDevices() { return MixEngine::enumeratePlaybackDevices(); }
    void setPreferredPlaybackDeviceId(const QByteArray& id);
    bool switchPlaybackDevice(const QByteArray& id);
    bool startMonitorDevice(const QByteArray& id);
    void stopMonitorDevice();
    bool isMonitorDeviceRunning() const;

    // --- Microphone input (Phase 3). Thin passthroughs to MixEngine — see
    // MixEngine::startMicInput()'s doc comment for the full picture.
    // Deliberately named setMicGain()/micPeakLevel() rather than reusing
    // setVolume()/masterPeakLevel()-style names even though they'd work
    // identically (the mic is just another attached source, "mic") — a
    // distinct name reads far more clearly at UI call sites than
    // setVolume(QStringLiteral("mic"), ...) would. EQ/trim/metering for the
    // mic strip itself reuse the existing generic setDeckEqBandGain()/
    // setDeckTrimVolume()/deckPeakLevel() directly with id "mic" — no new
    // methods needed for those, since MixEngine's per-source processing was
    // already generic over any attached source id.
    static std::vector<AudioDeviceInfo> enumerateCaptureDevices() { return MixEngine::enumerateCaptureDevices(); }
    bool startMicInput(const QByteArray& id);
    void stopMicInput();
    bool isMicInputRunning() const;
    void setMicGain(double linear);
    void micPeakLevel(float& outLeft, float& outRight) const;

    // --- Mic ducking (Phase 3). AudioEngine owns the DuckingController
    // instance (constructed in start(), see DuckingController.h) and
    // exposes it as thin passthroughs, same "sole gateway into audio
    // internals for UI code" role as everything else here — unlike
    // CrossfadeController (owned by MainWindow: it orchestrates deck
    // loading/scheduling, genuinely a station-level concern), ducking is a
    // self-contained mic-level-to-deck-trim feedback loop with no
    // dependencies outside AudioEngine itself.
    void setDuckingEnabled(bool enabled);
    bool isDuckingEnabled() const;
    void setDuckingThresholdLinear(double threshold);
    void setDuckingAmountDb(double amountDb);
    void setDuckingReleaseHoldMs(qint64 holdMs);

    // --- Dead-air detection (Phase 2). Detection only, reusing
    // masterPeakLevel() — no policy here; MainWindow owns the response
    // (alert + optional emergency-cart failover), matching how it already
    // owns pipelineError's response.
    void setDeadAirThresholdSeconds(double seconds);

signals:
    void deckStateChanged(const QString& deckId, radio::audio::DeckState state);
    void deckEos(const QString& deckId);
    void pipelineError(const QString& source, const QString& message);
    void positionsChanged(); // emitted ~5x/sec; UI re-reads position()/duration()/state()
    void deadAirDetected();
    void deadAirCleared();

private:
    // Holding area for events that arrive on the engine thread (from inside
    // GstEngineThread's GLib bus dispatch) and need to reach the UI thread.
    // Deliberately NOT delivered via QMetaObject::invokeMethod(...,
    // Qt::QueuedConnection) called from inside that dispatch — see
    // CartWallEngine.h's doc comment: that exact shape (a callback fired
    // from GstEngineThread's call stack reaching into Qt's cross-thread
    // signal machinery) reliably corrupted the heap for cart completion
    // notification and was deliberately replaced with plain engine-thread
    // state + UI-thread polling. This generalizes that same fix to decks
    // and the stream encoder: callbacks here only ever do plain mutex-
    // guarded C++ writes (safe from any thread), and drainEngineEvents()
    // (UI thread, via the existing positionTimer tick) is what actually
    // emits the public Qt signals.
    struct PendingDeckEvents {
        std::mutex mutex;
        int eosCount = 0; // counter, not bool -- CrossfadeController::onDeckEos drives real state (manual-override resume), a dropped EOS is a real loss
        bool hasStateChange = false; // state/error coalesce to "latest wins" -- only the current value matters for UI
        GstState lastState = GST_STATE_NULL;
        bool hasError = false;
        QString errorMessage;
        QString errorDebug;
    };
    // A QUEUE, not a single "latest wins" slot like PendingDeckEvents'
    // error fields above: the stream encoder's onError channel now also
    // carries transient, meaningfully-ordered status text (Phase 2's
    // reconnect-with-backoff — "Reconnecting to streaming server..." — see
    // StreamEncoderBin::onReconnectTimeout()), not just raw GStreamer error
    // strings. Confirmed via a real hang-turned-flaky-test investigation:
    // when a retry fails near-instantly (e.g. a "not-negotiated" caps
    // error firing within the same 200ms drain window), a single-slot
    // "latest wins" design silently drops the "Reconnecting..." message
    // every time, overwritten by the next raw error before it's ever
    // drained -- the operator's status label would get stuck cycling
    // through raw technical error text and never show the friendlier
    // retry status, even though reconnection itself was working correctly
    // the whole time.
    struct PendingErrorEvent {
        std::mutex mutex;
        std::vector<std::pair<QString, QString>> messages; // {message, debug}, oldest first
    };

    void buildPipelineOnEngineThread();
    void wireDeck(DeckEngine* deck, const QString& deckId);
    DeckEngine* findDeck(const QString& deckId) const;

    void handleError(const QString& source, const QString& message, const QString& debug);
    void handleDeckEos(const QString& deckId);
    void handleDeckStateChanged(const QString& deckId, GstState newState);

    void onPositionTimerTick();
    void onLoudnessLogTimerTick();
    void drainEngineEvents();
    void checkDeadAir();

    GstEngineThread* m_engineThread = nullptr;

    std::unique_ptr<MixEngine> m_mixEngine;
    std::unique_ptr<DuckingController> m_duckingController;
    std::map<QString, std::unique_ptr<DeckEngine>> m_decks;
    std::unique_ptr<CartWallEngine> m_cartWall;
    std::unique_ptr<StreamEncoderBin> m_streamEncoder;
    std::unique_ptr<StreamEncoderBin> m_backupStreamEncoder;

    // unique_ptr values: std::mutex isn't movable/copyable (map wouldn't
    // otherwise be constructible in-place), and this gives stable pointers
    // for wireDeck()'s lambda captures. Populated once, synchronously, in
    // buildPipelineOnEngineThread() before wireDeck() runs for either deck
    // -- the map itself is never mutated again after that point, so no
    // locking is needed around the map structure, only around each entry's
    // contents.
    std::map<QString, std::unique_ptr<PendingDeckEvents>> m_pendingDeckEvents;
    PendingErrorEvent m_pendingStreamError;
    PendingErrorEvent m_pendingBackupStreamError;

    // Dead-air detection state (UI-thread-only — checkDeadAir() only ever
    // runs from onPositionTimerTick()). qint64 timestamps are steady-clock
    // milliseconds (see MixEngine.cpp's steadyClockNowMs() for the same
    // "monotonic, immune to wall-clock adjustment" reasoning) rather than
    // QDateTime — compared only against themselves, never displayed.
    double m_deadAirThresholdSeconds = 8.0;
    qint64 m_silenceStartMs = -1; // -1 while not currently silent
    bool m_deadAirAlerted = false;
};

} // namespace radio::audio

Q_DECLARE_METATYPE(radio::audio::DeckState)
