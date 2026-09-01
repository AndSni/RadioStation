#include "GstSourcePipeline.h"
#include "RingBuffer.h"

#include "core/Logging.h"

#include <gst/app/gstappsink.h>

namespace radio::audio {

namespace {
// GstAppLeakyType::GST_APP_LEAKY_TYPE_DOWNSTREAM — drop the OLDEST queued
// buffer to make room for new data once max-buffers is reached, rather
// than rejecting the new buffer. Preferred here: we always want the
// freshest decoded audio, not to stall the decode chain waiting for a
// real-time consumer that might be running behind.
constexpr int kAppLeakyTypeDownstream = 2;
} // namespace

GstSourcePipeline::GstSourcePipeline() = default;

GstSourcePipeline::~GstSourcePipeline()
{
    if (m_pipeline) {
        // Deliberately NOT calling g_source_remove(m_busWatchId) here: the
        // watch is tied to the pipeline's bus, which gst_object_unref()
        // below tears down and already invalidates the source — removing
        // it explicitly first races with that and logs a harmless but
        // noisy "Source ID not found" GLib-CRITICAL (confirmed empirically;
        // matches the same reasoning already documented for the shared
        // pipeline's bus watch in AudioEngine::shutdown()).
        m_busWatchId = 0;
        if (m_eosPollId != 0) {
            g_source_remove(m_eosPollId);
            m_eosPollId = 0;
        }
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        gst_object_unref(m_pipeline);
        m_pipeline = nullptr;
    }
}

bool GstSourcePipeline::build()
{
    m_pipeline = gst_pipeline_new(nullptr);

    m_uridecodebin = gst_element_factory_make("uridecodebin", nullptr);
    m_audioconvert = gst_element_factory_make("audioconvert", nullptr);
    m_audioresample = gst_element_factory_make("audioresample", nullptr);
    m_capsfilter = gst_element_factory_make("capsfilter", nullptr);
    m_appsink = gst_element_factory_make("appsink", nullptr);

    if (!m_uridecodebin || !m_audioconvert || !m_audioresample || !m_capsfilter || !m_appsink) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("GstSourcePipeline: failed to create GStreamer elements"));
        return false;
    }

    GstCaps* caps = gst_caps_new_simple("audio/x-raw", "format", G_TYPE_STRING, "F32LE", "rate", G_TYPE_INT,
        kSampleRate, "channels", G_TYPE_INT, kChannels, nullptr);
    g_object_set(m_capsfilter, "caps", caps, nullptr);
    // Fixed caps on the appsink side too — avoids on-the-fly renegotiation
    // stalls; the capsfilter just upstream guarantees the decode chain
    // always converts to exactly this format before reaching it.
    g_object_set(m_appsink, "caps", caps, nullptr);
    gst_caps_unref(caps);

    // sync=TRUE: pace buffer delivery to the pipeline's own clock (defaults
    // to GstSystemClock — wall-clock time — since nothing else provides
    // one), matching real playback speed. This was originally set to FALSE
    // on the theory that miniaudio's device callback is the only real clock
    // that matters and decode should just run flat-out into the ring
    // buffer — but that's wrong: local file decode is routinely tens of
    // times faster than real-time and unpaced appsink will decode an
    // entire multi-minute track in a couple of seconds. Confirmed
    // empirically (a real MP3 audibly cut off after ~1s): the ring
    // buffer's ~1s capacity filled almost instantly and every buffer
    // decoded after that point was silently dropped by write()'s overflow
    // handling, so only the first ~1s of the file ever reached the
    // speakers before GStreamer posted EOS (having "finished" decoding a
    // track most of which was thrown away). sync=TRUE makes GStreamer's
    // own well-tested clock/preroll machinery pace decode to real-time —
    // the ring buffer goes back to being genuine jitter headroom instead
    // of the only thing standing between decode and total data loss.
    // emit-signals+new-sample: the real-time-safe consumption pattern (see
    // onNewSample) — never gst_app_sink_pull_sample() on a blocking basis
    // from anywhere that might touch a real-time thread.
    g_object_set(m_appsink, "emit-signals", TRUE, "sync", TRUE, "max-buffers", 8, "leaky-type",
        kAppLeakyTypeDownstream, nullptr);
    g_signal_connect(m_appsink, "new-sample", G_CALLBACK(&GstSourcePipeline::onNewSample), this);

    // seek()'s flushing seek needs the ring buffer cleared of anything
    // buffered from before the seek — but clearing it directly from seek()
    // (engine thread) would race a concurrent onNewSample() write on the
    // GStreamer streaming thread, since a flushing seek doesn't guarantee
    // the flush has actually stopped that thread before
    // gst_element_seek_simple() returns. A pad probe for flush events runs
    // synchronously IN-ORDER with buffer delivery on this same pad/thread,
    // so clearing on GST_EVENT_FLUSH_STOP here is a same-thread operation
    // relative to onNewSample()'s writes — no race, no cross-thread
    // synchronization needed.
    GstPad* appsinkSinkPad = gst_element_get_static_pad(m_appsink, "sink");
    gst_pad_add_probe(appsinkSinkPad, GST_PAD_PROBE_TYPE_EVENT_FLUSH, &GstSourcePipeline::onFlushProbe, this, nullptr);
    gst_object_unref(appsinkSinkPad);

    gst_bin_add_many(
        GST_BIN(m_pipeline), m_uridecodebin, m_audioconvert, m_audioresample, m_capsfilter, m_appsink, nullptr);
    if (!gst_element_link_many(m_audioconvert, m_audioresample, m_capsfilter, m_appsink, nullptr)) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("GstSourcePipeline: failed to link static chain"));
        return false;
    }

    g_signal_connect(m_uridecodebin, "pad-added", G_CALLBACK(&GstSourcePipeline::onPadAdded), this);

    m_bus = gst_pipeline_get_bus(GST_PIPELINE(m_pipeline));
    m_busWatchId = gst_bus_add_watch(m_bus, &GstSourcePipeline::onBusMessage, this);
    gst_object_unref(m_bus);
    m_bus = nullptr;

    // 50ms is comfortably tighter than any audio callback period in
    // practice — genuine playback completion is detected within one or two
    // ticks of the ring buffer actually running dry.
    m_eosPollId = g_timeout_add(50, &GstSourcePipeline::onEosPollTimeout, this);

    // ~1 second of headroom at 48kHz — generous relative to a real-time
    // audio callback's typical read size (tens of milliseconds), so a
    // brief GStreamer scheduling hiccup can't starve the consumer.
    m_ringBuffer = std::make_unique<RingBuffer>(static_cast<size_t>(kSampleRate), kChannels);

    return true;
}

void GstSourcePipeline::loadUri(const QString& uri)
{
    ++m_loadGeneration;
    if (m_prerollPending) {
        // A previous load's PAUSED transition hasn't settled yet. Changing
        // uridecodebin's "uri" property while it's still mid-preroll for the
        // OLD uri does NOT reliably retarget an already-spun-up decode chain
        // -- confirmed empirically (DeckEngineStressTest::
        // rapidReloadSettlesOnTheLatestTrackNotAStaleOne caught this: without
        // the reset below, the pipeline kept decoding the stale track).
        // Force a clean teardown first -- going DOWN in state is always
        // synchronous in GStreamer (same reasoning as stop()/unload()), so
        // this doesn't reintroduce the blocking-wait problem this whole
        // async gate exists to avoid.
        gst_element_set_state(m_pipeline, GST_STATE_NULL);
        m_prerollPending = false;
        m_pendingPlayAfterLoad = false;
    }
    g_object_set(m_uridecodebin, "uri", uri.toUtf8().constData(), nullptr);
    if (m_ringBuffer)
        m_ringBuffer->clear();
    m_framesConsumed.store(0, std::memory_order_relaxed);
    m_eosPendingDrain.store(false, std::memory_order_relaxed);
    m_eosReadyToFire.store(false, std::memory_order_relaxed);
    m_hasReplayGain.store(false, std::memory_order_relaxed);
    m_replayGainDb.store(0.0, std::memory_order_relaxed);
    m_replayGainPeak.store(1.0, std::memory_order_relaxed);

    // NULL/READY -> PAUSED is a real preroll (typefind, decode a first
    // buffer) and is asynchronous — gst_element_set_state() returning here
    // doesn't mean it's actually reached PAUSED yet. AudioEngine dispatches
    // loadTrack() and play() as two SEPARATE engine-thread invokes (e.g.
    // CrossfadeController's queue bootstrap does exactly this back-to-back,
    // and CartSlotPlayer::start() does it in the same stack frame), so
    // nothing else guarantees play() isn't issued before preroll settles.
    // This USED TO be handled by blocking here with gst_element_get_state()
    // — but that ran on GstEngineThread's single shared GMainContext, which
    // every other pipeline's bus watch also depends on, so a slow-to-preroll
    // file (cold disk cache, network mount, corrupt header) stalled bus
    // dispatch for every deck, every cart, and the stream encoder for up to
    // 5 real seconds. Instead: if the transition comes back ASYNC, just
    // remember that (m_prerollPending) and let onBusMessage()'s
    // GST_MESSAGE_ASYNC_DONE case (below) resolve it non-blockingly once
    // GStreamer itself reports the transition genuinely settled — a
    // follow-up play() while that's still pending is deferred via
    // m_pendingPlayAfterLoad rather than issued immediately. m_loadGeneration
    // guards against a stale ASYNC_DONE from a superseded load (a second
    // loadUri() issued before the first one prerolled) resolving the wrong
    // one.
    const GstStateChangeReturn ret = gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
    if (ret == GST_STATE_CHANGE_ASYNC) {
        m_prerollPending = true;
        m_prerollGeneration = m_loadGeneration;
    }
}

void GstSourcePipeline::play()
{
    if (m_prerollPending) {
        // Preroll from the most recent loadUri() hasn't settled yet — defer
        // PLAYING until onBusMessage()'s ASYNC_DONE handler resolves it,
        // rather than stacking a new target state on top of the unfinished
        // transition.
        m_pendingPlayAfterLoad = true;
        return;
    }
    gst_element_set_state(m_pipeline, GST_STATE_PLAYING);
}

void GstSourcePipeline::pause()
{
    m_pendingPlayAfterLoad = false; // a pause requested after play() shouldn't be clobbered once preroll resolves
    gst_element_set_state(m_pipeline, GST_STATE_PAUSED);
}

void GstSourcePipeline::stop()
{
    // This is exactly the kind of state-lowering call that used to be able
    // to deadlock when the bin was linked into a shared live aggregator —
    // here it's operating on this pipeline's own, fully independent
    // GstPipeline with nothing else attached to it, so it's the ordinary,
    // well-supported case.
    m_prerollPending = false;
    m_pendingPlayAfterLoad = false;
    gst_element_set_state(m_pipeline, GST_STATE_READY);
}

void GstSourcePipeline::unload()
{
    m_prerollPending = false;
    m_pendingPlayAfterLoad = false;
    gst_element_set_state(m_pipeline, GST_STATE_NULL);
    if (m_ringBuffer)
        m_ringBuffer->clear();
    m_framesConsumed.store(0, std::memory_order_relaxed);
    m_eosPendingDrain.store(false, std::memory_order_relaxed);
    m_eosReadyToFire.store(false, std::memory_order_relaxed);
    m_hasReplayGain.store(false, std::memory_order_relaxed);
    m_replayGainDb.store(0.0, std::memory_order_relaxed);
    m_replayGainPeak.store(1.0, std::memory_order_relaxed);
}

void GstSourcePipeline::seek(qint64 positionMs)
{
    // Not currently called back-to-back with loadUri() by any caller (only
    // AudioEngine::seek(), driven by the UI scrubber, well after a track is
    // already loaded) — seeking while a preroll from loadUri() is still
    // pending (m_prerollPending) is an unexercised edge case, not something
    // this handles specially.
    gst_element_seek_simple(m_pipeline, GST_FORMAT_TIME,
        static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT), positionMs * GST_MSECOND);
    // Ring buffer is cleared from onFlushProbe() (see build()), which runs
    // on the same GStreamer streaming thread that onNewSample() writes
    // from — not here, which would race a concurrent write from that
    // thread against RingBuffer::clear()'s unsynchronized reset.
    // Reflect the seek target immediately rather than waiting for freshly-
    // decoded post-seek frames to actually flow through read() — matches
    // what a caller querying position() right after seek() would expect.
    m_framesConsumed.store(
        static_cast<uint64_t>((positionMs * kSampleRate) / 1000), std::memory_order_relaxed);
}

qint64 GstSourcePipeline::positionMs() const
{
    return static_cast<qint64>(m_framesConsumed.load(std::memory_order_relaxed) * 1000 / kSampleRate);
}

qint64 GstSourcePipeline::durationMs() const
{
    gint64 dur = -1;
    if (!gst_element_query_duration(m_pipeline, GST_FORMAT_TIME, &dur) || dur < 0)
        return -1;
    return dur / GST_MSECOND;
}

GstState GstSourcePipeline::gstState() const
{
    GstState state = GST_STATE_NULL;
    GstState pending = GST_STATE_NULL;
    gst_element_get_state(m_pipeline, &state, &pending, 100 * GST_MSECOND);
    return state;
}

bool GstSourcePipeline::hasReplayGain() const
{
    // Acquire pairs with the release store in onBusMessage()'s
    // GST_MESSAGE_TAG case — seeing true here guarantees replayGainDb()/
    // replayGainPeak() also observe that same tag's values, not a stale
    // default from before it arrived.
    return m_hasReplayGain.load(std::memory_order_acquire);
}

double GstSourcePipeline::replayGainDb() const
{
    return m_replayGainDb.load(std::memory_order_relaxed);
}

double GstSourcePipeline::replayGainPeak() const
{
    return m_replayGainPeak.load(std::memory_order_relaxed);
}

size_t GstSourcePipeline::read(float* out, size_t frameCount)
{
    if (!m_ringBuffer)
        return 0;
    const size_t got = m_ringBuffer->read(out, frameCount);
    // Counts only frames actually delivered, not frames requested — an
    // underrun (rare, given ~1s of ring-buffer headroom) means real
    // silence was heard, not further progress into the track's content.
    m_framesConsumed.fetch_add(got, std::memory_order_relaxed);

    // See m_eosReadyToFire's doc comment (GstSourcePipeline.h): only once
    // the ring buffer has genuinely run dry AFTER GStreamer posted EOS does
    // this actually mean "finished playing", not just "finished decoding".
    // Pure atomic ops only — this runs on the real-time audio thread.
    if (m_eosPendingDrain.load(std::memory_order_acquire) && m_ringBuffer->availableToRead() == 0)
        m_eosReadyToFire.store(true, std::memory_order_release);

    return got;
}

void GstSourcePipeline::onPadAdded(GstElement* /*element*/, GstPad* pad, gpointer userData)
{
    auto* self = static_cast<GstSourcePipeline*>(userData);

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);

    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* name = gst_structure_get_name(structure);
    const bool isAudio = name && g_str_has_prefix(name, "audio/");
    gst_caps_unref(caps);

    if (!isAudio)
        return;

    GstPad* sinkPad = gst_element_get_static_pad(self->m_audioconvert, "sink");
    if (!gst_pad_is_linked(sinkPad)) {
        const GstPadLinkReturn ret = gst_pad_link(pad, sinkPad);
        if (ret != GST_PAD_LINK_OK)
            RS_LOG_ERROR("audio.pipeline", QStringLiteral("GstSourcePipeline: pad-added link failed (%1)").arg(ret));
    }
    gst_object_unref(sinkPad);
}

GstPadProbeReturn GstSourcePipeline::onFlushProbe(GstPad* /*pad*/, GstPadProbeInfo* info, gpointer userData)
{
    auto* self = static_cast<GstSourcePipeline*>(userData);
    GstEvent* event = gst_pad_probe_info_get_event(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_FLUSH_STOP && self->m_ringBuffer)
        self->m_ringBuffer->clear();
    return GST_PAD_PROBE_OK;
}

GstFlowReturn GstSourcePipeline::onNewSample(GstElement* sink, gpointer userData)
{
    auto* self = static_cast<GstSourcePipeline*>(userData);

    GstSample* sample = gst_app_sink_pull_sample(GST_APP_SINK(sink));
    if (!sample)
        return GST_FLOW_OK; // EOS reached while pulling — the bus EOS message handles the rest

    GstBuffer* buffer = gst_sample_get_buffer(sample);
    GstMapInfo map;
    if (buffer && gst_buffer_map(buffer, &map, GST_MAP_READ)) {
        const size_t frameCount = map.size / (sizeof(float) * static_cast<size_t>(kChannels));
        if (self->m_ringBuffer && frameCount > 0)
            self->m_ringBuffer->write(reinterpret_cast<const float*>(map.data), frameCount);
        gst_buffer_unmap(buffer, &map);
    }
    gst_sample_unref(sample);

    return GST_FLOW_OK;
}

gboolean GstSourcePipeline::onBusMessage(GstBus* /*bus*/, GstMessage* message, gpointer userData)
{
    auto* self = static_cast<GstSourcePipeline*>(userData);

    switch (GST_MESSAGE_TYPE(message)) {
    case GST_MESSAGE_EOS:
        // Deferred — see m_eosPendingDrain's doc comment. onEos() itself
        // fires later, from onEosPollTimeout(), once playback has actually
        // finished rather than merely decode.
        self->m_eosPendingDrain.store(true, std::memory_order_release);
        break;
    case GST_MESSAGE_ERROR: {
        GError* err = nullptr;
        gchar* debug = nullptr;
        gst_message_parse_error(message, &err, &debug);
        const QString text = QString::fromUtf8(err->message);
        const QString debugText = debug ? QString::fromUtf8(debug) : QString();
        g_clear_error(&err);
        g_free(debug);
        // Unconditional watchdog reset (Phase 2): GStreamer does NOT
        // automatically revert to NULL on ANY error, load-time or
        // mid-playback (a decoder choking partway through a file, an
        // unexpected internal error) — left alone, gstState() would keep
        // reporting something other than NULL indefinitely, so DeckState
        // never drops back to Null and CrossfadeController's idle-refill
        // watch (which only re-triggers on DeckState::Null) never notices
        // anything went wrong, leaving the deck stuck broken until an
        // operator manually stops/unloads it. Resetting to NULL here is
        // what lets the existing, already-proven idle-refill watch and Auto
        // DJ automatically cue a new track — routing around the failure via
        // machinery that already exists, rather than retrying the same
        // (possibly permanently bad) file in a loop. (Originally scoped to
        // load-time-only in Phase 1, when only the synchronous preroll
        // failure path existed; Phase 2's watchdog work extends it to every
        // error for exactly this reason.)
        if (self->m_prerollPending) {
            self->m_prerollPending = false;
            self->m_pendingPlayAfterLoad = false;
        }
        gst_element_set_state(self->m_pipeline, GST_STATE_NULL);
        if (self->onError)
            self->onError(text, debugText);
        break;
    }
    case GST_MESSAGE_TAG: {
        // GStreamer's demuxers (id3demux etc.) populate these standard tag
        // names automatically from a file's own ReplayGain frames
        // (confirmed against a real library file's TXXX:REPLAYGAIN_TRACK_
        // GAIN/_PEAK frames) — no custom tag parsing needed. Tags can
        // arrive incrementally across multiple messages; each one only
        // updates whichever fields it actually carries.
        GstTagList* tags = nullptr;
        gst_message_parse_tag(message, &tags);
        if (tags) {
            gdouble gainDb = 0.0;
            const bool gotGain = gst_tag_list_get_double(tags, GST_TAG_TRACK_GAIN, &gainDb);
            if (gotGain) {
                self->m_replayGainDb.store(gainDb, std::memory_order_relaxed);
                self->m_hasReplayGain.store(true, std::memory_order_release);
            }
            gdouble peak = 0.0;
            if (gst_tag_list_get_double(tags, GST_TAG_TRACK_PEAK, &peak))
                self->m_replayGainPeak.store(peak, std::memory_order_relaxed);
            gst_tag_list_unref(tags);

            if (gotGain && self->onReplayGainChanged)
                self->onReplayGainChanged();
        }
        break;
    }
    case GST_MESSAGE_STATE_CHANGED: {
        // Only this pipeline's own top-level transition is interesting —
        // every internal element (inside uridecodebin's decode graph etc.)
        // also posts STATE_CHANGED, which we don't want to surface.
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(self->m_pipeline)) {
            GstState oldState, newState, pending;
            gst_message_parse_state_changed(message, &oldState, &newState, &pending);
            if (self->onStateChanged)
                self->onStateChanged(newState);
        }
        break;
    }
    case GST_MESSAGE_ASYNC_DONE: {
        // GstBin (GstPipeline's base) aggregates every child element's
        // async completion into exactly one ASYNC_DONE on the pipeline
        // itself — GStreamer's canonical "the async transition I started
        // has now genuinely settled" signal, and what resolves
        // m_prerollPending non-blockingly (see loadUri()'s doc comment).
        if (GST_MESSAGE_SRC(message) == GST_OBJECT(self->m_pipeline) && self->m_prerollPending
            && self->m_prerollGeneration == self->m_loadGeneration) {
            self->m_prerollPending = false;
            if (self->m_pendingPlayAfterLoad) {
                self->m_pendingPlayAfterLoad = false;
                gst_element_set_state(self->m_pipeline, GST_STATE_PLAYING);
            }
        }
        break;
    }
    default:
        break;
    }

    return TRUE;
}

gboolean GstSourcePipeline::onEosPollTimeout(gpointer userData)
{
    auto* self = static_cast<GstSourcePipeline*>(userData);
    if (self->m_eosReadyToFire.exchange(false, std::memory_order_acq_rel)) {
        self->m_eosPendingDrain.store(false, std::memory_order_relaxed);
        if (self->onEos)
            self->onEos();
    }
    return G_SOURCE_CONTINUE;
}

} // namespace radio::audio
