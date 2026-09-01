#pragma once

#include "MixEngine.h"

#include <QString>

#include <functional>
#include <memory>

#include <gst/gst.h>

namespace radio::audio {

class GstSourcePipeline;

// One playback deck. Composes a GstSourcePipeline (its own fully
// independent GStreamer decode pipeline — see GstSourcePipeline.h for why
// that independence matters) registered once, permanently, as a source in
// the shared MixEngine's mix graph. There is no more "attach/detach into a
// live shared mixer" lifecycle here at all: an unloaded/idle deck simply
// contributes silence (GstSourcePipeline::read() returns 0 frames when
// nothing is decoding, which MixEngine pads with silence) rather than
// needing to be dynamically linked in and out — this is what eliminates
// the whole class of GStreamer STATE_LOCK/STREAM_LOCK deadlock the
// previous design (dynamic bins attached into a live audiomixer) hit.
//
// Volume/gain lives entirely in MixEngine now (per-source gain + ramping),
// not in this class or in GStreamer — setVolume()/rampVolume() delegate to
// MixEngine::setGain().
//
// All methods must be called on the engine thread (i.e. from inside an
// AudioEngine/GstEngineThread::invoke lambda), except positionMs()/
// durationMs()/gstState()/volume(), which are documented safe to call
// directly (same contract as before).
class DeckEngine {
public:
    explicit DeckEngine(QString id);
    ~DeckEngine();

    // Builds the underlying GstSourcePipeline and registers it with
    // mixEngine as a permanent mix-graph source. Must run on the engine
    // thread.
    bool build(MixEngine* mixEngine);

    void loadUri(const QString& uri);
    void play();
    void pause();
    void stop(); // returns to READY (track stays loaded); a subsequent play() restarts from 0
    void unload(); // returns to NULL — reports as "nothing cued" until loadUri() is called again

    void seek(qint64 positionMs);

    qint64 positionMs() const;
    qint64 durationMs() const;
    GstState gstState() const;

    void setVolume(double linear); // 0.0..1.0; instant, supersedes any in-progress ramp
    double volume() const;

    // Sample-accurate volume ramp over durationMs, shaped by curve (see
    // MixEngine's hand-rolled ramp in pullRead() — replaces the old
    // GstController-driven ramp on a GStreamer volume element).
    void rampVolume(double from, double to, qint64 durationMs, RampCurve curve = RampCurve::Linear);

    // A SEPARATE gain stage from setVolume()/rampVolume() above (which
    // drive the crossfade's A/B balance) — this is the mixer panel's own
    // "deck volume" fader, like a console's trim knob vs. its channel
    // fader. Combines with autoGainCompensation below into a single
    // MixEngine::setTrim() call — see recomputeTrim() in the .cpp.
    void setTrimVolume(double linear);
    double trimVolume() const { return m_trimVolume; }

    // When enabled, automatically offsets trim using the loaded track's
    // ReplayGain tags (see hasReplayGain() below) so it plays at
    // MixEngine::masterTargetDb() rather than its own native loudness,
    // never boosted past clipping (capped against the track's ReplayGain
    // peak). No-op (silently) for a track with no ReplayGain tags.
    void setAutoGainCompensation(bool enabled);
    bool autoGainCompensationEnabled() const { return m_autoGainCompensation; }

    void setEqBandGain(int band, double gainDb); // see MixEngine::setEqBandGain()'s doc comment for band indices
    double eqBandGain(int band) const; // see MixEngine::eqBandGain()
    bool peakLevel(float& outLeft, float& outRight) const;

    // Forwarded from the underlying GstSourcePipeline — see its own doc
    // comment for what these mean and where they come from.
    bool hasReplayGain() const;
    double replayGainDb() const;
    double replayGainPeak() const;

    const QString& id() const { return m_id; }

    // Forwarded from the underlying GstSourcePipeline's bus watch (engine
    // thread). AudioEngine wires these to marshal onto the UI thread and
    // emit its own public signals, same as before.
    std::function<void()> onEos;
    std::function<void(QString message, QString debug)> onError;
    std::function<void(GstState newState)> onStateChanged;

private:
    // Recomputes the combined trim (trimVolume * auto-gain-compensation,
    // capped against the track's ReplayGain peak so it can never clip) and
    // pushes it to MixEngine::setTrim() — called whenever trimVolume, the
    // auto-comp toggle, or the loaded track's ReplayGain tags change.
    void recomputeTrim();

    QString m_id;
    MixEngine* m_mixEngine = nullptr;
    std::unique_ptr<GstSourcePipeline> m_pipeline;

    double m_trimVolume = 1.0;
    bool m_autoGainCompensation = false;
};

} // namespace radio::audio
