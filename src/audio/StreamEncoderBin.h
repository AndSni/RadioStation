#pragma once

#include <QString>

#include <atomic>
#include <functional>
#include <thread>

#include <gst/gst.h>

namespace radio::audio {

class MixEngine;

// Streams MixEngine's final mixed output to an Icecast/Shoutcast server:
// appsrc -> audioconvert -> audioresample -> [lamemp3enc | vorbisenc+
// oggmux] -> shout2send, its own fully independent GstPipeline (same
// reasoning as GstSourcePipeline — no shared aggregator for state changes
// to ever contend with). A dedicated feeder thread pulls mixed PCM via
// MixEngine::readMixedOutput() and pushes it into appsrc.
//
// Unlike the old design (a second branch hot-swapped on the shared tee via
// a blocking pad probe), there's no "other branch" to protect anymore —
// local playback is entirely MixEngine/miniaudio's concern now, completely
// decoupled from this pipeline — so connect()/disconnect() can just
// build/tear down this whole small independent pipeline directly.
class StreamEncoderBin {
public:
    enum class Format { Mp3, Ogg, Opus };

    // Primary reads MixEngine::readMixedOutput()/m_streamingRingBuffer;
    // Backup reads MixEngine::readBackupMixedOutput()/
    // m_backupStreamingRingBuffer -- a genuinely separate ring buffer (see
    // MixEngine's own doc comment: RingBuffer is explicitly single-
    // producer/single-consumer, so two independent feeder threads can't
    // safely share one). Two independent MountRole::Primary/Backup
    // StreamEncoderBin instances, each with their own pipeline/feeder
    // thread/reconnect state, is the whole redundancy mechanism -- no tee
    // or shared-encoder logic here, deliberately (see this class's own
    // doc comment on why the shared-aggregator design was eliminated).
    enum class MountRole { Primary, Backup };

    struct Config {
        QString host = QStringLiteral("localhost");
        int port = 8000;
        QString mount = QStringLiteral("/stream");
        QString username = QStringLiteral("source");
        QString password;
        QString streamName;
        QString genre;
        QString description;
        Format format = Format::Mp3;
    };

    explicit StreamEncoderBin(MixEngine* mixEngine, MountRole role = MountRole::Primary);
    ~StreamEncoderBin();

    void connect(const Config& config);
    void disconnect();
    bool isConnected() const { return m_connected; }
    const Config& config() const { return m_config; }

    // Invoked from the engine thread (bus-watch dispatch) on a pipeline
    // ERROR (e.g. an unreachable server) — both the initial failure AND, if
    // a reconnect attempt is scheduled (see onReconnectTimeout()), a
    // follow-up call with an informative retry message. AudioEngine wires
    // this the same way as GstSourcePipeline's onError, marshaling onto the
    // UI thread; reusing this single existing channel for retry status
    // means MainWindow's streaming status label already shows reconnect
    // progress with no new signal needed.
    std::function<void(QString message, QString debug)> onError;

private:
    void buildAndStart();
    void teardown();
    void feederLoop(); // runs on m_feederThread — pulls from MixEngine, pushes into appsrc
    void scheduleReconnect();
    void cancelScheduledReconnect();

    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer userData);
    static gboolean onReconnectTimeout(gpointer userData);
    static gboolean onStabilityTimeout(gpointer userData);

    MixEngine* m_mixEngine;
    MountRole m_role;
    GstElement* m_pipeline = nullptr;
    GstElement* m_appsrc = nullptr;
    GstBus* m_bus = nullptr;
    guint m_busWatchId = 0;
    Config m_config;
    bool m_connected = false;

    std::atomic<bool> m_feederRunning{ false };
    std::thread m_feederThread;

    // Reconnect-with-backoff (Phase 2): a connection error while the
    // caller still wants to be connected (i.e. not from an explicit
    // disconnect()) schedules an automatic retry instead of just sitting
    // disconnected until an operator notices and reconnects by hand.
    // Engine-thread-only (same contract as the rest of this class) — every
    // touch point runs from either a public method call (already documented
    // as engine-thread-only via AudioEngine's invoke() dispatch) or a bus/
    // GLib-timeout callback on the same thread.
    bool m_userRequestedDisconnect = true; // starts "disconnected on purpose" — not connected yet
    int m_reconnectAttempt = 0;
    guint m_reconnectTimeoutId = 0;
    guint m_stabilityTimeoutId = 0;
};

} // namespace radio::audio
