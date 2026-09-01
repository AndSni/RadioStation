#pragma once

#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <gst/gst.h>

namespace radio::audio {

class RingBuffer;

// One independent, self-contained GStreamer decode pipeline:
// uridecodebin -> audioconvert -> audioresample -> capsfilter(F32LE,48000,2)
// -> appsink. Deliberately NOT a bin attached into any shared parent
// pipeline — this is the whole point of this class. The previous design
// (per-deck GstBins dynamically attached/detached into one shared,
// always-running GstPipeline with a live audiomixer) hit a confirmed,
// unresolved GStreamer deadlock: gst_element_set_state() on a bin linked
// into a live aggregator can deadlock inside GStreamer's own
// STATE_LOCK/STREAM_LOCK ordering machinery (GitLab gstreamer#349, #91).
// A fully independent linear pipeline going through
// PLAYING<->PAUSED<->READY<->NULL is GStreamer's single most common, most
// tested use case, with no shared aggregator for its state changes to
// ever contend with — this class's independence from every other instance
// is what eliminates that deadlock's precondition entirely.
//
// Decoded PCM is pulled out via a lock-free RingBuffer (see RingBuffer.h)
// fed by appsink's "new-sample" callback (GStreamer's own streaming
// thread). read() is real-time-safe: callable from an audio device
// callback thread without ever blocking or touching GStreamer's API.
//
// All methods (other than read(), positionMs(), durationMs(), gstState() —
// documented by GStreamer as thread-safe) must run on the engine thread
// (i.e. from inside an AudioEngine/GstEngineThread::invoke lambda), same
// contract as DeckEngine.
class GstSourcePipeline {
public:
    GstSourcePipeline();
    ~GstSourcePipeline();

    // Must be called once, on the engine thread, before any other method.
    // The bus watch this installs attaches to the calling thread's current
    // GLib thread-default context (GstEngineThread's run() pushes its own
    // context as thread-default for exactly this reason) — no explicit
    // context plumbing needed, matching how the single shared pipeline
    // used to register its bus watch.
    bool build();

    void loadUri(const QString& uri);
    void play();
    void pause();
    void stop(); // -> READY, track stays loaded; a subsequent play() restarts from 0
    void unload(); // -> NULL; reports as "nothing cued" until loadUri() is called again
    void seek(qint64 positionMs);

    // Reports playback position based on frames actually CONSUMED via
    // read() (see below), not GStreamer's own internal decode position.
    // This pipeline's appsink runs with sync=FALSE — decode is
    // deliberately unpaced by any clock (miniaudio's real-time thread is
    // the only real clock in this architecture now), so GStreamer can
    // decode an entire short file into the ring buffer in a fraction of a
    // real second. Querying gst_element_query_position() here would
    // report how far decoding has gotten, not how much has actually been
    // audibly played — confirmed empirically (a track's reported position
    // jumped to equal its duration within one 200ms tick of starting).
    qint64 positionMs() const;
    qint64 durationMs() const; // static file property — decode speed doesn't affect this, queried from GStreamer directly
    GstState gstState() const;

    // Real-time-safe. Pulls up to frameCount interleaved frames (channels()
    // channels each) of decoded PCM into out. Returns frames actually
    // available (may be less than requested, including 0 — caller fills
    // any shortfall with silence itself). Never blocks.
    size_t read(float* out, size_t frameCount);

    static constexpr int channels() { return kChannels; }
    static constexpr int sampleRate() { return kSampleRate; }

    // ReplayGain metadata (GST_TAG_TRACK_GAIN/GST_TAG_TRACK_PEAK), extracted
    // from GST_MESSAGE_TAG bus messages as they arrive — GStreamer's ID3
    // (and other format) demuxers populate these automatically from
    // standard TXXX:REPLAYGAIN_TRACK_GAIN/_PEAK frames, no custom tag
    // parsing needed. hasReplayGain() is false until a tag with these
    // fields actually arrives (most files may have none at all). Safe to
    // call from any thread — plain atomic loads, same contract as
    // positionMs()/durationMs()/gstState().
    bool hasReplayGain() const;
    double replayGainDb() const;
    double replayGainPeak() const;

    // Invoked from the engine thread (bus-watch dispatch) on EOS / a
    // pipeline ERROR. Set by the owner (DeckEngine/CartSlotPlayer) before
    // any state-changing call; plain std::function callbacks rather than
    // Qt signals, matching DeckEngine's existing non-QObject convention —
    // this is a low-level implementation class, not a UI-facing one.
    std::function<void()> onEos;
    std::function<void(QString message, QString debug)> onError;
    // Fired only for this pipeline's own top-level state transitions (not
    // its internal elements) — simpler than the old shared-pipeline design
    // needed, since there's no longer any sibling bin traffic to filter out.
    std::function<void(GstState newState)> onStateChanged;
    // Fired after hasReplayGain()/replayGainDb()/replayGainPeak() are
    // updated from a tag message that actually carried GST_TAG_TRACK_GAIN —
    // lets an owner (DeckEngine) recompute anything derived from these the
    // moment they become known, rather than needing to poll.
    std::function<void()> onReplayGainChanged;

private:
    static void onPadAdded(GstElement* element, GstPad* pad, gpointer userData);
    static GstFlowReturn onNewSample(GstElement* sink, gpointer userData);
    static gboolean onBusMessage(GstBus* bus, GstMessage* message, gpointer userData);
    static gboolean onEosPollTimeout(gpointer userData);
    // Runs on the GStreamer streaming thread, synchronously in-order with
    // onNewSample() on the same pad — see build()'s doc comment on why the
    // ring buffer is cleared here rather than from seek() on the engine
    // thread.
    static GstPadProbeReturn onFlushProbe(GstPad* pad, GstPadProbeInfo* info, gpointer userData);

    static constexpr int kChannels = 2;
    static constexpr int kSampleRate = 48000;

    GstElement* m_pipeline = nullptr;
    GstElement* m_uridecodebin = nullptr;
    GstElement* m_audioconvert = nullptr;
    GstElement* m_audioresample = nullptr;
    GstElement* m_capsfilter = nullptr;
    GstElement* m_appsink = nullptr;
    GstBus* m_bus = nullptr;
    guint m_busWatchId = 0;
    // Polls for genuine playback completion (see m_eosReadyToFire below) on
    // the engine thread's own GMainContext — independent of the bus watch,
    // so removed explicitly in the destructor rather than relying on
    // gst_object_unref() to invalidate it (that trick only applies to
    // bus-tied sources).
    guint m_eosPollId = 0;
    std::unique_ptr<RingBuffer> m_ringBuffer;

    // Incremented by read() with every frame actually handed to the
    // real-time consumer — the basis for positionMs(). Written from
    // whatever thread calls read() (the real-time audio thread in
    // practice), read from positionMs() (documented safe from any
    // thread) — a plain atomic counter needs no further synchronization
    // for this single-writer/multi-reader use.
    std::atomic<uint64_t> m_framesConsumed{ 0 };

    // GStreamer posts GST_MESSAGE_EOS the moment DECODING finishes, which
    // (like positionMs() above) is decoupled from actual audible playback
    // now that appsink runs unpaced (sync=FALSE) — confirmed empirically: a
    // short fixture's EOS message arrived within milliseconds of PLAYING,
    // seconds before its audio had actually finished draining out of the
    // ring buffer. Firing onEos() straight from the bus handler would
    // report a track "finished" while it's still audibly playing (and, for
    // CartSlotPlayer specifically, would tear down its pipeline —
    // physically cutting the audio off mid-playback). Instead: the bus
    // handler only sets m_eosPendingDrain (engine thread); read() (the
    // real-time thread) watches for the ring buffer actually running dry
    // while that's set and flags m_eosReadyToFire (a plain atomic write,
    // RT-safe — never calls onEos() itself); onEosPollTimeout (engine
    // thread, via m_eosPollId) is what actually invokes onEos(), preserving
    // every existing caller's assumption that onEos() runs on the engine
    // thread.
    std::atomic<bool> m_eosPendingDrain{ false };
    std::atomic<bool> m_eosReadyToFire{ false };

    // See hasReplayGain()'s doc comment. Written from the bus-message
    // handler (engine thread), read from any thread.
    std::atomic<bool> m_hasReplayGain{ false };
    std::atomic<double> m_replayGainDb{ 0.0 };
    std::atomic<double> m_replayGainPeak{ 1.0 };

    // Async preroll gate (see loadUri()'s doc comment). Plain, non-atomic —
    // every touch point (loadUri/play/pause/stop/unload/onBusMessage) runs
    // exclusively on the engine thread, same contract as m_pipeline itself.
    bool m_prerollPending = false;
    bool m_pendingPlayAfterLoad = false;
    // Incremented at the top of every loadUri(). m_prerollGeneration is
    // stashed at the moment m_prerollPending is set, so a stale
    // GST_MESSAGE_ASYNC_DONE left over from a superseded load (e.g. a rapid
    // second loadUri() issued before the first one prerolled) can be told
    // apart from the one actually being waited on and ignored.
    uint64_t m_loadGeneration = 0;
    uint64_t m_prerollGeneration = 0;
};

} // namespace radio::audio
