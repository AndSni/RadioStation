#pragma once

#include <QByteArray>
#include <QString>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include <miniaudio.h>

namespace radio::audio {

class RingBuffer;

// One playback device, as enumerated by MixEngine::enumeratePlaybackDevices().
// id is a raw byte-copy of miniaudio's ma_device_id (a 256-byte POD union —
// safe to copy verbatim without knowing which member is "active" for the
// current backend). Persist via QSettings as hex; on ALSA/PulseAudio (the
// realistic backends here) this is effectively a name string, stable for a
// fixed device across restarts but NOT guaranteed stable for a USB device if
// enumeration order shifts — callers should fall back to the default device
// (an empty QByteArray) if a persisted id doesn't match any current entry.
struct AudioDeviceInfo {
    QByteArray id;
    QString name;
    bool isDefault = false;
};

// Shape of a setGain() ramp's progress over time. Linear/SlowDown/SpeedUp
// are amplitude-linear (see MixEngine::shapeRampProgress()); EqualPower is
// NOT — it's handled separately in pullRead() via
// MixEngine::computeEqualPowerGain(), since equal-power fading can't be
// expressed as a shapeRampProgress()-style t->progress function combined
// with the linear-blend formula the other three curves share (see
// computeEqualPowerGain()'s own doc comment). Only meaningful for a ramp
// between 0 and 1 (in either direction) — the crossfade case setGain() is
// actually used for.
enum class RampCurve { Linear, SlowDown, SpeedUp, EqualPower };

// Owns the real-time mixing/output layer: a miniaudio ma_device driving the
// actual sound card, and an ma_node_graph that sums together every
// currently-attached source. Replaces GStreamer's own `audiomixer` element
// — the shared-live-aggregator design that caused the STATE_LOCK/
// STREAM_LOCK deadlock this whole rewrite exists to eliminate (see the
// architecture plan). Each source is decoded independently (GstSourcePipeline)
// and only ever touches this class through a plain, real-time-safe
// PullFn — MixEngine has no GStreamer dependency of its own.
//
// Thread-safety: attachSource()/detachSource()/setGain() are safe to call
// from any thread EXCEPT from inside the audio device callback itself
// (confirmed via miniaudio's own ma_node_graph documentation — its
// attach/detach use an internal spinlock designed for exactly this
// "control thread mutates while audio thread reads" pattern). All other
// methods here run on whatever thread calls them; only the device
// callback (dataCallback) runs on miniaudio's real-time audio thread.
class MixEngine {
public:
    // Real-time-safe pull: writes up to frameCount interleaved frames
    // (channels() channels) into out, returns frames actually written.
    // Never blocks. A source that has less data ready than requested is
    // expected to return fewer frames (0 is fine) — MixEngine pads any
    // shortfall with silence itself before mixing, never treats an
    // under-read as end-of-stream.
    using PullFn = std::function<size_t(float* out, size_t frameCount)>;

    MixEngine();
    ~MixEngine();

    // Opens the output device and starts the mix graph running. If the
    // RS_AUDIO_SINK environment variable is set to "null", forces
    // miniaudio's null backend instead of touching real hardware — the
    // same opt-in-for-tests convention already used for the GStreamer side
    // (RS_AUDIO_SINK=fakesink).
    bool start();
    void stop();

    // id must be unique among currently-attached sources; attaching an
    // already-attached id replaces it. initialGain is linear (0..1+).
    // withProcessing opts this source into the per-deck signal chain (5-band
    // EQ + trim + VU metering) below — decks always use this, and cart-wall
    // clips do too (see CartSlotPlayer::start()), so a jingle sits at the
    // same EQ curve and target loudness as the deck next to it rather than
    // playing back raw and unshaped.
    void attachSource(const QString& id, PullFn pull, double initialGain = 1.0, bool withProcessing = false);
    void detachSource(const QString& id);

    // target is linear gain; rampMs smooths the transition (0 = apply
    // immediately) via a sample-accurate ramp applied in pullRead() — this
    // is what replaces GstController-driven crossfade volume ramps. curve
    // shapes the ramp's progress over time (see shapeRampProgress()); the
    // default, Linear, is the original constant-rate behavior.
    void setGain(const QString& id, double target, qint64 rampMs = 0, RampCurve curve = RampCurve::Linear);

    // Returns the last gain requested via setGain() (or attachSource()'s
    // initialGain) — i.e. what the caller most recently asked for, not the
    // live mid-ramp interpolated value. Safe to call from any thread (a
    // plain atomic load); deliberately NOT the audio-thread-only
    // currentGain, which would need extra synchronization to read safely
    // from here. Returns 0 for an unknown id.
    double gain(const QString& id) const;

    // A SEPARATE multiplier from setGain()'s ramped crossfade gain — like a
    // mixing console's trim knob vs. its channel fader, two independent
    // gain stages in series (out = ... * crossfadeGain * trim). No-op for a
    // source attached without withProcessing. Applied instantly (no ramp):
    // this is driven by discrete UI actions (a volume slider settling, an
    // auto-gain-compensation toggle, a new track's ReplayGain tags
    // arriving), not something that needs crossfade-style smoothing.
    void setTrim(const QString& id, double linearTrim);

    // A THIRD, independent multiplier from both setGain()'s ramped
    // crossfade gain AND setTrim()'s instant trim (out = ... * crossfadeGain
    // * trim * duckGain) — deliberately NOT folded into trim, even though
    // trim is also instant/unramped: trim already combines multiple
    // UI/engine-computed concepts into one caller-supplied number (volume
    // slider position, auto-gain-compensation, ReplayGain — see setTrim()'s
    // doc comment), and DuckingController has no visibility into whatever
    // the current combined value is, so it would clobber it rather than
    // multiply against it. A dedicated slot avoids that entirely: ducking
    // can be set/cleared independently of whatever trim is doing. Applies
    // to any attached source regardless of withProcessing, same as
    // setTrim() (neither is gated on it).
    void setDuckGain(const QString& id, double linearDuckGain);

    // gainDb is NOT a ramp the way setGain()'s crossfade gain is (no
    // caller-controlled duration/curve — matches "twist a knob" EQ
    // behavior), but internally steps gradually toward the target over a
    // few callbacks rather than jumping instantly, to avoid a filter
    // coefficient discontinuity clicking (see MixEngine.cpp's
    // kMaxEqDbStepPerCallback). Full 5-band graphic EQ, same as
    // setMasterEqBandGain() below at the per-source level: band 0=60Hz low
    // shelf, 1=250Hz peak, 2=1kHz peak, 3=4kHz peak, 4=12kHz high shelf.
    // The Mixer's Deck strips only ever drive bands 0/4 (a Bass/Treble knob
    // pair — the real multi-band shaping lives on the master bus), but the
    // Mic strip exposes all 5 as sliders — this API itself makes no
    // distinction between callers. No-op for a source attached without
    // withProcessing, or an out-of-range band index.
    void setEqBandGain(const QString& id, int band, double gainDb);

    // Returns the last gain requested via setEqBandGain() for this source's
    // band — same "last requested, not the live mid-step value" contract as
    // gain(). 0.0 for an unknown id, a source attached without
    // withProcessing, or an out-of-range band index.
    double eqBandGain(const QString& id, int band) const;

    // Peak (0..1+ linear, post-EQ, post-gain — i.e. what's actually
    // audible) since the last audio callback, per channel. Safe to call
    // from any thread (plain atomic loads). false (levels left at 0) for an
    // unknown id or a source attached without withProcessing.
    bool deckPeakLevel(const QString& id, float& outLeft, float& outRight) const;

    // --- Master bus: applied to the final summed mix, after every source's
    // own gain/EQ/trim above, shared by both the local device output and
    // the streaming feed (see dataCallback()'s "local = stream" comment —
    // this matches that existing, deliberate precedent rather than
    // introducing a new split). All instant (no ramp), same reasoning as
    // setEqBandGain().
    void setMasterVolume(double linear);

    // The real graphic EQ lives here, not on individual decks (see
    // setEqBandGain()'s doc comment) — 6 fixed bands: 0=60Hz low shelf,
    // 1=150Hz peak, 2=400Hz peak, 3=1.2kHz peak, 4=3.5kHz peak, 5=12kHz
    // high shelf. Bands 0 and 5 are the same filters the master bus has
    // always had (formerly reached via setBassBoostDb()/setTrebleDb()) —
    // they still carry the loudness-compensation boost summed in on top
    // (see dataCallback()'s combinedLowDb/combinedHighDb) — bands 1-4 are
    // new. Same "steps gradually, not a ramp" behavior as setEqBandGain().
    // No-op for an out-of-range band index.
    void setMasterEqBandGain(int band, double gainDb);

    // Returns the last gain requested via setMasterEqBandGain() for this
    // band — same "last requested, not the live mid-step value" contract as
    // eqBandGain(). 0.0 for an out-of-range band index.
    double masterEqBandGain(int band) const;

    void setLoudnessEnabled(bool enabled); // amp-style: more boost at lower masterVolume, ~0 near unity
    void setMasterTargetDb(double targetDb); // reference level for per-deck auto-gain-compensation math (see DeckEngine)
    double masterTargetDb() const;
    void masterPeakLevel(float& outLeft, float& outRight) const;

    // --- Output device management (Phase 2: "survive unattended"). None of
    // these need engine-thread dispatch — like every other public method
    // here, safe to call from any thread except from inside the audio
    // device callback itself.

    // Independent of whether this instance is running: opens/closes its
    // own short-lived ma_context (enumeration's returned pointers are
    // invalidated by the next call or by ma_context_uninit(), so this
    // copies out immediately rather than reusing m_context). Honors
    // RS_AUDIO_SINK the same way start() does, so tests can force the null
    // backend (which reports one fake device).
    static std::vector<AudioDeviceInfo> enumeratePlaybackDevices();

    // Device id to use on the NEXT start() (an empty id means system
    // default, today's behavior). Call before start(); a no-op once
    // already running — use switchPlaybackDevice() for that.
    void setPreferredPlaybackDeviceId(const QByteArray& id);

    // Re-opens ONLY the ma_device against a new id (empty = system
    // default) while running — m_graph/m_sources/m_streamingRingBuffer are
    // completely untouched, since dataCallback() only reads from m_graph,
    // which doesn't care which physical device is asking. A brief audio
    // gap during the swap is real and unavoidable (closing one hardware
    // stream, opening another). Returns false (leaves the OLD device
    // running, unchanged) if the new device fails to open.
    bool switchPlaybackDevice(const QByteArray& id);

    // Call from a UI-thread poll (e.g. every ~200ms) to detect and recover
    // from the currently-selected device disappearing (e.g. a USB
    // interface unplugged) — NEVER from inside dataCallback()/the
    // notification callback itself (see MixEngine.cpp's
    // deviceNotificationCallback() for why: on this dev machine's likely
    // backend, that callback can run on the same thread ma_device_uninit()
    // would need to join, which would self-deadlock). Detects loss via
    // BOTH the notification callback's flag (fast, but not reliably fired
    // by every backend on unplug — traced: ALSA's write-error path can
    // spin without ever unwinding to fire it) and a callback-heartbeat
    // timeout (the real backstop). Performs the actual fallback reinit
    // (against the system default device) synchronously on the calling
    // thread before returning true for "something happened, go check
    // deviceLostRecoverySucceeded()". Returns false if nothing to report.
    bool pollForDeviceLoss();
    bool deviceLostRecoverySucceeded() const; // valid only right after pollForDeviceLoss() returns true

    // An OPTIONAL second, independent playback device mirroring the exact
    // final mix (post-master-bus, same signal readMixedOutput() exposes to
    // the streaming encoder) — e.g. speakers vs. headphones. NOT a
    // separate cue/PFL mix bus (this app has no per-channel pre-fade-listen
    // concept at all) — literally the same output, on a second device. id
    // empty = system default. No-op (returns true) if already running
    // against this id; call again with a different id to switch, or
    // stopMonitorDevice() to disable. Never required for start()/stop() to
    // succeed.
    bool startMonitorDevice(const QByteArray& id);
    void stopMonitorDevice();
    bool isMonitorDeviceRunning() const { return m_monitorDeviceInitialized; }

    // Independent of whether this instance is running: opens/closes its
    // own short-lived ma_context, same reasoning/RS_AUDIO_SINK convention
    // as enumeratePlaybackDevices() above, just against capture devices.
    static std::vector<AudioDeviceInfo> enumerateCaptureDevices();

    // A microphone input channel -- structurally mirrors
    // startMonitorDevice()/stopMonitorDevice() exactly: a second ma_device
    // (this time type=capture) with its own callback writing into a
    // dedicated RingBuffer (SPSC, so it needs its own buffer, not a shared
    // one — same reasoning as the monitor device's own ring buffer).
    // Internally also calls attachSource("mic", ..., withProcessing=true)
    // — from the graph's perspective the mic becomes an ordinary
    // pull-based source, getting the same 5-band EQ + VU metering every
    // deck already gets for free (see deckPeakLevel()/setEqBandGain()/
    // setDeckTrimVolume() — all already generic over any attached source
    // id, "mic" included, no new methods needed for those). id empty =
    // system default. No-op (returns true) if already running against
    // this id; call again with a different id to switch, or
    // stopMicInput() to disable.
    bool startMicInput(const QByteArray& id);
    void stopMicInput();
    bool isMicInputRunning() const { return m_micDeviceInitialized; }

    // 30Hz 2nd-order Butterworth high-pass — removes subsonic rumble before
    // it reaches any other master-bus stage. Plain on/off (fixed cutoff),
    // same simplicity as setLoudnessEnabled().
    void setMasterHighPassEnabled(bool enabled);

    // Always-active peak limiter (final stage before the device's own hard
    // clamp) — see MixEngine.cpp's computeLimiterGainReduction() for the
    // attack/release design. ceilingDb is typically -3.0..0.0. Runs a fixed
    // ~5ms lookahead (see kLimiterLookaheadFrames) — the entire mix (device
    // output and streaming feed alike) is delayed by that much, which is
    // how the limiter can pre-ramp gain reduction ahead of a transient
    // instead of reacting only once it's already too late.
    // --- Multiband compressor (Phase 3): 3-band split (Low <200Hz, Mid
    // 200Hz-3kHz, High >3kHz, via cascaded LR4/Linkwitz-Riley 4th-order
    // crossovers — see kCrossoverButterworthQ in MixEngine.cpp), each band
    // independently compressed then recombined, inserted ahead of the
    // limiter below (the limiter's lookahead delay line receives the
    // COMPRESSED signal, not the raw one). De-esser is not a separate
    // band — just tuned parameters (fast attack, low threshold, tight
    // knee — see kDeEsserAttackSeconds etc. in MixEngine.cpp) on this High
    // band, per broadcast convention. Defaults on with gentle, broadcast-
    // typical settings. band: 0=Low, 1=Mid, 2=High.
    void setCompressorEnabled(bool enabled);
    void setBandThresholdDb(int band, double thresholdDb);
    double bandThresholdDb(int band) const;
    void setBandRatio(int band, double ratio); // 1.0 = no compression
    double bandRatio(int band) const;

    // Current gain reduction for this band, in dB (0 = not engaged) — for
    // metering only. Safe to call from any thread.
    double bandGainReductionDb(int band) const;

    void setLimiterCeilingDb(double ceilingDb);

    // Current smoothed gain reduction, in dB (0 = not engaged, negative =
    // actively pulling the signal down) — for metering only. Safe to call
    // from any thread (plain atomic load).
    double limiterGainReductionDb() const;

    // Loudness leveler: a slow (multi-second), gentle corrector that
    // measures the ACTUAL post-volume output level and nudges gain toward
    // a target — complements (does not replace) per-track ReplayGain
    // compensation (see DeckEngine::recomputeTrim()), which is a static,
    // pre-computed, single number per track and can't react to what's
    // really coming out of the chain. Defaults on. rangeDb is the maximum
    // correction in either direction, typically 0..6.
    void setLevelerEnabled(bool enabled);
    void setLevelerRangeDb(double rangeDb);

    // Current correction, in dB (positive = boosting, negative = pulling
    // down) — for metering only. Safe to call from any thread.
    double levelerGainDb() const;

    // Test/tuning knob: how many seconds of audio the leveler's rolling
    // level estimate takes to converge — default a few seconds (long
    // enough to ignore normal musical dynamics, short enough to actually
    // correct within a track). Same "expose the time constant so tests can
    // force it fast and deterministic" precedent as CrossfadeController::
    // setCrossfadeLeadMs()/BlockTransitionController::setLeadSeconds().
    void setLevelerTimeConstantSeconds(double seconds);

    // --- EBU R128/LUFS loudness metering (Phase 3), measured on the FINAL
    // post-limiter output (device output and streaming feed alike) --
    // distinct from limiterGainReductionDb()/levelerGainDb() above, which
    // describe what the processing chain is DOING, not what the result
    // actually measures as loudness. All read plain atomics written by
    // dataCallback() -- safe to call from any thread. Like every other
    // meter in this class, 0.0 before the first real measurement lands
    // (~100ms after start()) is expected, not a distinct "no data" state
    // -- real LUFS/dBFS readings are essentially never exactly 0.
    double momentaryLoudnessLufs() const; // last 400ms, updated every 100ms
    double shortTermLoudnessLufs() const; // last 3s, updated every 100ms

    // Full BS.1770 two-stage-gated integrated loudness over the entire
    // measurement history (see resetIntegratedLoudnessMeasurement()) --
    // unlike momentary/shortTermLoudnessLufs() above, this does real work
    // (gating over up to hours of stored 100ms segments) and must only ever
    // be called from the UI thread, never from dataCallback() or any other
    // real-time context.
    double integratedLoudnessLufs() const;

    // Restarts integrated-loudness measurement from now -- call when a
    // streaming connection starts, so "integrated loudness" means "since
    // this broadcast began", not an accumulation across unrelated sessions.
    // Safe to call from any thread (sets a flag dataCallback() consumes on
    // its next callback).
    void resetIntegratedLoudnessMeasurement();

    // True-peak (dBFS) of the FINAL post-limiter output, since the last
    // audio callback -- a genuine inter-sample-aware measurement (see
    // estimateTruePeak()), distinct from masterPeakLevel()'s plain sample
    // peak. Safe to call from any thread.
    double outputTruePeakDb() const;

    // Real-time-safe. Pulls up to frameCount frames of the final MIXED
    // output (post-gain, post-sum, post-master-bus, pre-device-clip) — used
    // to feed the streaming encoder pipeline. The mixed buffer is captured
    // once per device callback and copied here, never re-read from the
    // graph (the graph has no concept of multiple simultaneous output
    // endpoints).
    size_t readMixedOutput(float* out, size_t frameCount);

    // Same mixed output, second independent tap — for a second,
    // redundant/backup StreamEncoderBin mount (see StreamEncoderBin::
    // MountRole). A genuinely separate RingBuffer, not a second reader on
    // the primary one: RingBuffer is explicitly single-producer/single-
    // consumer (see RingBuffer.h's own doc comment), so two independent
    // feeder threads calling readMixedOutput() concurrently would race on
    // its read index. dataCallback() writes the same mixed buffer into
    // both — same "this device's callback is the sole writer to BOTH ring
    // buffers, so each is still one-writer/one-reader" precedent already
    // established for the Phase 2 monitor device.
    size_t readBackupMixedOutput(float* out, size_t frameCount);

    static constexpr int channels() { return 2; }
    static constexpr int sampleRate() { return 48000; }

    // Maps t (elapsed/total ramp fraction, 0..1) to a shaped progress
    // fraction, also 0..1, used in place of t itself when interpolating
    // between rampStartGain and rampTargetGain — this is what gives
    // SlowDown/SpeedUp their asymmetric feel despite the ramp still running
    // for the same wall-clock duration. Linear returns t unchanged.
    // SlowDown ("fades fast at first, slows toward the end" — quadratic
    // ease-out): 1-(1-t)^2. SpeedUp ("fades slow at first, speeds up
    // toward the end" — quadratic ease-in): t^2. Pure function, no state —
    // real-time-safe to call from pullRead()'s per-frame loop, and directly
    // unit-testable without spinning up a real MixEngine.
    static float shapeRampProgress(float t, RampCurve curve);

    // General equal-power crossfade law: reduces to cos(t*pi/2) when
    // fading startGain=1->targetGain=0, and to sin(t*pi/2) when fading
    // 0->1 — the only two cases setGain()'s crossfade usage ever produces
    // (see RampCurve's doc comment). Boundary conditions (t=0 -> startGain,
    // t=1 -> targetGain) hold for either direction without a branch. NOT
    // routed through shapeRampProgress()/the linear-blend formula:
    // 1-cos(t*pi/2) != sin(t*pi/2), so unlike Linear/SlowDown/SpeedUp this
    // curve needs its own formula, not just a different shape function fed
    // into the same blend. Pure, no state — real-time-safe and directly
    // unit-testable.
    static float computeEqualPowerGain(float t, float startGain, float targetGain);

    // Moves current toward target by at most maxStepDb (which must be
    // positive) — the per-callback smoothing step behind every EQ/
    // master-bus band's gradual-gain-change behavior (see
    // kMaxEqDbStepPerCallback in MixEngine.cpp for why an instant coefficient
    // jump on a resonant filter needs smoothing at all). Pure, no state —
    // real-time-safe and directly unit-testable.
    static float stepTowardDb(float current, float target, float maxStepDb);

    // Cheap true-peak approximation for the half-sample point BETWEEN p1
    // and p2, given the two samples on either side (p0, p3) — a 4-point
    // Catmull-Rom estimate: (-p0 + 9*p1 + 9*p2 - p3) / 16. Deliberately NOT
    // a straight-line (2-point linear) interpolation between p1 and p2:
    // linear interpolation is a convex combination of its two endpoints, so
    // it can PROVABLY never exceed max(|p1|,|p2|) — mathematically
    // incapable of ever detecting an inter-sample peak, regardless of the
    // signal. Catmull-Rom, using the two outer points for curvature, can
    // genuinely overshoot the sample envelope — e.g. p0=0,p1=1,p2=1,p3=0
    // (a brief plateau, the realistic case for already-limited/clipped
    // program material) yields 1.125, correctly flagging an inter-sample
    // over neither p1 nor p2 alone would reveal. Not a full ITU-R
    // BS.1770-style 4x windowed-sinc oversampled detector (disproportionate
    // for this app, and still wouldn't catch every pathological case a
    // proper detector would) — the cheapest estimate that's actually
    // mathematically capable of catching the common case. Needs samples on
    // both sides of the interval being evaluated (p0 behind, p3 ahead) —
    // see where this is called from dataCallback()'s lookahead scan, whose
    // buffer already holds those "future" samples relative to the delayed
    // output position. Pure, no state — real-time-safe and directly
    // unit-testable.
    static float estimateTruePeak(float p0, float p1, float p2, float p3);

    // Given the previous smoothed gain-reduction value, this frame's
    // absolute peak (max across channels — a stereo limiter must move both
    // channels together or it smears the stereo image), the ceiling
    // (linear), and a precomputed release coefficient (see
    // kLimiterReleaseSeconds in MixEngine.cpp), returns the next gain-
    // reduction value to apply. Attack is instant (snaps down the moment
    // framePeak exceeds the ceiling) — this function itself has no notion
    // of lookahead; dataCallback() is what supplies a lookahead-window MAX
    // as framePeak (see kLimiterLookaheadFrames) rather than a single
    // frame's own peak, which is what actually gives the limiter as a
    // whole real pre-ramping (the reduction is already in place by the
    // time a loud frame reaches the delayed output, not snapped at the
    // same instant it's heard). Release recovers exponentially toward 1.0,
    // avoiding audible pumping. Pure, no state — real-time-safe and
    // directly unit-testable.
    static float computeLimiterGainReduction(
        float previousGainReduction, float framePeak, float ceilingLinear, float releaseCoeff);

    // One-pole EMA update: given the previous RMS estimate (mean-square,
    // linear — power, not amplitude) and this callback's measured
    // mean-square level, moves toward it by smoothingCoeff (0..1, derived
    // from the leveler's time constant and this callback's duration).
    // Deliberately a mean-square (power) quantity, not amplitude/dB — a
    // linear EMA is only mathematically well-behaved on a power-like
    // quantity; blending dB values directly would not represent the same
    // thing as blending the levels they describe. Pure, no state —
    // real-time-safe and directly unit-testable.
    static float computeLevelerRmsEstimate(float previousEstimate, float measuredMeanSquare, float smoothingCoeff);

    // Given the current RMS estimate (mean-square, linear) and a target
    // level (mean-square, linear), returns the correction to apply, in dB,
    // clamped to +/-rangeDb. Converts mean-square to dB via 10*log10 (power
    // convention) — NOT the 20*log10 amplitude convention used for
    // peak/ceiling dB elsewhere in this class; mixing the two up would be a
    // real, easy-to-make bug. Below noiseFloorMeanSquare (near-silence — an
    // intro, an outro, a gap), returns 0.0: never boost silence toward the
    // target, which would otherwise blast the noise floor up the moment
    // real audio resumes. Pure, no state — real-time-safe and directly
    // unit-testable.
    static float computeLevelerGainDb(
        float rmsEstimateMeanSquare, float targetMeanSquare, float rangeDb, float noiseFloorMeanSquare);

    // Amp-style loudness compensation, expressed against actual dB
    // attenuation from unity rather than the linear fader position — 0 at
    // volumeDb >= 0 (unity or above), 1 at volumeDb <= floorDb (floorDb
    // must be negative), tapering linearly between. A linear-fader-driven
    // taper reaches full compensation only at literal silence, which is
    // unreachable through a UI slider that (like MixerPanelWidget's) never
    // spans the full 0..-inf dB range — see kLoudnessFullBoostFloorDb in
    // MixEngine.cpp. Pure, no state — real-time-safe and directly
    // unit-testable.
    static float computeLoudnessBoostFactor(float volumeDb, float floorDb);

    // BS.1770 loudness formula: L = -0.691 + 10*log10(meanSquare), where
    // meanSquare must already be the SUMMED (not averaged) per-channel
    // mean-square for stereo content — BS.1770's channel weight is 1.0 for
    // each of L/R, so summing (not averaging) is what the standard actually
    // specifies; averaging would silently make every reading 3.01dB too
    // quiet (a real bug an earlier design review caught before any of this
    // shipped — see dataCallback()'s segmentValue comment). Clamped to a
    // tiny epsilon floor first so a genuinely silent (0.0) input never
    // produces -infinity/NaN. Pure, no state — real-time-safe and directly
    // unit-testable.
    static float lufsFromMeanSquare(double meanSquare);

    // Full BS.1770 two-stage-gated integrated loudness over a sequence of
    // already-finalized 100ms segment values (each already the SUMMED
    // per-channel mean-square for that 100ms — see lufsFromMeanSquare()'s
    // doc comment). Reconstructs 400ms/75%-overlap blocks from the 100ms
    // segments (block[k] = mean of segments k..k+3), then applies the
    // absolute (-70 LUFS) and relative (ungated mean - 10 LU) gates.
    // Returns 0.0 if there's not enough data for even one 400ms block, or
    // if every block gets gated out. Pure, no state — real-time-safe (not
    // that anything real-time calls it; integratedLoudnessLufs() is what
    // calls this against the live segment history — factored out here so
    // it's directly unit-testable against a synthetic segment sequence,
    // without spinning up a real MixEngine or waiting out real audio).
    static double computeIntegratedLoudnessLufs(const std::vector<float>& segments);

    // Pure per-band compressor gain math: soft-knee, finite-ratio,
    // non-instant-attack generalization of computeLimiterGainReduction()
    // (which is this function's ratio=infinity/attackCoeff=1.0 special
    // case). levelEstimateMeanSquare is mean-square (power), not
    // amplitude — matches computeLevelerRmsEstimate()'s convention,
    // converted to dB via 10*log10 (power, not the 20*log10 amplitude
    // convention used elsewhere in this class for peak/ceiling dB).
    // thresholdDb/kneeWidthDb are plain dB values, not linear amplitude —
    // avoids a power-vs-amplitude unit-mismatch bug a linear threshold
    // would risk. Silence (mean-square 0, levelDb == -infinity)
    // self-resolves to zero gain reduction with no separate guard needed:
    // a compressor is supposed to pass silence through at unity, not hold
    // a frozen correction (unlike the leveler, which deliberately DOES
    // gate near silence — see computeLevelerGainDb()'s own doc comment for
    // why those two cases differ). Pure, no state — real-time-safe and
    // directly unit-testable.
    static float computeBandCompressorGainReduction(float previousGainReduction, float levelEstimateMeanSquare,
        float thresholdDb, float ratio, float kneeWidthDb, float attackCoeff, float releaseCoeff);

private:
    // Per-source gain ramping is hand-rolled here rather than using
    // miniaudio's ma_gainer: ma_gainer's smoothing duration is fixed at
    // ma_gainer_init() time, not adjustable per-call, and re-initializing
    // it to change the ramp length resets its "first setting" special case
    // (ma_gainer_calculate_current_gain's t==-1 marker) — meaning the very
    // next gain change would jump instantly instead of ramping, silently
    // breaking crossfades. This class instead tracks target/elapsed/total
    // itself (via atomics written by setGain() on a control thread, read
    // and consumed by pullRead() on the audio thread) and applies a
    // sample-accurate linear interpolation per buffer — the same curve
    // shape GstController used to produce.
    struct PullDataSource {
        ma_data_source_base base; // MUST be the first member — miniaudio's custom-data-source contract
        PullFn pull;
        std::vector<float> scratch;

        // Cross-thread ramp request: control thread writes, audio thread
        // reads. requestSeq is the "new request pending" signal — the
        // audio thread only reads the other three when it observes seq
        // change, avoiding any need for target/rampFrames to themselves be
        // atomically consistent as a pair.
        std::atomic<float> requestedTargetGain{ 1.0f };
        std::atomic<ma_uint64> requestedRampFrames{ 0 };
        std::atomic<RampCurve> requestedRampCurve{ RampCurve::Linear };
        std::atomic<uint64_t> requestSeq{ 0 };

        // Audio-thread-only — never touched from the control thread.
        uint64_t lastSeenSeq = 0;
        float currentGain = 1.0f;
        float rampStartGain = 1.0f;
        float rampTargetGain = 1.0f;
        RampCurve rampCurve = RampCurve::Linear;
        ma_uint64 rampFramesTotal = 0;
        ma_uint64 rampFramesElapsed = 0;

        // Trim: a second, unramped multiplier — see setTrim()'s doc comment
        // above for why this is kept separate from the ramped gain above.
        std::atomic<float> requestedTrim{ 1.0f };

        // Duck gain: a THIRD, unramped multiplier — see setDuckGain()'s doc
        // comment for why this is kept separate from trim above too.
        std::atomic<float> requestedDuckGain{ 1.0f };

        // Per-deck processing (full 5-band EQ + metering), only used when
        // attachSource()'s withProcessing is true — reinit() only ever
        // happens from pullRead() on the audio thread, never directly from
        // whatever control thread called setEqBandGain(): miniaudio's own
        // filter _reinit() functions do a plain unlocked field write, not
        // safe to race against this same struct's process callback. Every
        // withProcessing source (decks, cart clips, mic) gets the full
        // 5-band capability — the Mixer's Deck strips only ever drive bands
        // 0/4 (Bass/Treble knobs) themselves, but the Mic strip still
        // exposes all 5 as sliders (see MixerPanelWidget::buildMicStrip()).
        bool hasProcessing = false;
        ma_loshelf2 eqLowShelf{};
        ma_peak2 eqBand2{};
        ma_peak2 eqBand3{};
        ma_peak2 eqBand4{};
        ma_hishelf2 eqHighShelf{};

        std::atomic<float> requestedLowShelfGainDb{ 0.0f };
        std::atomic<float> requestedBand2GainDb{ 0.0f };
        std::atomic<float> requestedBand3GainDb{ 0.0f };
        std::atomic<float> requestedBand4GainDb{ 0.0f };
        std::atomic<float> requestedHighShelfGainDb{ 0.0f };

        // Audio-thread-only. "Applied" tracks where each filter's gain
        // actually is right now, which pullRead() steps gradually toward
        // the requested value above (see MixEngine.cpp's
        // kMaxEqDbStepPerCallback) — re-checked every callback rather than
        // latched behind a seq number, since a large change can take
        // several callbacks to converge.
        float appliedLowShelfGainDb = 0.0f;
        float appliedBand2GainDb = 0.0f;
        float appliedBand3GainDb = 0.0f;
        float appliedBand4GainDb = 0.0f;
        float appliedHighShelfGainDb = 0.0f;

        // VU metering: overwritten (not accumulated) each callback with
        // that callback's peak of the final post-EQ, post-gain, post-trim
        // signal — i.e. what's actually audible. Written by pullRead() on
        // the audio thread, read from any thread via deckPeakLevel().
        std::atomic<float> meterPeakL{ 0.0f };
        std::atomic<float> meterPeakR{ 0.0f };
    };

    struct SourceEntry {
        std::unique_ptr<PullDataSource> dataSource;
        ma_data_source_node node;
    };

    // Shared by detachSource() (one entry) and stop() (every remaining
    // entry) — uninits the node (which detaches from the graph, blocking
    // until the audio thread is done with any in-flight processing),
    // the data source, and, if hasProcessing, the 5 EQ filters. Extracted
    // as one function specifically so stop() can't drift out of sync with
    // detachSource() and skip cleanup for whatever's still attached at
    // shutdown — confirmed via reading miniaudio's own
    // ma_node_graph_uninit() that it only tears down the graph's own
    // endpoint/base nodes, never anything external still attached to it.
    static void teardownSourceEntry(SourceEntry* entry);

public:
    // Callback surface required by miniaudio's C vtable/device-callback
    // mechanisms (needs external linkage to a file-scope vtable constant) —
    // not part of the class's intended usage surface; treat as private
    // implementation detail despite the access level.
    static void dataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
    static ma_result pullRead(ma_data_source* dataSource, void* framesOut, ma_uint64 frameCount, ma_uint64* framesRead);
    static ma_result pullGetDataFormat(ma_data_source* dataSource, ma_format* format, ma_uint32* channels,
        ma_uint32* sampleRate, ma_channel* channelMap, size_t channelMapCap);
    static ma_result pullSeek(ma_data_source* dataSource, ma_uint64 frameIndex);
    static ma_result pullGetCursor(ma_data_source* dataSource, ma_uint64* cursor);
    static ma_result pullGetLength(ma_data_source* dataSource, ma_uint64* length);

private:
    // Builds the shared device config (format/channels/rate/callbacks) and
    // calls ma_device_init() against m_context into m_device — id may be
    // null (system default). If a non-null id fails to open (the
    // persisted/selected device no longer exists), retries once against
    // null before giving up, so a vanished device degrades to "use the
    // default" rather than failing outright. Does NOT call
    // ma_device_start() — start()/switchPlaybackDevice() do that
    // themselves once whatever else they each need ready first (start()
    // needs the ring buffers/master filters set up before audio can flow;
    // switchPlaybackDevice() doesn't, since those already exist).
    bool initDevice(const ma_device_id* id);

    static void deviceNotificationCallback(const ma_device_notification* pNotification);
    static void monitorDataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);
    static void micCaptureDataCallback(ma_device* device, void* output, const void* input, ma_uint32 frameCount);

    ma_device m_device{};
    ma_context m_context{};
    bool m_contextInitialized = false;
    ma_node_graph m_graph{};
    bool m_graphInitialized = false;
    bool m_deviceInitialized = false;

    std::map<QString, std::unique_ptr<SourceEntry>> m_sources;

    std::unique_ptr<RingBuffer> m_streamingRingBuffer;
    std::unique_ptr<RingBuffer> m_backupStreamingRingBuffer; // see readBackupMixedOutput()'s doc comment

    // --- Master bus (see the public setters above for the full picture).
    // Same marshal-through-the-callback pattern as PullDataSource's EQ:
    // control thread writes a pending request, dataCallback() (audio
    // thread) checks and applies it once per buffer.
    bool m_masterFiltersInitialized = false;
    // Bands 0 and 5 of the master 6-band EQ (setMasterEqBandGain()) — also
    // still carry the loudness shelves' gain, summed in — see dataCallback().
    ma_loshelf2 m_bassBoost{};
    ma_hishelf2 m_treble{};
    // Bands 1-4 of the master 6-band EQ — plain peaking filters, no
    // loudness-compensation gain summed into these (loudness only ever
    // shaped the two end shelves, same as before this EQ became 6 bands).
    ma_peak2 m_masterEqBand1{};
    ma_peak2 m_masterEqBand2{};
    ma_peak2 m_masterEqBand3{};
    ma_peak2 m_masterEqBand4{};
    ma_hpf2 m_highPass{};

    // K-weighting side-chain (see kKWeightStage1B0 etc. in MixEngine.cpp) —
    // applied only to m_levelerScratch, a copy of the signal, never to buf/
    // the actual output. m_levelerScratch grows-only (never shrinks/
    // reallocates mid-steady-state), matching PullDataSource::scratch's
    // pattern.
    ma_biquad m_levelerKStage1{};
    ma_biquad m_levelerKStage2{};
    std::vector<float> m_levelerScratch;

    // --- Multiband compressor (Phase 3, see the public methods above).
    // 8 filter instances: two cascaded Butterworth-Q stages per crossover
    // leg (LR4 = Linkwitz-Riley 4th-order). m_xoverMidHighStage1/2 splits
    // Low off from the raw input; m_xoverMidStage1/2/m_xoverHighStage1/2
    // then split that Mid+High intermediate into Mid and High.
    ma_lpf2 m_xoverLowStage1{};
    ma_lpf2 m_xoverLowStage2{};
    ma_hpf2 m_xoverMidHighStage1{};
    ma_hpf2 m_xoverMidHighStage2{};
    ma_lpf2 m_xoverMidStage1{};
    ma_lpf2 m_xoverMidStage2{};
    ma_hpf2 m_xoverHighStage1{};
    ma_hpf2 m_xoverHighStage2{};

    std::atomic<bool> m_compressorEnabled{ true };
    std::atomic<float> m_requestedLowBandThresholdDb{ -20.0f };
    std::atomic<float> m_requestedLowBandRatio{ 2.0f };
    std::atomic<float> m_requestedMidBandThresholdDb{ -18.0f };
    std::atomic<float> m_requestedMidBandRatio{ 2.0f };
    // De-esser tuning lives in the fixed knee/attack/release/detector
    // constants applied to this band in MixEngine.cpp, not here — only
    // threshold/ratio are user-settable, same as Low/Mid.
    std::atomic<float> m_requestedHighBandThresholdDb{ -24.0f };
    std::atomic<float> m_requestedHighBandRatio{ 4.0f };

    // Audio-thread-only, persists across callbacks — one RMS-estimate
    // (detector) and one smoothed gain-reduction value per band, same
    // shapes as the limiter's/leveler's own single-band equivalents.
    float m_lowBandLevelEstimate = 0.0f;
    float m_lowBandGainReduction = 1.0f;
    float m_midBandLevelEstimate = 0.0f;
    float m_midBandGainReduction = 1.0f;
    float m_highBandLevelEstimate = 0.0f;
    float m_highBandGainReduction = 1.0f;

    std::atomic<float> m_lowBandGainReductionDb{ 0.0f }; // metering only
    std::atomic<float> m_midBandGainReductionDb{ 0.0f };
    std::atomic<float> m_highBandGainReductionDb{ 0.0f };

    // Limiter lookahead delay line — see kLimiterLookaheadFrames in
    // MixEngine.cpp. Both fixed-size, allocated once in start(), NEVER
    // resized in the callback (real-time-safety — same reasoning as
    // PullDataSource::scratch/m_levelerScratch growing only outside the
    // real-time-critical steady state, just with no growth at all here
    // since the size is a fixed constant, not caller-dependent). Circular,
    // indexed by m_lookaheadWritePos.
    std::vector<float> m_lookaheadDelayLine; // raw (post-volume/leveler) samples, interleaved
    std::vector<float> m_lookaheadPeakWindow; // one true-peak estimate (see estimateTruePeak()) per frame
    size_t m_lookaheadWritePos = 0;
    // Per-channel 3-sample history feeding estimateTruePeak()'s 4-point
    // Catmull-Rom estimate as each new raw sample arrives (see
    // dataCallback()) — persists across callbacks.
    float m_peakHistoryL[3] = { 0.0f, 0.0f, 0.0f };
    float m_peakHistoryR[3] = { 0.0f, 0.0f, 0.0f };

    // --- EBU R128/LUFS metering (see the public methods above). Same
    // "control thread requests, audio thread applies" marshal pattern used
    // throughout this class.

    // Independent K-weighting biquad pair from the leveler's own
    // m_levelerKStage1/2 (same BS.1770 coefficients, see MixEngine.cpp) --
    // this one measures the FINAL post-limiter output, not the leveler's
    // pre-limiter side-chain, so the two must never share state.
    ma_biquad m_r128KStage1{};
    ma_biquad m_r128KStage2{};
    std::vector<float> m_r128Scratch; // grow-only, mirrors m_levelerScratch

    // Persists across callbacks -- a 100ms segment (kR128SegmentFrames in
    // MixEngine.cpp) essentially never divides evenly into an arbitrary
    // callback size, so this accumulates across as many callbacks as it
    // takes and finalizes whenever it reaches kR128SegmentFrames.
    double m_r128SegmentSumSquaresL = 0.0;
    double m_r128SegmentSumSquaresR = 0.0;
    size_t m_r128SegmentFrameCount = 0;

    // Momentary (400ms = 4 segments) / short-term (3s = 30 segments) --
    // fixed-size ring buffers of raw per-segment channel-summed mean-square
    // values (not yet converted to dB), re-summed in full on every 100ms
    // finalize (see dataCallback() in MixEngine.cpp for why re-summing 4 or
    // 30 floats every 100ms is cheap enough not to need a running sum).
    static constexpr size_t kR128MomentarySegmentCount = 4;
    static constexpr size_t kR128ShortTermSegmentCount = 30;
    float m_r128MomentarySegments[kR128MomentarySegmentCount] = {};
    float m_r128ShortTermSegments[kR128ShortTermSegmentCount] = {};
    size_t m_r128MomentaryIndex = 0;
    size_t m_r128ShortTermIndex = 0;

    // Integrated-loudness history: fixed capacity, resize()'d ONCE in
    // start(), NEVER push_back()'d/resized/cleared again afterward -- a
    // concurrent UI-thread read (integratedLoudnessLufs()) racing an audio-
    // thread mutation of the vector's actual size would be a real data
    // race. Only the atomic count below ever changes after start() --
    // audio thread: relaxed store of a new segment's value followed by a
    // release-store of the incremented count; UI thread: acquire-load the
    // count, then read that many already-published entries.
    std::vector<float> m_r128SegmentHistory;
    std::atomic<size_t> m_r128SegmentHistoryCount{ 0 };
    // Control thread requests (resetIntegratedLoudnessMeasurement()), audio
    // thread applies once (consumed and cleared) on its next callback --
    // the standard marshaling pattern every other setter in this file uses.
    std::atomic<bool> m_r128IntegratedResetRequested{ false };

    std::atomic<float> m_r128MomentaryLufs{ 0.0f };
    std::atomic<float> m_r128ShortTermLufs{ 0.0f };

    // Post-limiter true-peak tracking -- an independent Catmull-Rom history
    // from m_peakHistoryL/R above, which feeds the LIMITER off the
    // pre-limiter raw signal; this one measures what the limiter actually
    // produced (see estimateTruePeak()'s doc comment for the algorithm).
    float m_outputPeakHistoryL[3] = { 0.0f, 0.0f, 0.0f };
    float m_outputPeakHistoryR[3] = { 0.0f, 0.0f, 0.0f };
    std::atomic<float> m_outputTruePeakLinear{ 0.0f };

    std::atomic<float> m_requestedMasterVolume{ 1.0f };
    // Master 6-band EQ requested gains (setMasterEqBandGain()) — band 0/5
    // named for what they've always been (bass boost/treble), bands 1-4
    // new.
    std::atomic<float> m_requestedBassBoostDb{ 0.0f };
    std::atomic<float> m_requestedMasterEqBand1Db{ 0.0f };
    std::atomic<float> m_requestedMasterEqBand2Db{ 0.0f };
    std::atomic<float> m_requestedMasterEqBand3Db{ 0.0f };
    std::atomic<float> m_requestedMasterEqBand4Db{ 0.0f };
    std::atomic<float> m_requestedTrebleDb{ 0.0f };
    std::atomic<bool> m_loudnessEnabled{ false };
    std::atomic<float> m_masterTargetDb{ 0.0f }; // read directly by DeckEngine's auto-comp math; no ramp/reinit involved
    std::atomic<bool> m_highPassEnabled{ false };
    std::atomic<float> m_requestedLimiterCeilingDb{ -1.0f }; // matches MixerPanelWidget's default slider position
    std::atomic<bool> m_levelerEnabled{ true }; // defaults on — addresses an active, reported problem, unlike the other optional coloring stages
    std::atomic<float> m_requestedLevelerRangeDb{ 3.0f };
    std::atomic<float> m_levelerTimeConstantSeconds{ 4.0f }; // settable via setLevelerTimeConstantSeconds() for tests; atomic — read every callback on the audio thread, written from the control thread

    // Audio-thread-only. m_appliedBassBoostDb/m_appliedTrebleDb track the
    // COMBINED (bass/treble + loudness) gain actually applied to
    // m_bassBoost/m_treble, stepped gradually toward it — see
    // kMaxEqDbStepPerCallback in MixEngine.cpp.
    float m_appliedBassBoostDb = 0.0f;
    float m_appliedTrebleDb = 0.0f;
    // Master EQ bands 1-4 — plain applied gain, no loudness component
    // (unlike m_appliedBassBoostDb/m_appliedTrebleDb above).
    float m_appliedMasterEqBand1Db = 0.0f;
    float m_appliedMasterEqBand2Db = 0.0f;
    float m_appliedMasterEqBand3Db = 0.0f;
    float m_appliedMasterEqBand4Db = 0.0f;
    float m_limiterGainReduction = 1.0f; // envelope-follower state, persists across callbacks
    float m_levelerRmsEstimate = 0.0f; // mean-square, linear — rolling estimate, persists across callbacks
    float m_levelerGain = 1.0f; // linear, computed from the PREVIOUS callback's measurement (feedback, not feedforward)

    std::atomic<float> m_masterPeakL{ 0.0f };
    std::atomic<float> m_masterPeakR{ 0.0f };
    std::atomic<float> m_limiterGainReductionDb{ 0.0f }; // metering only
    std::atomic<float> m_levelerGainDb{ 0.0f }; // metering only

    // --- Output device management (see the public methods' doc comments
    // above for the full picture).
    QByteArray m_preferredPlaybackDeviceId; // set via setPreferredPlaybackDeviceId(), consumed by start()

    // true until the device is genuinely up and running, and set back to
    // true around every deliberate teardown (stop()/switchPlaybackDevice())
    // — so our own intentional device stops are never misread as loss by
    // deviceNotificationCallback().
    std::atomic<bool> m_intentionalStop{ true };
    std::atomic<bool> m_deviceLostPending{ false }; // written ONLY by deviceNotificationCallback() -- a plain flag, no other work happens there (see its doc comment on why)
    std::atomic<qint64> m_lastCallbackEpochMs{ 0 }; // steady-clock ms, written every dataCallback() invocation -- the real backstop when a backend never fires the stopped notification on unplug
    bool m_deviceLostRecoverySucceeded = false; // control-thread only, set by pollForDeviceLoss()

    ma_device m_monitorDevice{};
    bool m_monitorDeviceInitialized = false;
    std::unique_ptr<RingBuffer> m_monitorRingBuffer;

    ma_device m_micDevice{};
    bool m_micDeviceInitialized = false;
    std::unique_ptr<RingBuffer> m_micRingBuffer;
};

} // namespace radio::audio
