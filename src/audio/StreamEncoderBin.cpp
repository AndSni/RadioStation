#include "StreamEncoderBin.h"
#include "MixEngine.h"

#include "core/Logging.h"

#include <gst/app/gstappsrc.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iterator>
#include <thread>
#include <vector>

namespace radio::audio {

namespace {
QString formatName(StreamEncoderBin::Format format)
{
    switch (format) {
    case StreamEncoderBin::Format::Mp3:
        return QStringLiteral("MP3");
    case StreamEncoderBin::Format::Ogg:
        return QStringLiteral("Ogg");
    case StreamEncoderBin::Format::Opus:
        return QStringLiteral("Opus");
    }
    return QString();
}
}

StreamEncoderBin::StreamEncoderBin(MixEngine* mixEngine, MountRole role)
    : m_mixEngine(mixEngine)
    , m_role(role)
{
}

StreamEncoderBin::~StreamEncoderBin()
{
    disconnect();
}

void StreamEncoderBin::connect(const Config& config)
{
    cancelScheduledReconnect();
    // Checked against m_pipeline, not m_connected: after an error,
    // scheduleReconnect() flips m_connected false immediately but leaves
    // the (broken) pipeline object alive until its deferred teardown runs —
    // connect()/disconnect() can land in that window and must still clean
    // up the stale pipeline, not skip it because m_connected already reads
    // false.
    if (m_pipeline)
        teardown(); // always tear down any existing stream first, hot-swap style
    m_userRequestedDisconnect = false;
    m_reconnectAttempt = 0; // a fresh, explicit connect always starts the backoff schedule over
    m_config = config;
    buildAndStart();
}

void StreamEncoderBin::disconnect()
{
    m_userRequestedDisconnect = true;
    cancelScheduledReconnect();
    if (!m_pipeline) // see connect()'s comment on why this checks m_pipeline, not m_connected
        return;
    teardown();
    RS_LOG_INFO("streaming.encoder", QStringLiteral("Streaming disconnected"));
}

void StreamEncoderBin::buildAndStart()
{
    m_pipeline = gst_pipeline_new("stream-pipeline");
    m_appsrc = gst_element_factory_make("appsrc", "stream-src");
    GstElement* convert = gst_element_factory_make("audioconvert", "stream-convert");
    GstElement* resample = gst_element_factory_make("audioresample", "stream-resample");
    GstElement* encoder = nullptr;
    GstElement* muxer = nullptr;
    switch (m_config.format) {
    case Format::Mp3:
        encoder = gst_element_factory_make("lamemp3enc", "stream-encoder");
        break;
    case Format::Ogg:
        encoder = gst_element_factory_make("vorbisenc", "stream-encoder");
        muxer = gst_element_factory_make("oggmux", "stream-muxer");
        break;
    case Format::Opus:
        // shout2send's sink caps only accept Ogg-family, MP3, and WebM
        // (confirmed via gst-inspect-1.0 shout2send -- no raw audio/x-opus
        // entry, and no audio/mpeg,mpegversion=4 either, which is why AAC
        // was descoped from this app entirely rather than just picking an
        // encoder) -- so Opus goes through the same oggmux path Vorbis
        // already does, just swapping the encoder element.
        encoder = gst_element_factory_make("opusenc", "stream-encoder");
        muxer = gst_element_factory_make("oggmux", "stream-muxer");
        break;
    }
    GstElement* sink = gst_element_factory_make("shout2send", "stream-sink");

    const bool missingElement = !m_appsrc || !convert || !resample || !encoder || !sink
        || (m_config.format != Format::Mp3 && !muxer);
    if (missingElement) {
        RS_LOG_ERROR("streaming.encoder",
            QStringLiteral("Failed to create streaming elements (missing GStreamer plugin?)"));
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
        m_appsrc = nullptr;
        return;
    }

    // Fixed caps matching MixEngine's own format exactly — avoids
    // renegotiation stalls, same reasoning as GstSourcePipeline's appsink
    // caps on the decode side.
    GstCaps* caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE", "rate", G_TYPE_INT,
        MixEngine::sampleRate(), "channels", G_TYPE_INT, MixEngine::channels(), nullptr);
    g_object_set(m_appsrc, "caps", caps, "format", GST_FORMAT_TIME, "is-live", TRUE, "block", TRUE, nullptr);
    gst_caps_unref(caps);

    // async=FALSE: shout2send is a live network sink, not a preroll-exact
    // media sink — without this, its PAUSED transition waits on a preroll
    // buffer that may not arrive promptly, observed as an intermittent
    // multi-second-plus stall reaching PLAYING (and thus any connection
    // error not surfacing until it does).
    g_object_set(sink, "async", FALSE, "ip", m_config.host.toUtf8().constData(), "port", m_config.port, "mount",
        m_config.mount.toUtf8().constData(), "username", m_config.username.toUtf8().constData(), "password",
        m_config.password.toUtf8().constData(), "streamname", m_config.streamName.toUtf8().constData(), "genre",
        m_config.genre.toUtf8().constData(), "description", m_config.description.toUtf8().constData(), nullptr);

    if (m_config.format == Format::Mp3) {
        gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, convert, resample, encoder, sink, nullptr);
        gst_element_link_many(m_appsrc, convert, resample, encoder, sink, nullptr);
    } else {
        gst_bin_add_many(GST_BIN(m_pipeline), m_appsrc, convert, resample, encoder, muxer, sink, nullptr);
        gst_element_link_many(m_appsrc, convert, resample, encoder, muxer, sink, nullptr);
    }

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_busWatchId = gst_bus_add_watch(m_bus, &StreamEncoderBin::onBusMessage, this);
    gst_object_unref(m_bus);
    m_bus = nullptr;

    // Start the feeder BEFORE setting PLAYING: appsrc needs buffers
    // arriving promptly for the PAUSED->PLAYING preroll to resolve
    // quickly, same "don't leave anything waiting on data that isn't
    // flowing yet" concern as elsewhere in this rewrite.
    m_feederRunning.store(true, std::memory_order_relaxed);
    m_feederThread = std::thread(&StreamEncoderBin::feederLoop, this);

    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
    m_connected = true;

    // A connection that holds for a full minute without another error is
    // "stable" -- resets the backoff schedule back to its fast end (see
    // onStabilityTimeout()), so a connection that's genuinely recovered
    // doesn't stay parked at a slow retry interval forever because of one
    // old outage.
    if (m_stabilityTimeoutId == 0)
        m_stabilityTimeoutId = g_timeout_add(60000, &StreamEncoderBin::onStabilityTimeout, this);

    RS_LOG_INFO("streaming.encoder",
        QStringLiteral("Streaming (%5) connecting to %1:%2%3 (%4)")
            .arg(m_config.host)
            .arg(m_config.port)
            .arg(m_config.mount, formatName(m_config.format), m_role == MountRole::Primary ? QStringLiteral("primary") : QStringLiteral("backup")));
}

void StreamEncoderBin::teardown()
{
    m_connected = false;
    m_feederRunning.store(false, std::memory_order_relaxed);

    if (m_pipeline) {
        // Set to NULL BEFORE joining the feeder thread below — a real
        // deadlock, not just a hypothetical: the feeder can be blocked
        // inside gst_app_src_push_buffer() (appsrc's "block" property is
        // TRUE) if downstream isn't consuming, e.g. shout2send stalled
        // against a dead/erroring connection. Setting NULL flushes appsrc
        // and unblocks that call; joining first (the original ordering)
        // waits forever for a thread that can only exit once this runs —
        // confirmed by catching it live: Phase 2's reconnect logic calls
        // teardown() far more readily than before (every error, not just
        // an explicit disconnect()), which made this ordering bug hang the
        // WHOLE engine thread (this runs from onReconnectTimeout(), a GLib
        // timeout ON that thread) reliably enough to catch in
        // automaticallyRetriesAfterConnectionError. appsrc/pipeline state
        // changes are safe to issue concurrently with the feeder thread's
        // in-flight push — that's the whole point of appsrc as a
        // cross-thread feed mechanism.
        //
        // Deliberately NOT calling g_source_remove(m_busWatchId) — matches
        // GstSourcePipeline's destructor: gst_object_unref() below tears
        // down the bus and already invalidates the watch source, and
        // removing it explicitly first races with that (confirmed
        // empirically elsewhere in this rewrite — logs a harmless but
        // noisy GLib-CRITICAL).
        m_busWatchId = 0;
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
    }

    if (m_feederThread.joinable())
        m_feederThread.join();

    if (m_pipeline) {
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
    m_appsrc = nullptr;
}

void StreamEncoderBin::feederLoop()
{
    constexpr size_t kChunkFrames = 2048;
    std::vector<float> buffer(kChunkFrames * static_cast<size_t>(MixEngine::channels()));
    GstClockTime runningTime = 0;

    while (m_feederRunning.load(std::memory_order_relaxed)) {
        const size_t got = m_role == MountRole::Primary ? m_mixEngine->readMixedOutput(buffer.data(), kChunkFrames)
                                                          : m_mixEngine->readBackupMixedOutput(buffer.data(), kChunkFrames);
        if (got == 0) {
            // Not real-time-critical here (this is a dedicated feeder
            // thread, not the audio device callback) — a short sleep
            // between empty polls is fine; streaming tolerates a few ms
            // of extra latency that local playback never could.
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        const size_t byteCount = got * static_cast<size_t>(MixEngine::channels()) * sizeof(float);
        GstBuffer* gstBuffer = gst_buffer_new_allocate(nullptr, byteCount, nullptr);
        GstMapInfo map;
        gst_buffer_map(gstBuffer, &map, GST_MAP_WRITE);
        std::memcpy(map.data, buffer.data(), byteCount);
        gst_buffer_unmap(gstBuffer, &map);

        const GstClockTime duration = gst_util_uint64_scale(got, GST_SECOND, static_cast<guint64>(MixEngine::sampleRate()));
        GST_BUFFER_PTS(gstBuffer) = runningTime;
        GST_BUFFER_DURATION(gstBuffer) = duration;
        runningTime += duration;

        gst_app_src_push_buffer(GST_APP_SRC(m_appsrc), gstBuffer); // takes ownership of gstBuffer
    }
}

gboolean StreamEncoderBin::onBusMessage(GstBus* /*bus*/, GstMessage* message, gpointer userData)
{
    auto* self = static_cast<StreamEncoderBin*>(userData);

    if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &err, &debug);
        const QString text = QString::fromUtf8(err->message);
        const QString debugText = debug ? QString::fromUtf8(debug) : QString();
        g_clear_error(&err);
        g_free(debug);
        if (self->onError)
            self->onError(text, debugText);

        if (!self->m_userRequestedDisconnect)
            self->scheduleReconnect();
    }

    return TRUE;
}

void StreamEncoderBin::scheduleReconnect()
{
    if (m_reconnectTimeoutId != 0)
        return; // a reconnect is already scheduled -- don't stack another one on top

    // Report disconnected immediately (a cheap flag flip); the actual
    // GStreamer pipeline teardown is deferred to onReconnectTimeout() below,
    // NOT done here — this runs from inside onBusMessage(), itself a
    // bus-watch callback dispatched BY this same pipeline's own bus.
    // Tearing the pipeline down (gst_element_set_state(NULL) +
    // gst_object_unref()) synchronously from inside that call stack is
    // exactly the "unsafe self-destruction mid-callback" hazard
    // CartSlotPlayer::onEos/onError already document and avoid the same
    // way — deferring to a separate, later GLib source invocation instead.
    m_connected = false;

    if (m_stabilityTimeoutId != 0) {
        g_source_remove(m_stabilityTimeoutId);
        m_stabilityTimeoutId = 0;
    }

    static constexpr int kBackoffScheduleSecs[] = { 2, 4, 8, 16, 30 };
    constexpr int kMaxIndex = static_cast<int>(std::size(kBackoffScheduleSecs)) - 1;
    const int delaySecs = kBackoffScheduleSecs[std::min(m_reconnectAttempt, kMaxIndex)];

    RS_LOG_INFO("streaming.encoder",
        QStringLiteral("Streaming connection lost — reconnecting in %1s (attempt %2)").arg(delaySecs).arg(m_reconnectAttempt + 1));
    m_reconnectTimeoutId = g_timeout_add(static_cast<guint>(delaySecs * 1000), &StreamEncoderBin::onReconnectTimeout, this);
}

void StreamEncoderBin::cancelScheduledReconnect()
{
    if (m_reconnectTimeoutId != 0) {
        g_source_remove(m_reconnectTimeoutId);
        m_reconnectTimeoutId = 0;
    }
    if (m_stabilityTimeoutId != 0) {
        g_source_remove(m_stabilityTimeoutId);
        m_stabilityTimeoutId = 0;
    }
}

gboolean StreamEncoderBin::onReconnectTimeout(gpointer userData)
{
    auto* self = static_cast<StreamEncoderBin*>(userData);
    self->m_reconnectTimeoutId = 0;
    ++self->m_reconnectAttempt;

    // Safe here (unlike in scheduleReconnect()/onBusMessage()): this is a
    // separate GLib timeout source firing later, not nested inside the
    // erroring pipeline's own bus-dispatch call stack.
    if (self->m_pipeline)
        self->teardown();

    if (self->onError) {
        // Reuses the existing onError channel for retry status too (rather
        // than adding a new signal) — MainWindow's streaming status label
        // already displays whatever text arrives here.
        self->onError(QStringLiteral("Reconnecting to streaming server (attempt %1)...").arg(self->m_reconnectAttempt), QString());
    }

    self->buildAndStart();
    return G_SOURCE_REMOVE;
}

gboolean StreamEncoderBin::onStabilityTimeout(gpointer userData)
{
    auto* self = static_cast<StreamEncoderBin*>(userData);
    self->m_stabilityTimeoutId = 0;
    self->m_reconnectAttempt = 0; // held a connection for a full minute straight -- back to the fast end of the backoff schedule if it ever drops again
    return G_SOURCE_REMOVE;
}

} // namespace radio::audio
