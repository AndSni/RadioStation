#include "MixEngine.h"
#include "RingBuffer.h"

#include "core/Logging.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace radio::audio {

float MixEngine::shapeRampProgress(float t, RampCurve curve)
{
    switch (curve) {
    case RampCurve::SlowDown: // fast at first, slows toward the end - quadratic ease-out
        return 1.0f - (1.0f - t) * (1.0f - t);
    case RampCurve::SpeedUp: // slow at first, speeds up toward the end - quadratic ease-in
        return t * t;
    case RampCurve::Linear:
    default:
        return t;
    }
}

float MixEngine::computeEqualPowerGain(float t, float startGain, float targetGain)
{
    constexpr float kHalfPi = static_cast<float>(M_PI) / 2.0f;
    return startGain * std::cos(t * kHalfPi) + targetGain * std::sin(t * kHalfPi);
}

float MixEngine::estimateTruePeak(float p0, float p1, float p2, float p3)
{
    const float halfSamplePoint = (-p0 + 9.0f * p1 + 9.0f * p2 - p3) / 16.0f;
    return std::max({ std::fabs(p1), std::fabs(p2), std::fabs(halfSamplePoint) });
}

float MixEngine::computeLimiterGainReduction(
    float previousGainReduction, float framePeak, float ceilingLinear, float releaseCoeff)
{
    const float neededGain = framePeak > ceilingLinear ? ceilingLinear / framePeak : 1.0f;
    if (neededGain < previousGainReduction)
        return neededGain; // instant attack — snap down immediately, never overshoot the ceiling
    return previousGainReduction + (1.0f - previousGainReduction) * releaseCoeff; // exponential release toward 1.0
}

float MixEngine::computeLevelerRmsEstimate(float previousEstimate, float measuredMeanSquare, float smoothingCoeff)
{
    return previousEstimate + (measuredMeanSquare - previousEstimate) * smoothingCoeff;
}

float MixEngine::stepTowardDb(float current, float target, float maxStepDb)
{
    return current + std::clamp(target - current, -maxStepDb, maxStepDb);
}

float MixEngine::computeLevelerGainDb(
    float rmsEstimateMeanSquare, float targetMeanSquare, float rangeDb, float noiseFloorMeanSquare)
{
    if (rmsEstimateMeanSquare < noiseFloorMeanSquare)
        return 0.0f; // silence — never boost toward the target
    const float rmsEstimateDb = 10.0f * std::log10(rmsEstimateMeanSquare);
    const float targetDb = 10.0f * std::log10(targetMeanSquare);
    return std::clamp(targetDb - rmsEstimateDb, -rangeDb, rangeDb);
}

float MixEngine::computeLoudnessBoostFactor(float volumeDb, float floorDb)
{
    return std::clamp(volumeDb / floorDb, 0.0f, 1.0f);
}

float MixEngine::lufsFromMeanSquare(double meanSquare)
{
    // A mean-square of exactly 0.0 (true digital silence) would make
    // log10() produce -infinity — clamped to this floor first so every
    // LUFS value this file computes stays finite, never propagates a NaN
    // or inf into an average.
    constexpr double kSilentMeanSquareFloor = 1e-12;
    return static_cast<float>(-0.691 + 10.0 * std::log10(std::max(meanSquare, kSilentMeanSquareFloor)));
}

double MixEngine::computeIntegratedLoudnessLufs(const std::vector<float>& segments)
{
    if (segments.size() < 4)
        return 0.0; // not enough data for even one 400ms gating block yet

    std::vector<double> blockMeanSquares;
    blockMeanSquares.reserve(segments.size() - 3);
    for (size_t k = 0; k + 4 <= segments.size(); ++k) {
        const double sum
            = static_cast<double>(segments[k]) + segments[k + 1] + segments[k + 2] + segments[k + 3];
        blockMeanSquares.push_back(sum / 4.0);
    }

    // Stage 1: absolute gate at -70 LUFS.
    std::vector<double> absoluteGated;
    absoluteGated.reserve(blockMeanSquares.size());
    for (const double blockMeanSquare : blockMeanSquares) {
        if (lufsFromMeanSquare(blockMeanSquare) >= -70.0f)
            absoluteGated.push_back(blockMeanSquare);
    }
    if (absoluteGated.empty())
        return 0.0; // everything so far was gated out (silence/near-silence)

    // Stage 2: relative gate at (ungated mean over the absolute-gated set)
    // minus 10 LU.
    double absoluteGatedSum = 0.0;
    for (const double blockMeanSquare : absoluteGated)
        absoluteGatedSum += blockMeanSquare;
    const double relativeThresholdDb
        = static_cast<double>(lufsFromMeanSquare(absoluteGatedSum / static_cast<double>(absoluteGated.size()))) - 10.0;

    double finalSum = 0.0;
    size_t finalCount = 0;
    for (const double blockMeanSquare : absoluteGated) {
        if (lufsFromMeanSquare(blockMeanSquare) >= relativeThresholdDb) {
            finalSum += blockMeanSquare;
            ++finalCount;
        }
    }
    if (finalCount == 0)
        return 0.0;

    return static_cast<double>(lufsFromMeanSquare(finalSum / static_cast<double>(finalCount)));
}

float MixEngine::computeBandCompressorGainReduction(float previousGainReduction, float levelEstimateMeanSquare,
    float thresholdDb, float ratio, float kneeWidthDb, float attackCoeff, float releaseCoeff)
{
    // A mean-square of exactly 0.0 (true silence) makes log10() produce
    // -infinity, which the "below the knee" branch below handles
    // correctly and naturally (-infinity <= kneeLower is always true) —
    // no separate silence guard needed.
    const float levelDb = 10.0f * std::log10(levelEstimateMeanSquare);
    const float kneeLower = thresholdDb - kneeWidthDb * 0.5f;
    const float kneeUpper = thresholdDb + kneeWidthDb * 0.5f;

    float targetReductionDb;
    if (levelDb <= kneeLower) {
        targetReductionDb = 0.0f;
    } else if (levelDb >= kneeUpper) {
        // (x - T) * (1/ratio - 1) -- collapses to a hard ceiling at
        // thresholdDb as ratio grows large (infinite-ratio limiting),
        // matching computeLimiterGainReduction()'s own ceiling behavior
        // once attackCoeff is also ~1 (instant attack).
        targetReductionDb = (levelDb - thresholdDb) * (1.0f / ratio - 1.0f);
    } else {
        // Standard soft-knee quadratic interpolation -- continuous with
        // both branches above at the knee's edges.
        const float delta = levelDb - kneeLower;
        targetReductionDb = ((1.0f / ratio - 1.0f) * delta * delta) / (2.0f * kneeWidthDb);
    }

    const float targetGainLinear = std::pow(10.0f, targetReductionDb / 20.0f);
    const float coeff = targetGainLinear < previousGainReduction ? attackCoeff : releaseCoeff;
    return previousGainReduction + (targetGainLinear - previousGainReduction) * coeff;
}

namespace {
constexpr ma_uint32 kRingBufferCapacityFrames = MixEngine::sampleRate(); // ~1 second

// Monotonic, immune to wall-clock adjustments — used for the device-loss
// heartbeat (see MixEngine::pollForDeviceLoss()'s doc comment). std::chrono
// rather than QDateTime deliberately: this is compared against itself only,
// never displayed, so there's no reason to pull in Qt's wall-clock type for
// something a system clock adjustment could otherwise corrupt.
qint64 steadyClockNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

const ma_data_source_vtable kPullVtable = {
    &MixEngine::pullRead,
    &MixEngine::pullSeek,
    &MixEngine::pullGetDataFormat,
    &MixEngine::pullGetCursor,
    &MixEngine::pullGetLength,
};

// Standard 5-band graphic-EQ layout for every per-source EQ (decks, cart
// clips, mic — see PullDataSource). Also the two shelf frequencies the
// master EQ's own band 0/5 (see setMasterEqBandGain()) reuse, so a Deck
// strip's Bass knob and the master's leftmost EQ slider act on the same
// part of the spectrum. shelfSlope/peakQ are the RBJ-cookbook "musical
// default" values (1.0 / ~Butterworth 0.707) — miniaudio's own
// ma_loshelf2_config_init()/ma_hishelf2_config_init() name this parameter
// "q" but it's actually passed straight through as shelfSlope (confirmed by
// reading the vendored miniaudio.h — a real naming inconsistency in the
// library itself, not a mistake here).
constexpr double kEqLowShelfFreq = 60.0;
constexpr double kEqBand2Freq = 250.0;
constexpr double kEqBand3Freq = 1000.0;
constexpr double kEqBand4Freq = 4000.0;
constexpr double kEqHighShelfFreq = 12000.0;
constexpr double kShelfSlope = 1.0;
constexpr double kPeakQ = 0.707;

// Master EQ's own 4 mid peaking bands (see setMasterEqBandGain()) — roughly
// log-spaced between the 60Hz/12kHz shelves above, standard 6-band
// graphic-EQ territory. A deliberately DIFFERENT set of frequencies from
// the per-source EQ's own 250Hz/1kHz/4kHz above — the master bus is where
// the real multi-band shaping happens, so its own bands don't need to
// match a per-source strip's.
constexpr double kMasterEqBand1Freq = 150.0;
constexpr double kMasterEqBand2Freq = 400.0;
constexpr double kMasterEqBand3Freq = 1200.0;
constexpr double kMasterEqBand4Freq = 3500.0;

// Amp-style "loudness" curve: taper from these maximum boosts at/below
// kLoudnessFullBoostFloorDb down to 0dB at/above unity gain.
constexpr double kMaxLoudnessLowBoostDb = 6.0;
constexpr double kMaxLoudnessHighBoostDb = 3.0;

// Full loudness compensation is reached by this many dB BELOW unity, not
// only at literal silence — matches MixerPanelWidget's master-volume slider
// range (kMasterVolumeRangeDb = 12, i.e. the slider only ever reaches
// -12dB), so full compensation is actually reachable at the slider's
// quietest position rather than requiring an attenuation nothing in the UI
// can produce.
constexpr double kLoudnessFullBoostFloorDb = -12.0;

// Below this, a gain-request is treated as "unchanged" — avoids calling a
// filter's _reinit() (a real, if small, unlocked coefficient recompute)
// from floating-point noise alone.
constexpr float kEqGainEpsilonDb = 0.01f;

// Caps how far an EQ/master-bus band's applied gain moves in a single
// callback — a large instant coefficient jump on a resonant filter (this
// file's peaking bands) can click/thump from the discontinuity between the
// filter's retained sample history and its suddenly-different coefficients.
// 6dB/callback still converges a full +/-12dB slider throw in ~2 callbacks
// (a few ms) — imperceptible as a ramp, but enough to eliminate the
// discontinuity. Applies equally to a human turning a knob and to any
// future automated EQ change.
constexpr float kMaxEqDbStepPerCallback = 6.0f;

// Standard broadcast subsonic-filter cutoff — well below any audible bass,
// removes true rumble/handling noise before it reaches bass-boost/loudness.
constexpr double kMasterHighPassFreq = 30.0;

// --- Multiband compressor (Phase 3) ---------------------------------------

constexpr double kCrossoverLowMidHz = 200.0;
constexpr double kCrossoverMidHighHz = 3000.0;

// Butterworth Q for the LR4 (Linkwitz-Riley 4th-order) crossover legs —
// deliberately its OWN named constant, not kPeakQ above: kPeakQ is
// documented as the per-deck peaking-EQ's musical default, and silently
// repurposing it here would be a maintainability trap if it's ever tuned
// for EQ feel later. Two cascaded stages at this Q per leg (see
// m_xoverLowStage1/2 etc.) is what makes each leg a true LR4, not just a
// single 2nd-order Butterworth section.
constexpr double kCrossoverButterworthQ = 0.7071;

// Low/Mid band knee/attack/release/detector -- gentle, broadcast-typical
// compressor tuning (not exposed as settings; only threshold/ratio are).
constexpr double kCompressorKneeWidthDb = 6.0;
constexpr double kCompressorAttackSeconds = 0.010;
constexpr double kCompressorReleaseSeconds = 0.150;
constexpr double kCompressorDetectorTimeConstantSeconds = 0.010;

// De-esser tuning (High band only) -- tighter knee, faster attack/release,
// and a shorter detector window than Low/Mid: catches fast sibilant
// transients a slower detector would smear past, and recovers quickly so
// it doesn't audibly dull everything else that also happens to live in
// the High band.
constexpr double kDeEsserKneeWidthDb = 3.0;
constexpr double kDeEsserAttackSeconds = 0.003;
constexpr double kDeEsserReleaseSeconds = 0.080;
constexpr double kDeEsserDetectorTimeConstantSeconds = 0.003;

// One-pole EMA coefficient for a given time constant, evaluated once at
// static-init time (these never change at runtime, unlike the leveler's
// own per-CALLBACK smoothingCoeff, which depends on that callback's frame
// count -- a per-SAMPLE coefficient like this one doesn't).
float onePoleCoeffForTimeConstant(double timeConstantSeconds)
{
    return 1.0f - static_cast<float>(std::exp(-1.0 / (timeConstantSeconds * MixEngine::sampleRate())));
}

const float kCompressorAttackCoeff = onePoleCoeffForTimeConstant(kCompressorAttackSeconds);
const float kCompressorReleaseCoeff = onePoleCoeffForTimeConstant(kCompressorReleaseSeconds);
const float kCompressorDetectorCoeff = onePoleCoeffForTimeConstant(kCompressorDetectorTimeConstantSeconds);
const float kDeEsserAttackCoeff = onePoleCoeffForTimeConstant(kDeEsserAttackSeconds);
const float kDeEsserReleaseCoeff = onePoleCoeffForTimeConstant(kDeEsserReleaseSeconds);
const float kDeEsserDetectorCoeff = onePoleCoeffForTimeConstant(kDeEsserDetectorTimeConstantSeconds);

// Limiter release time constant — how long it takes gain reduction to
// recover back toward 1.0 once the signal drops back under the ceiling.
// 150ms is a standard "transparent" value (fast enough to not audibly duck
// the mix, slow enough to avoid pumping). Attack is instant (see
// MixEngine::computeLimiterGainReduction()), so there's no matching attack
// constant.
constexpr double kLimiterReleaseSeconds = 0.15;

// How far ahead the limiter looks before a sample reaches the output —
// 5ms is enough for computeLimiterGainReduction()'s instant-attack branch
// to already have pulled gain reduction down by the time a loud frame
// (seen via the lookahead window's MAX, not just its own single-frame
// peak) actually reaches the delayed output position, avoiding the
// zero-lookahead design's harsher, purely-reactive character. This delays
// the ENTIRE mix (device output and streaming feed alike) by this many
// frames — confirmed safe: this app has no live mic/capture input
// anywhere for a human to self-monitor in real time (attachSource() is
// only ever called by DeckEngine/CartSlotPlayer, both file-based decode),
// so nothing is latency-sensitive to a human ear the way live monitoring
// would be.
constexpr int kLimiterLookaheadFrames = 240; // 5ms @ 48kHz

// Target level the leveler tries to converge the output toward — matches
// ReplayGain's own legacy reference level, so the two work in harmony
// (the leveler is a gentle, adaptive TOP-UP on what per-track ReplayGain
// compensation already got most of the way there, not a competing target).
// Expressed as an RMS amplitude dB value (same 20*log10 convention as
// every other dB value in this file); kLevelerTargetMeanSquare below
// converts it to the power/mean-square domain the leveler's own math
// actually operates in.
constexpr double kLevelerTargetDb = -18.0;

// Below this, the leveler treats the signal as silence (an intro, an
// outro, a gap between tracks) and never applies a boost — without this,
// true silence would drive the RMS estimate toward -infinity dB and the
// leveler would try to apply enormous gain the moment real audio resumes.
constexpr double kLevelerNoiseFloorDb = -50.0;

// ITU-R BS.1770 K-weighting, exact 48kHz direct-form biquad coefficients
// (a0 = 1.0 in both stages) — same published values used by libebur128/
// ffmpeg's ebur128 filter. Applied to a SIDE-CHAIN COPY of the signal for
// the leveler's own measurement only (see dataCallback()'s
// m_levelerScratch) — never to the actual output path. This makes the
// leveler's RMS estimate track PERCEIVED loudness rather than flat energy
// (a bass-heavy and a bright track at identical flat RMS sound different
// loudness to a human ear) — this is K-weighted RMS leveling, not full
// gated-block LUFS metering (no 400ms blocks, no relative/absolute gating,
// which would be disproportionate for a continuous one-pole leveler);
// kLevelerTargetDb above is unchanged, just measured more accurately now.
// Stage 1: a ~+4dB shelf above ~1681Hz approximating head diffraction.
constexpr double kKWeightStage1B0 = 1.53512485958697;
constexpr double kKWeightStage1B1 = -2.69169618940638;
constexpr double kKWeightStage1B2 = 1.19839281085285;
constexpr double kKWeightStage1A1 = -1.69065929318241;
constexpr double kKWeightStage1A2 = 0.73248077421585;
// Stage 2 (RLB filter): a ~38Hz highpass approximating ear/head absorption.
constexpr double kKWeightStage2B0 = 1.0;
constexpr double kKWeightStage2B1 = -2.0;
constexpr double kKWeightStage2B2 = 1.0;
constexpr double kKWeightStage2A1 = -1.99004745483398;
constexpr double kKWeightStage2A2 = 0.99007225036621;

double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

// Inverse of dbToLinear(). Callers must guard linear <= 0 themselves (log of
// zero/negative is undefined) — see dataCallback()'s masterVolume check.
double linearToDb(double linear)
{
    return 20.0 * std::log10(linear);
}

// dB values in this file are amplitude (20*log10) EXCEPT the leveler's own
// internal mean-square/power math, which needs the power convention
// (10*log10) — kept as a separate helper specifically so the two never get
// mixed up by accident.
double dbToMeanSquare(double db)
{
    return std::pow(10.0, db / 10.0);
}

const double kLevelerTargetMeanSquare = dbToMeanSquare(kLevelerTargetDb);
const double kLevelerNoiseFloorMeanSquare = dbToMeanSquare(kLevelerNoiseFloorDb);

// --- EBU R128/LUFS metering (Phase 3) -------------------------------------

// One measurement segment, the finest granularity momentary/short-term/
// integrated loudness are all built from below -- 100ms at 48kHz.
constexpr int kR128SegmentFrames = MixEngine::sampleRate() / 10;

// How many finalized 100ms segments MixEngine::start() pre-allocates
// storage for -- 12 hours' worth (12*3600*10 segments/sec), matching
// m_lookaheadDelayLine's "fixed-size, allocated once in start(), never
// resized in the callback" discipline. A session running integrated
// loudness measurement continuously past this without ever calling
// resetIntegratedLoudnessMeasurement() (e.g. a new streaming connection)
// simply stops recording further segments into the integrated figure —
// momentary/short-term readings are unaffected.
constexpr size_t kR128SegmentHistoryCapacitySegments = 12 * 3600 * 10;

ma_loshelf2_config makeLowShelfConfig(double gainDb, double frequency)
{
    return ma_loshelf2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), gainDb, kShelfSlope, frequency);
}

ma_hishelf2_config makeHighShelfConfig(double gainDb, double frequency)
{
    return ma_hishelf2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), gainDb, kShelfSlope, frequency);
}

ma_peak2_config makePeakConfig(double gainDb, double frequency)
{
    return ma_peak2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), gainDb, kPeakQ, frequency);
}

ma_hpf2_config makeHighPassConfig(double frequency)
{
    return ma_hpf2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), frequency, kPeakQ);
}

// Crossover leg config helpers -- kCrossoverButterworthQ, NOT kPeakQ (see
// its own doc comment above for why makeHighPassConfig()'s Q isn't reused
// here despite the two currently having the same numeric value).
ma_lpf2_config makeCrossoverLowpassConfig(double frequency)
{
    return ma_lpf2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), frequency, kCrossoverButterworthQ);
}

ma_hpf2_config makeCrossoverHighpassConfig(double frequency)
{
    return ma_hpf2_config_init(ma_format_f32, static_cast<ma_uint32>(MixEngine::channels()),
        static_cast<ma_uint32>(MixEngine::sampleRate()), frequency, kCrossoverButterworthQ);
}
}

MixEngine::MixEngine() = default;

MixEngine::~MixEngine()
{
    stop();
}

bool MixEngine::start()
{
    ma_backend nullBackend = ma_backend_null;
    // RS_AUDIO_SINK is the same override the old GStreamer-sink-based
    // design used ("if set to ANYTHING, don't touch real hardware" — see
    // every test's CMakeLists.txt, which sets it to "fakesink", a
    // GStreamer sink name meaningless to miniaudio). Decks/carts no longer
    // have a local GStreamer sink to choose at all (they always terminate
    // in appsink now) — this env var's only remaining purpose is selecting
    // miniaudio's backend here, so any non-empty value forces the null
    // backend rather than requiring a specific value.
    const bool forceNull = !qgetenv("RS_AUDIO_SINK").isEmpty();

    const ma_result contextResult = forceNull
        ? ma_context_init(&nullBackend, 1, nullptr, &m_context)
        : ma_context_init(nullptr, 0, nullptr, &m_context);
    if (contextResult != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize audio context (%1)").arg(contextResult));
        return false;
    }
    m_contextInitialized = true;

    ma_node_graph_config graphConfig = ma_node_graph_config_init(static_cast<ma_uint32>(channels()));
    if (ma_node_graph_init(&graphConfig, nullptr, &m_graph) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize node graph"));
        return false;
    }
    m_graphInitialized = true;

    ma_device_id preferredId;
    const ma_device_id* preferredIdPtr = nullptr;
    if (!m_preferredPlaybackDeviceId.isEmpty() && m_preferredPlaybackDeviceId.size() == sizeof(ma_device_id)) {
        std::memcpy(&preferredId, m_preferredPlaybackDeviceId.constData(), sizeof(ma_device_id));
        preferredIdPtr = &preferredId;
    }
    if (!initDevice(preferredIdPtr)) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize output device"));
        return false;
    }
    m_deviceInitialized = true;

    m_streamingRingBuffer = std::make_unique<RingBuffer>(kRingBufferCapacityFrames, channels());
    m_backupStreamingRingBuffer = std::make_unique<RingBuffer>(kRingBufferCapacityFrames, channels());

    // Fixed-size, zero-initialized (silence) — sized once here, never
    // resized in dataCallback(). Starting silent means the first
    // kLimiterLookaheadFrames of real output are delayed behind that much
    // silence, the same (inaudible, expected) warm-up any lookahead delay
    // has at stream start.
    m_lookaheadDelayLine.assign(static_cast<size_t>(kLimiterLookaheadFrames) * channels(), 0.0f);
    m_lookaheadPeakWindow.assign(static_cast<size_t>(kLimiterLookaheadFrames), 0.0f);
    m_lookaheadWritePos = 0;
    std::fill(std::begin(m_peakHistoryL), std::end(m_peakHistoryL), 0.0f);
    std::fill(std::begin(m_peakHistoryR), std::end(m_peakHistoryR), 0.0f);

    // Master-bus filters start at 0dB (flat) — always initialized so
    // dataCallback() can unconditionally process_pcm_frames() through them;
    // "disabled" just means the caller never requests a nonzero gain.
    // Bass/treble double as the loudness compensation shelves (see
    // dataCallback()'s combinedLowDb/combinedHighDb) rather than each
    // having a separate same-frequency filter of their own.
    const ma_loshelf2_config bassBoostConfig = makeLowShelfConfig(0.0, kEqLowShelfFreq);
    const ma_hishelf2_config trebleConfig = makeHighShelfConfig(0.0, kEqHighShelfFreq);
    // Master EQ bands 1-4 (see setMasterEqBandGain()) — the 4 new mid
    // peaking bands between the bass/treble shelves above.
    const ma_peak2_config masterEqBand1Config = makePeakConfig(0.0, kMasterEqBand1Freq);
    const ma_peak2_config masterEqBand2Config = makePeakConfig(0.0, kMasterEqBand2Freq);
    const ma_peak2_config masterEqBand3Config = makePeakConfig(0.0, kMasterEqBand3Freq);
    const ma_peak2_config masterEqBand4Config = makePeakConfig(0.0, kMasterEqBand4Freq);
    const ma_hpf2_config highPassConfig = makeHighPassConfig(kMasterHighPassFreq);
    const ma_biquad_config kWeightStage1Config = ma_biquad_config_init(ma_format_f32,
        static_cast<ma_uint32>(channels()), kKWeightStage1B0, kKWeightStage1B1, kKWeightStage1B2, 1.0,
        kKWeightStage1A1, kKWeightStage1A2);
    const ma_biquad_config kWeightStage2Config = ma_biquad_config_init(ma_format_f32,
        static_cast<ma_uint32>(channels()), kKWeightStage2B0, kKWeightStage2B1, kKWeightStage2B2, 1.0,
        kKWeightStage2A1, kKWeightStage2A2);
    // Independent instances from m_levelerKStage1/2 above (same
    // coefficients, same config objects reused) -- this pair measures the
    // FINAL post-limiter output for R128 metering, never the leveler's
    // pre-limiter side-chain, so the two must never share filter state.
    //
    // The 8 crossover filters below implement the multiband compressor's
    // 3-band LR4 split (see m_xoverLowStage1's doc comment in MixEngine.h
    // for the topology) -- two stages per leg, each leg its own config
    // object (a shared config passed to two ma_lpf2_init() calls produces
    // two INDEPENDENT filter instances with identical coefficients but
    // separate internal state, exactly what cascading a 2nd-order section
    // into an LR4 requires).
    const ma_lpf2_config xoverLowConfig = makeCrossoverLowpassConfig(kCrossoverLowMidHz);
    const ma_hpf2_config xoverMidHighConfig = makeCrossoverHighpassConfig(kCrossoverLowMidHz);
    const ma_lpf2_config xoverMidConfig = makeCrossoverLowpassConfig(kCrossoverMidHighHz);
    const ma_hpf2_config xoverHighConfig = makeCrossoverHighpassConfig(kCrossoverMidHighHz);
    if (ma_loshelf2_init(&bassBoostConfig, nullptr, &m_bassBoost) != MA_SUCCESS
        || ma_hishelf2_init(&trebleConfig, nullptr, &m_treble) != MA_SUCCESS
        || ma_peak2_init(&masterEqBand1Config, nullptr, &m_masterEqBand1) != MA_SUCCESS
        || ma_peak2_init(&masterEqBand2Config, nullptr, &m_masterEqBand2) != MA_SUCCESS
        || ma_peak2_init(&masterEqBand3Config, nullptr, &m_masterEqBand3) != MA_SUCCESS
        || ma_peak2_init(&masterEqBand4Config, nullptr, &m_masterEqBand4) != MA_SUCCESS
        || ma_hpf2_init(&highPassConfig, nullptr, &m_highPass) != MA_SUCCESS
        || ma_biquad_init(&kWeightStage1Config, nullptr, &m_levelerKStage1) != MA_SUCCESS
        || ma_biquad_init(&kWeightStage2Config, nullptr, &m_levelerKStage2) != MA_SUCCESS
        || ma_biquad_init(&kWeightStage1Config, nullptr, &m_r128KStage1) != MA_SUCCESS
        || ma_biquad_init(&kWeightStage2Config, nullptr, &m_r128KStage2) != MA_SUCCESS
        || ma_lpf2_init(&xoverLowConfig, nullptr, &m_xoverLowStage1) != MA_SUCCESS
        || ma_lpf2_init(&xoverLowConfig, nullptr, &m_xoverLowStage2) != MA_SUCCESS
        || ma_hpf2_init(&xoverMidHighConfig, nullptr, &m_xoverMidHighStage1) != MA_SUCCESS
        || ma_hpf2_init(&xoverMidHighConfig, nullptr, &m_xoverMidHighStage2) != MA_SUCCESS
        || ma_lpf2_init(&xoverMidConfig, nullptr, &m_xoverMidStage1) != MA_SUCCESS
        || ma_lpf2_init(&xoverMidConfig, nullptr, &m_xoverMidStage2) != MA_SUCCESS
        || ma_hpf2_init(&xoverHighConfig, nullptr, &m_xoverHighStage1) != MA_SUCCESS
        || ma_hpf2_init(&xoverHighConfig, nullptr, &m_xoverHighStage2) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize master-bus filters"));
        return false;
    }
    m_masterFiltersInitialized = true;

    // EBU R128/LUFS state, fresh for every start() -- the segment history
    // vector is assign()'d (fixed capacity, zero-filled) exactly once here
    // and never resized again for the lifetime of this running instance
    // (see m_r128SegmentHistory's doc comment in MixEngine.h).
    m_r128SegmentSumSquaresL = 0.0;
    m_r128SegmentSumSquaresR = 0.0;
    m_r128SegmentFrameCount = 0;
    std::fill(std::begin(m_r128MomentarySegments), std::end(m_r128MomentarySegments), 0.0f);
    std::fill(std::begin(m_r128ShortTermSegments), std::end(m_r128ShortTermSegments), 0.0f);
    m_r128MomentaryIndex = 0;
    m_r128ShortTermIndex = 0;
    m_r128SegmentHistory.assign(kR128SegmentHistoryCapacitySegments, 0.0f);
    m_r128SegmentHistoryCount.store(0, std::memory_order_relaxed);
    m_r128IntegratedResetRequested.store(false, std::memory_order_relaxed);
    m_r128MomentaryLufs.store(0.0f, std::memory_order_relaxed);
    m_r128ShortTermLufs.store(0.0f, std::memory_order_relaxed);
    std::fill(std::begin(m_outputPeakHistoryL), std::end(m_outputPeakHistoryL), 0.0f);
    std::fill(std::begin(m_outputPeakHistoryR), std::end(m_outputPeakHistoryR), 0.0f);
    m_outputTruePeakLinear.store(0.0f, std::memory_order_relaxed);

    if (ma_device_start(&m_device) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to start output device"));
        return false;
    }
    // Only from here on can the device genuinely stop unexpectedly — see
    // deviceNotificationCallback()'s doc comment.
    m_intentionalStop.store(false, std::memory_order_relaxed);
    m_lastCallbackEpochMs.store(steadyClockNowMs(), std::memory_order_relaxed);

    RS_LOG_INFO("audio.pipeline", QStringLiteral("MixEngine started (%1Hz, %2ch)").arg(sampleRate()).arg(channels()));
    return true;
}

void MixEngine::stop()
{
    m_intentionalStop.store(true, std::memory_order_relaxed);
    stopMonitorDevice(); // must happen before m_context is uninitialized below -- m_monitorDevice was opened against it
    stopMicInput(); // same reasoning -- m_micDevice was opened against m_context too
    if (m_deviceInitialized) {
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
    }
    // SourceEntry has no destructor of its own (ma_data_source_node is a
    // plain C struct) and ma_node_graph_uninit() below only tears down the
    // graph's own endpoint/base nodes, never anything externally attached
    // to it (confirmed by reading its implementation) — so every remaining
    // source must go through the same teardown detachSource() uses, or its
    // node (and, now, its EQ filters) never gets uninitialized.
    for (auto& [id, entry] : m_sources)
        teardownSourceEntry(entry.get());
    m_sources.clear();
    if (m_graphInitialized) {
        ma_node_graph_uninit(&m_graph, nullptr);
        m_graphInitialized = false;
    }
    if (m_contextInitialized) {
        ma_context_uninit(&m_context);
        m_contextInitialized = false;
    }
    if (m_masterFiltersInitialized) {
        ma_loshelf2_uninit(&m_bassBoost, nullptr);
        ma_hishelf2_uninit(&m_treble, nullptr);
        ma_peak2_uninit(&m_masterEqBand1, nullptr);
        ma_peak2_uninit(&m_masterEqBand2, nullptr);
        ma_peak2_uninit(&m_masterEqBand3, nullptr);
        ma_peak2_uninit(&m_masterEqBand4, nullptr);
        ma_hpf2_uninit(&m_highPass, nullptr);
        ma_biquad_uninit(&m_levelerKStage1, nullptr);
        ma_biquad_uninit(&m_levelerKStage2, nullptr);
        ma_biquad_uninit(&m_r128KStage1, nullptr);
        ma_biquad_uninit(&m_r128KStage2, nullptr);
        ma_lpf2_uninit(&m_xoverLowStage1, nullptr);
        ma_lpf2_uninit(&m_xoverLowStage2, nullptr);
        ma_hpf2_uninit(&m_xoverMidHighStage1, nullptr);
        ma_hpf2_uninit(&m_xoverMidHighStage2, nullptr);
        ma_lpf2_uninit(&m_xoverMidStage1, nullptr);
        ma_lpf2_uninit(&m_xoverMidStage2, nullptr);
        ma_hpf2_uninit(&m_xoverHighStage1, nullptr);
        ma_hpf2_uninit(&m_xoverHighStage2, nullptr);
        m_masterFiltersInitialized = false;
    }
    m_streamingRingBuffer.reset();
    m_backupStreamingRingBuffer.reset();
}

void MixEngine::attachSource(const QString& id, PullFn pull, double initialGain, bool withProcessing)
{
    if (m_sources.find(id) != m_sources.end())
        detachSource(id);

    auto entry = std::make_unique<SourceEntry>();
    entry->dataSource = std::make_unique<PullDataSource>();
    entry->dataSource->pull = std::move(pull);
    entry->dataSource->currentGain = static_cast<float>(initialGain);
    entry->dataSource->rampStartGain = static_cast<float>(initialGain);
    entry->dataSource->rampTargetGain = static_cast<float>(initialGain);
    entry->dataSource->requestedTargetGain.store(static_cast<float>(initialGain), std::memory_order_relaxed);

    if (withProcessing) {
        const ma_loshelf2_config lowConfig = makeLowShelfConfig(0.0, kEqLowShelfFreq);
        const ma_peak2_config band2Config = makePeakConfig(0.0, kEqBand2Freq);
        const ma_peak2_config band3Config = makePeakConfig(0.0, kEqBand3Freq);
        const ma_peak2_config band4Config = makePeakConfig(0.0, kEqBand4Freq);
        const ma_hishelf2_config highConfig = makeHighShelfConfig(0.0, kEqHighShelfFreq);
        if (ma_loshelf2_init(&lowConfig, nullptr, &entry->dataSource->eqLowShelf) == MA_SUCCESS
            && ma_peak2_init(&band2Config, nullptr, &entry->dataSource->eqBand2) == MA_SUCCESS
            && ma_peak2_init(&band3Config, nullptr, &entry->dataSource->eqBand3) == MA_SUCCESS
            && ma_peak2_init(&band4Config, nullptr, &entry->dataSource->eqBand4) == MA_SUCCESS
            && ma_hishelf2_init(&highConfig, nullptr, &entry->dataSource->eqHighShelf) == MA_SUCCESS) {
            entry->dataSource->hasProcessing = true;
        } else {
            RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize EQ for source %1").arg(id));
        }
    }

    ma_data_source_config dsConfig = ma_data_source_config_init();
    dsConfig.vtable = &kPullVtable;
    ma_data_source_init(&dsConfig, &entry->dataSource->base);

    ma_data_source_node_config nodeConfig
        = ma_data_source_node_config_init(reinterpret_cast<ma_data_source*>(entry->dataSource.get()));
    if (ma_data_source_node_init(&m_graph, &nodeConfig, nullptr, &entry->node) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to attach source %1").arg(id));
        return;
    }

    // Sum this source's output straight into the graph's endpoint — no
    // intermediate routing needed, gain/EQ is already applied inside
    // pullRead() via the hand-rolled per-source ramp/filters.
    ma_node_attach_output_bus(&entry->node, 0, ma_node_graph_get_endpoint(&m_graph), 0);

    m_sources.emplace(id, std::move(entry));
}

void MixEngine::teardownSourceEntry(SourceEntry* entry)
{
    // ma_data_source_node_uninit() internally calls ma_node_uninit(), which
    // fully detaches from the graph (blocking until the audio thread is
    // done with any in-flight processing of this node) BEFORE freeing
    // anything — an explicit ma_node_detach_output_bus() call first would
    // be redundant, not an extra safety measure.
    ma_data_source_node_uninit(&entry->node, nullptr);
    ma_data_source_uninit(reinterpret_cast<ma_data_source*>(entry->dataSource.get()));

    if (entry->dataSource->hasProcessing) {
        ma_loshelf2_uninit(&entry->dataSource->eqLowShelf, nullptr);
        ma_peak2_uninit(&entry->dataSource->eqBand2, nullptr);
        ma_peak2_uninit(&entry->dataSource->eqBand3, nullptr);
        ma_peak2_uninit(&entry->dataSource->eqBand4, nullptr);
        ma_hishelf2_uninit(&entry->dataSource->eqHighShelf, nullptr);
    }
}

void MixEngine::detachSource(const QString& id)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return;

    teardownSourceEntry(it->second.get());
    m_sources.erase(it);
}

void MixEngine::setGain(const QString& id, double target, qint64 rampMs, RampCurve curve)
{
    auto it = m_sources.find(id);
    if (it == m_sources.end())
        return;

    PullDataSource* ds = it->second->dataSource.get();
    const ma_uint64 rampFrames
        = rampMs > 0 ? static_cast<ma_uint64>((rampMs * sampleRate()) / 1000) : 0;
    // requestSeq published last (release) so pullRead's acquire-load is
    // guaranteed to observe both prior stores once it sees the new seq.
    ds->requestedTargetGain.store(static_cast<float>(target), std::memory_order_relaxed);
    ds->requestedRampFrames.store(rampFrames, std::memory_order_relaxed);
    ds->requestedRampCurve.store(curve, std::memory_order_relaxed);
    ds->requestSeq.fetch_add(1, std::memory_order_release);
}

double MixEngine::gain(const QString& id) const
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end())
        return 0.0;
    return static_cast<double>(it->second->dataSource->requestedTargetGain.load(std::memory_order_relaxed));
}

void MixEngine::setTrim(const QString& id, double linearTrim)
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end())
        return;
    it->second->dataSource->requestedTrim.store(static_cast<float>(linearTrim), std::memory_order_relaxed);
}

void MixEngine::setDuckGain(const QString& id, double linearDuckGain)
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end())
        return;
    it->second->dataSource->requestedDuckGain.store(static_cast<float>(linearDuckGain), std::memory_order_relaxed);
}

void MixEngine::setEqBandGain(const QString& id, int band, double gainDb)
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end())
        return;

    PullDataSource* ds = it->second->dataSource.get();
    if (!ds->hasProcessing)
        return;

    switch (band) {
    case 0:
        ds->requestedLowShelfGainDb.store(static_cast<float>(gainDb), std::memory_order_relaxed);
        break;
    case 1:
        ds->requestedBand2GainDb.store(static_cast<float>(gainDb), std::memory_order_relaxed);
        break;
    case 2:
        ds->requestedBand3GainDb.store(static_cast<float>(gainDb), std::memory_order_relaxed);
        break;
    case 3:
        ds->requestedBand4GainDb.store(static_cast<float>(gainDb), std::memory_order_relaxed);
        break;
    case 4:
        ds->requestedHighShelfGainDb.store(static_cast<float>(gainDb), std::memory_order_relaxed);
        break;
    default:
        return; // out-of-range band index — no-op, matches doc comment
    }
    // No seq/acquire-release handshake needed here (unlike setGain()'s
    // requestSeq): pullRead() now re-reads and re-compares every band's
    // requested value every callback regardless (see its own comment, for
    // gradual-stepping's sake), so a plain relaxed store is enough — the
    // same "always re-check" convention the master-bus bands already used.
}

double MixEngine::eqBandGain(const QString& id, int band) const
{
    const auto it = m_sources.find(id);
    if (it == m_sources.end())
        return 0.0;

    const PullDataSource* ds = it->second->dataSource.get();
    if (!ds->hasProcessing)
        return 0.0;

    switch (band) {
    case 0:
        return static_cast<double>(ds->requestedLowShelfGainDb.load(std::memory_order_relaxed));
    case 1:
        return static_cast<double>(ds->requestedBand2GainDb.load(std::memory_order_relaxed));
    case 2:
        return static_cast<double>(ds->requestedBand3GainDb.load(std::memory_order_relaxed));
    case 3:
        return static_cast<double>(ds->requestedBand4GainDb.load(std::memory_order_relaxed));
    case 4:
        return static_cast<double>(ds->requestedHighShelfGainDb.load(std::memory_order_relaxed));
    default:
        return 0.0; // out-of-range band index — matches setEqBandGain()'s no-op
    }
}

bool MixEngine::deckPeakLevel(const QString& id, float& outLeft, float& outRight) const
{
    outLeft = 0.0f;
    outRight = 0.0f;
    const auto it = m_sources.find(id);
    if (it == m_sources.end() || !it->second->dataSource->hasProcessing)
        return false;

    outLeft = it->second->dataSource->meterPeakL.load(std::memory_order_relaxed);
    outRight = it->second->dataSource->meterPeakR.load(std::memory_order_relaxed);
    return true;
}

void MixEngine::setMasterVolume(double linear)
{
    m_requestedMasterVolume.store(static_cast<float>(linear), std::memory_order_relaxed);
}

void MixEngine::setMasterEqBandGain(int band, double gainDb)
{
    const auto db = static_cast<float>(gainDb);
    switch (band) {
    case 0:
        m_requestedBassBoostDb.store(db, std::memory_order_relaxed);
        break;
    case 1:
        m_requestedMasterEqBand1Db.store(db, std::memory_order_relaxed);
        break;
    case 2:
        m_requestedMasterEqBand2Db.store(db, std::memory_order_relaxed);
        break;
    case 3:
        m_requestedMasterEqBand3Db.store(db, std::memory_order_relaxed);
        break;
    case 4:
        m_requestedMasterEqBand4Db.store(db, std::memory_order_relaxed);
        break;
    case 5:
        m_requestedTrebleDb.store(db, std::memory_order_relaxed);
        break;
    default:
        return; // out-of-range band index — no-op, matches doc comment
    }
}

double MixEngine::masterEqBandGain(int band) const
{
    switch (band) {
    case 0:
        return static_cast<double>(m_requestedBassBoostDb.load(std::memory_order_relaxed));
    case 1:
        return static_cast<double>(m_requestedMasterEqBand1Db.load(std::memory_order_relaxed));
    case 2:
        return static_cast<double>(m_requestedMasterEqBand2Db.load(std::memory_order_relaxed));
    case 3:
        return static_cast<double>(m_requestedMasterEqBand3Db.load(std::memory_order_relaxed));
    case 4:
        return static_cast<double>(m_requestedMasterEqBand4Db.load(std::memory_order_relaxed));
    case 5:
        return static_cast<double>(m_requestedTrebleDb.load(std::memory_order_relaxed));
    default:
        return 0.0; // out-of-range band index — matches setMasterEqBandGain()'s no-op
    }
}

void MixEngine::setLoudnessEnabled(bool enabled)
{
    m_loudnessEnabled.store(enabled, std::memory_order_relaxed);
}

void MixEngine::setMasterTargetDb(double targetDb)
{
    m_masterTargetDb.store(static_cast<float>(targetDb), std::memory_order_relaxed);
}

double MixEngine::masterTargetDb() const
{
    return static_cast<double>(m_masterTargetDb.load(std::memory_order_relaxed));
}

void MixEngine::masterPeakLevel(float& outLeft, float& outRight) const
{
    outLeft = m_masterPeakL.load(std::memory_order_relaxed);
    outRight = m_masterPeakR.load(std::memory_order_relaxed);
}

void MixEngine::setMasterHighPassEnabled(bool enabled)
{
    m_highPassEnabled.store(enabled, std::memory_order_relaxed);
}

void MixEngine::setCompressorEnabled(bool enabled)
{
    m_compressorEnabled.store(enabled, std::memory_order_relaxed);
}

void MixEngine::setBandThresholdDb(int band, double thresholdDb)
{
    switch (band) {
    case 0:
        m_requestedLowBandThresholdDb.store(static_cast<float>(thresholdDb), std::memory_order_relaxed);
        break;
    case 1:
        m_requestedMidBandThresholdDb.store(static_cast<float>(thresholdDb), std::memory_order_relaxed);
        break;
    case 2:
        m_requestedHighBandThresholdDb.store(static_cast<float>(thresholdDb), std::memory_order_relaxed);
        break;
    default:
        break; // out-of-range band index -- no-op, matches setEqBandGain()'s convention
    }
}

double MixEngine::bandThresholdDb(int band) const
{
    switch (band) {
    case 0:
        return static_cast<double>(m_requestedLowBandThresholdDb.load(std::memory_order_relaxed));
    case 1:
        return static_cast<double>(m_requestedMidBandThresholdDb.load(std::memory_order_relaxed));
    case 2:
        return static_cast<double>(m_requestedHighBandThresholdDb.load(std::memory_order_relaxed));
    default:
        return 0.0;
    }
}

void MixEngine::setBandRatio(int band, double ratio)
{
    switch (band) {
    case 0:
        m_requestedLowBandRatio.store(static_cast<float>(ratio), std::memory_order_relaxed);
        break;
    case 1:
        m_requestedMidBandRatio.store(static_cast<float>(ratio), std::memory_order_relaxed);
        break;
    case 2:
        m_requestedHighBandRatio.store(static_cast<float>(ratio), std::memory_order_relaxed);
        break;
    default:
        break;
    }
}

double MixEngine::bandRatio(int band) const
{
    switch (band) {
    case 0:
        return static_cast<double>(m_requestedLowBandRatio.load(std::memory_order_relaxed));
    case 1:
        return static_cast<double>(m_requestedMidBandRatio.load(std::memory_order_relaxed));
    case 2:
        return static_cast<double>(m_requestedHighBandRatio.load(std::memory_order_relaxed));
    default:
        return 1.0; // out-of-range -- "no compression" is the least surprising default for a getter
    }
}

double MixEngine::bandGainReductionDb(int band) const
{
    switch (band) {
    case 0:
        return static_cast<double>(m_lowBandGainReductionDb.load(std::memory_order_relaxed));
    case 1:
        return static_cast<double>(m_midBandGainReductionDb.load(std::memory_order_relaxed));
    case 2:
        return static_cast<double>(m_highBandGainReductionDb.load(std::memory_order_relaxed));
    default:
        return 0.0;
    }
}

void MixEngine::setLimiterCeilingDb(double ceilingDb)
{
    m_requestedLimiterCeilingDb.store(static_cast<float>(ceilingDb), std::memory_order_relaxed);
}

double MixEngine::limiterGainReductionDb() const
{
    return static_cast<double>(m_limiterGainReductionDb.load(std::memory_order_relaxed));
}

void MixEngine::setLevelerEnabled(bool enabled)
{
    m_levelerEnabled.store(enabled, std::memory_order_relaxed);
}

void MixEngine::setLevelerRangeDb(double rangeDb)
{
    m_requestedLevelerRangeDb.store(static_cast<float>(rangeDb), std::memory_order_relaxed);
}

double MixEngine::levelerGainDb() const
{
    return static_cast<double>(m_levelerGainDb.load(std::memory_order_relaxed));
}

void MixEngine::setLevelerTimeConstantSeconds(double seconds)
{
    m_levelerTimeConstantSeconds.store(static_cast<float>(seconds), std::memory_order_relaxed);
}

double MixEngine::momentaryLoudnessLufs() const
{
    return static_cast<double>(m_r128MomentaryLufs.load(std::memory_order_relaxed));
}

double MixEngine::shortTermLoudnessLufs() const
{
    return static_cast<double>(m_r128ShortTermLufs.load(std::memory_order_relaxed));
}

void MixEngine::resetIntegratedLoudnessMeasurement()
{
    m_r128IntegratedResetRequested.store(true, std::memory_order_relaxed);
}

double MixEngine::outputTruePeakDb() const
{
    const float linear = m_outputTruePeakLinear.load(std::memory_order_relaxed);
    // linearToDb(0) is -infinity, not a useful value to hand a caller (a UI
    // label formatting it, a DB column) -- -100dBFS is comfortably below
    // any real signal and reads unambiguously as "silent" rather than
    // "broken".
    return linear > 0.0f ? linearToDb(static_cast<double>(linear)) : -100.0;
}

// UI-thread-only (see the doc comment in MixEngine.h), never called from
// dataCallback() or any other real-time context -- takes a snapshot copy of
// the live segment history (bounded by an acquire-load of the published
// count, matching m_r128SegmentHistoryCount's doc comment) and hands it to
// the actual gating algorithm, computeIntegratedLoudnessLufs(), which is a
// pure function over that snapshot and directly unit-testable on its own.
double MixEngine::integratedLoudnessLufs() const
{
    const size_t count = m_r128SegmentHistoryCount.load(std::memory_order_acquire);
    const auto begin = m_r128SegmentHistory.begin();
    return computeIntegratedLoudnessLufs(std::vector<float>(begin, begin + static_cast<std::ptrdiff_t>(count)));
}

size_t MixEngine::readMixedOutput(float* out, size_t frameCount)
{
    if (!m_streamingRingBuffer)
        return 0;
    return m_streamingRingBuffer->read(out, frameCount);
}

size_t MixEngine::readBackupMixedOutput(float* out, size_t frameCount)
{
    if (!m_backupStreamingRingBuffer)
        return 0;
    return m_backupStreamingRingBuffer->read(out, frameCount);
}

void MixEngine::dataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    auto* self = static_cast<MixEngine*>(device->pUserData);

    // Device-loss heartbeat (see pollForDeviceLoss()'s doc comment) — cheap,
    // real-time-safe (one relaxed store), and the only reliable cross-
    // backend signal that this device is genuinely still delivering audio;
    // the notificationCallback path alone isn't sufficient on every backend.
    self->m_lastCallbackEpochMs.store(steadyClockNowMs(), std::memory_order_relaxed);

    ma_uint64 framesRead = 0;
    ma_node_graph_read_pcm_frames(&self->m_graph, output, frameCount, &framesRead);

    auto* buf = static_cast<float*>(output);
    const size_t ch = static_cast<size_t>(channels());
    const size_t frames = static_cast<size_t>(framesRead);

    if (self->m_masterFiltersInitialized && frames > 0) {
        // High-pass first — nothing below the cutoff should ever reach the
        // boost/loudness stages below.
        if (self->m_highPassEnabled.load(std::memory_order_relaxed))
            ma_hpf2_process_pcm_frames(&self->m_highPass, buf, buf, frames);

        // Amp-style loudness: recomputed every callback from the CURRENT
        // master volume (more boost at lower volume, ~0 near/above unity)
        // rather than a separate pending-request atomic — cheap to compute,
        // and the epsilon-guarded reinit below already avoids churn while
        // volume sits still. Computed BEFORE bass/treble below because it
        // shares their same 60Hz/12kHz filters (see the combinedLowDb/
        // combinedHighDb comment) rather than running through its own,
        // separate pair of shelves.
        const float masterVolume = self->m_requestedMasterVolume.load(std::memory_order_relaxed);
        const bool loudnessOn = self->m_loudnessEnabled.load(std::memory_order_relaxed);
        // dB-based, not linear-fader-based: tapering against the linear
        // masterVolume value directly would reach full compensation only
        // at literal zero gain, which kLoudnessFullBoostFloorDb's own
        // comment explains is unreachable through the actual UI slider.
        const float volumeDb = masterVolume > 0.0f
            ? static_cast<float>(linearToDb(static_cast<double>(masterVolume)))
            : static_cast<float>(kLoudnessFullBoostFloorDb);
        const float boostFactor = loudnessOn
            ? computeLoudnessBoostFactor(volumeDb, static_cast<float>(kLoudnessFullBoostFloorDb))
            : 0.0f;
        const float loudnessLowDb = boostFactor * static_cast<float>(kMaxLoudnessLowBoostDb);
        const float loudnessHighDb = boostFactor * static_cast<float>(kMaxLoudnessHighBoostDb);

        // Bass boost and the loudness low-shelf are both centered at
        // kEqLowShelfFreq — cascading two independent shelves at the same
        // frequency would double the phase rotation there for no benefit
        // over one filter at the summed gain, and cost an extra biquad pass
        // every callback. Same reasoning for treble + the loudness
        // high-shelf at kEqHighShelfFreq.
        const float requestedBassDb = self->m_requestedBassBoostDb.load(std::memory_order_relaxed);
        const float combinedLowDb = requestedBassDb + loudnessLowDb;
        if (std::fabs(combinedLowDb - self->m_appliedBassBoostDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->m_appliedBassBoostDb, combinedLowDb, kMaxEqDbStepPerCallback);
            const ma_loshelf2_config cfg = makeLowShelfConfig(steppedDb, kEqLowShelfFreq);
            if (ma_loshelf2_reinit(&cfg, &self->m_bassBoost) == MA_SUCCESS)
                self->m_appliedBassBoostDb = steppedDb;
        }
        ma_loshelf2_process_pcm_frames(&self->m_bassBoost, buf, buf, frames);

        // Master EQ bands 1-4 (see setMasterEqBandGain()) — the 4 mid
        // peaking bands between the bass/treble shelves, same
        // read-step-reinit-process shape as every other EQ band in this
        // file. No loudness component here — that only ever shapes the two
        // end shelves (see combinedLowDb/combinedHighDb above).
        const float requestedMasterEqBand1Db = self->m_requestedMasterEqBand1Db.load(std::memory_order_relaxed);
        if (std::fabs(requestedMasterEqBand1Db - self->m_appliedMasterEqBand1Db) > kEqGainEpsilonDb) {
            const float steppedDb
                = stepTowardDb(self->m_appliedMasterEqBand1Db, requestedMasterEqBand1Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kMasterEqBand1Freq);
            if (ma_peak2_reinit(&cfg, &self->m_masterEqBand1) == MA_SUCCESS)
                self->m_appliedMasterEqBand1Db = steppedDb;
        }
        ma_peak2_process_pcm_frames(&self->m_masterEqBand1, buf, buf, frames);

        const float requestedMasterEqBand2Db = self->m_requestedMasterEqBand2Db.load(std::memory_order_relaxed);
        if (std::fabs(requestedMasterEqBand2Db - self->m_appliedMasterEqBand2Db) > kEqGainEpsilonDb) {
            const float steppedDb
                = stepTowardDb(self->m_appliedMasterEqBand2Db, requestedMasterEqBand2Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kMasterEqBand2Freq);
            if (ma_peak2_reinit(&cfg, &self->m_masterEqBand2) == MA_SUCCESS)
                self->m_appliedMasterEqBand2Db = steppedDb;
        }
        ma_peak2_process_pcm_frames(&self->m_masterEqBand2, buf, buf, frames);

        const float requestedMasterEqBand3Db = self->m_requestedMasterEqBand3Db.load(std::memory_order_relaxed);
        if (std::fabs(requestedMasterEqBand3Db - self->m_appliedMasterEqBand3Db) > kEqGainEpsilonDb) {
            const float steppedDb
                = stepTowardDb(self->m_appliedMasterEqBand3Db, requestedMasterEqBand3Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kMasterEqBand3Freq);
            if (ma_peak2_reinit(&cfg, &self->m_masterEqBand3) == MA_SUCCESS)
                self->m_appliedMasterEqBand3Db = steppedDb;
        }
        ma_peak2_process_pcm_frames(&self->m_masterEqBand3, buf, buf, frames);

        const float requestedMasterEqBand4Db = self->m_requestedMasterEqBand4Db.load(std::memory_order_relaxed);
        if (std::fabs(requestedMasterEqBand4Db - self->m_appliedMasterEqBand4Db) > kEqGainEpsilonDb) {
            const float steppedDb
                = stepTowardDb(self->m_appliedMasterEqBand4Db, requestedMasterEqBand4Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kMasterEqBand4Freq);
            if (ma_peak2_reinit(&cfg, &self->m_masterEqBand4) == MA_SUCCESS)
                self->m_appliedMasterEqBand4Db = steppedDb;
        }
        ma_peak2_process_pcm_frames(&self->m_masterEqBand4, buf, buf, frames);

        const float requestedTrebleDb = self->m_requestedTrebleDb.load(std::memory_order_relaxed);
        const float combinedHighDb = requestedTrebleDb + loudnessHighDb;
        if (std::fabs(combinedHighDb - self->m_appliedTrebleDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->m_appliedTrebleDb, combinedHighDb, kMaxEqDbStepPerCallback);
            const ma_hishelf2_config cfg = makeHighShelfConfig(steppedDb, kEqHighShelfFreq);
            if (ma_hishelf2_reinit(&cfg, &self->m_treble) == MA_SUCCESS)
                self->m_appliedTrebleDb = steppedDb;
        }
        ma_hishelf2_process_pcm_frames(&self->m_treble, buf, buf, frames);

        // Master volume, then the limiter (gain reduction computed once per
        // FRAME, not per channel — moving channels independently would
        // smear the stereo image), then an explicit clamp: the node
        // graph's own internal clamp-to-[-1,1] (see the noClip comment in
        // start()) only covers ITS OWN summing — bass-boost/loudness above
        // can still push this buffer out of range, so re-clamp here too.
        const float ceilingLinear = static_cast<float>(dbToLinear(self->m_requestedLimiterCeilingDb.load(std::memory_order_relaxed)));
        const float releaseCoeff
            = 1.0f - static_cast<float>(std::exp(-1.0 / (kLimiterReleaseSeconds * sampleRate())));

        // Leveler gain applied this callback was computed from the PREVIOUS
        // callback's measurement (feedback, not feedforward) — real AGC/
        // leveler hardware works the same way, and at a multi-second time
        // constant, one callback's worth of lag (a few ms) is completely
        // inaudible. This is what lets the leveler slot into the existing
        // per-frame loop with no restructuring: just another scalar
        // multiply alongside masterVolume, and another accumulator
        // alongside framePeak.
        const float levelerGain = self->m_levelerGain;

        // m_levelerScratch mirrors the pre-limiter, post-volume/leveler
        // signal — measuring the leveler's own input rather than the
        // limiter's output avoids a feedback loop (the leveler reacting to
        // compression the limiter only applies BECAUSE of how loud the
        // leveler already made things). Grows-only, matching
        // PullDataSource::scratch's pattern.
        if (self->m_levelerScratch.size() < frames * ch)
            self->m_levelerScratch.resize(frames * ch);

        // R128 side-chain copy of the FINAL post-limiter output (filled
        // inside the loop below as outL/outR are computed) -- grows only,
        // same pattern as m_levelerScratch above.
        if (self->m_r128Scratch.size() < frames * ch)
            self->m_r128Scratch.resize(frames * ch);

        float peakL = 0.0f;
        float peakR = 0.0f;
        float outputTruePeak = 0.0f; // this callback's max post-limiter true-peak estimate, across both channels
        const size_t lookaheadFrames = static_cast<size_t>(kLimiterLookaheadFrames);
        for (size_t i = 0; i < frames; ++i) {
            const float rawL = buf[i * ch + 0] * masterVolume * levelerGain;
            const float rawR = (ch > 1) ? buf[i * ch + 1] * masterVolume * levelerGain : rawL;
            self->m_levelerScratch[i * ch + 0] = rawL;
            if (ch > 1)
                self->m_levelerScratch[i * ch + 1] = rawR;

            // Multiband compressor -- 3-band LR4 split of rawL/rawR (see
            // m_xoverLowStage1's doc comment in MixEngine.h), each band
            // independently compressed, then recombined into
            // compressedL/compressedR below, which everything downstream
            // (true-peak estimate, limiter lookahead delay line) uses
            // INSTEAD of rawL/rawR from here on. Runs per-sample
            // (frameCount=1) on tiny stack scratch: a biquad's state
            // persists identically across calls whether invoked once for N
            // frames or N times for 1 frame each, so this is
            // mathematically equivalent to bulk-processing while adding
            // zero new heap scratch (the gain computation below needs
            // per-sample values anyway). The leveler's side-chain above
            // measures rawL/rawR -- pre-compression, deliberately (see
            // m_levelerScratch's comment): the leveler and compressor sit
            // in the same causal position relative to each other as the
            // leveler and limiter already do.
            float compressedL = rawL;
            float compressedR = rawR;
            if (self->m_compressorEnabled.load(std::memory_order_relaxed)) {
                float lowSample[2] = { rawL, rawR };
                ma_lpf2_process_pcm_frames(&self->m_xoverLowStage1, lowSample, lowSample, 1);
                ma_lpf2_process_pcm_frames(&self->m_xoverLowStage2, lowSample, lowSample, 1);

                float midHighSample[2] = { rawL, rawR };
                ma_hpf2_process_pcm_frames(&self->m_xoverMidHighStage1, midHighSample, midHighSample, 1);
                ma_hpf2_process_pcm_frames(&self->m_xoverMidHighStage2, midHighSample, midHighSample, 1);

                float midSample[2] = { midHighSample[0], midHighSample[1] };
                ma_lpf2_process_pcm_frames(&self->m_xoverMidStage1, midSample, midSample, 1);
                ma_lpf2_process_pcm_frames(&self->m_xoverMidStage2, midSample, midSample, 1);

                float highSample[2] = { midHighSample[0], midHighSample[1] };
                ma_hpf2_process_pcm_frames(&self->m_xoverHighStage1, highSample, highSample, 1);
                ma_hpf2_process_pcm_frames(&self->m_xoverHighStage2, highSample, highSample, 1);

                // Per-band level detection: max across channels (a stereo
                // compressor must move both channels together per band, or
                // it smears the stereo image -- same reasoning as
                // framePeakEstimate's max-across-channels below), fed
                // through a one-pole EMA (computeLevelerRmsEstimate()'s
                // shape) as the detector -- sustained-level detection, not
                // the limiter's lookahead-peak machinery.
                const float lowLevel = std::max(lowSample[0] * lowSample[0], lowSample[1] * lowSample[1]);
                const float midLevel = std::max(midSample[0] * midSample[0], midSample[1] * midSample[1]);
                const float highLevel = std::max(highSample[0] * highSample[0], highSample[1] * highSample[1]);
                self->m_lowBandLevelEstimate
                    = computeLevelerRmsEstimate(self->m_lowBandLevelEstimate, lowLevel, kCompressorDetectorCoeff);
                self->m_midBandLevelEstimate
                    = computeLevelerRmsEstimate(self->m_midBandLevelEstimate, midLevel, kCompressorDetectorCoeff);
                self->m_highBandLevelEstimate
                    = computeLevelerRmsEstimate(self->m_highBandLevelEstimate, highLevel, kDeEsserDetectorCoeff);

                self->m_lowBandGainReduction = computeBandCompressorGainReduction(self->m_lowBandGainReduction,
                    self->m_lowBandLevelEstimate, self->m_requestedLowBandThresholdDb.load(std::memory_order_relaxed),
                    self->m_requestedLowBandRatio.load(std::memory_order_relaxed),
                    static_cast<float>(kCompressorKneeWidthDb), kCompressorAttackCoeff, kCompressorReleaseCoeff);
                self->m_midBandGainReduction = computeBandCompressorGainReduction(self->m_midBandGainReduction,
                    self->m_midBandLevelEstimate, self->m_requestedMidBandThresholdDb.load(std::memory_order_relaxed),
                    self->m_requestedMidBandRatio.load(std::memory_order_relaxed),
                    static_cast<float>(kCompressorKneeWidthDb), kCompressorAttackCoeff, kCompressorReleaseCoeff);
                self->m_highBandGainReduction = computeBandCompressorGainReduction(self->m_highBandGainReduction,
                    self->m_highBandLevelEstimate, self->m_requestedHighBandThresholdDb.load(std::memory_order_relaxed),
                    self->m_requestedHighBandRatio.load(std::memory_order_relaxed),
                    static_cast<float>(kDeEsserKneeWidthDb), kDeEsserAttackCoeff, kDeEsserReleaseCoeff);

                compressedL = lowSample[0] * self->m_lowBandGainReduction + midSample[0] * self->m_midBandGainReduction
                    + highSample[0] * self->m_highBandGainReduction;
                compressedR = lowSample[1] * self->m_lowBandGainReduction + midSample[1] * self->m_midBandGainReduction
                    + highSample[1] * self->m_highBandGainReduction;
            }

            // True-peak estimate for the half-sample point trailing each
            // channel's own history (see estimateTruePeak()'s doc comment)
            // — NOT the raw sample-peak alone, so the lookahead window
            // below can catch an inter-sample over a plain reader would
            // miss, same as it would catch an ordinary sample peak.
            const float peakEstimateL = estimateTruePeak(
                self->m_peakHistoryL[0], self->m_peakHistoryL[1], self->m_peakHistoryL[2], compressedL);
            self->m_peakHistoryL[0] = self->m_peakHistoryL[1];
            self->m_peakHistoryL[1] = self->m_peakHistoryL[2];
            self->m_peakHistoryL[2] = compressedL;
            float framePeakEstimate = peakEstimateL;
            if (ch > 1) {
                const float peakEstimateR = estimateTruePeak(
                    self->m_peakHistoryR[0], self->m_peakHistoryR[1], self->m_peakHistoryR[2], compressedR);
                self->m_peakHistoryR[0] = self->m_peakHistoryR[1];
                self->m_peakHistoryR[1] = self->m_peakHistoryR[2];
                self->m_peakHistoryR[2] = compressedR;
                framePeakEstimate = std::max(framePeakEstimate, peakEstimateR);
            }

            // Read the OLDEST entry (about to be overwritten) before
            // writing this frame's new one — that's exactly
            // kLimiterLookaheadFrames old, i.e. what should be emitted
            // NOW, having already sat in the delay line for the full
            // lookahead window's duration.
            const size_t p = self->m_lookaheadWritePos;
            const float delayedRawL = self->m_lookaheadDelayLine[p * ch + 0];
            const float delayedRawR = (ch > 1) ? self->m_lookaheadDelayLine[p * ch + 1] : delayedRawL;
            self->m_lookaheadDelayLine[p * ch + 0] = compressedL;
            if (ch > 1)
                self->m_lookaheadDelayLine[p * ch + 1] = compressedR;
            self->m_lookaheadPeakWindow[p] = framePeakEstimate;
            self->m_lookaheadWritePos = (p + 1) % lookaheadFrames;

            // Scanning the whole window's small, fixed size (240 entries)
            // every sample is cheap (see kLimiterLookaheadFrames's doc
            // comment) — this MAX, not a single frame's own peak, is what
            // gives the limiter real pre-ramping: a loud frame raises this
            // max (and so the gain reduction, via the instant-attack
            // branch below) the instant it's SEEN, long before it reaches
            // the delayed output position being emitted this iteration.
            float windowMaxPeak = 0.0f;
            for (size_t w = 0; w < lookaheadFrames; ++w)
                windowMaxPeak = std::max(windowMaxPeak, self->m_lookaheadPeakWindow[w]);

            self->m_limiterGainReduction = computeLimiterGainReduction(
                self->m_limiterGainReduction, windowMaxPeak, ceilingLinear, releaseCoeff);

            const float outL = std::clamp(delayedRawL * self->m_limiterGainReduction, -1.0f, 1.0f);
            buf[i * ch + 0] = outL;
            peakL = std::max(peakL, std::fabs(outL));
            self->m_r128Scratch[i * ch + 0] = outL;
            float outR = outL;
            if (ch > 1) {
                outR = std::clamp(delayedRawR * self->m_limiterGainReduction, -1.0f, 1.0f);
                buf[i * ch + 1] = outR;
                peakR = std::max(peakR, std::fabs(outR));
                self->m_r128Scratch[i * ch + 1] = outR;
            }

            // Post-limiter true-peak tracking -- an independent Catmull-Rom
            // history from m_peakHistoryL/R above (which feeds the LIMITER
            // off the pre-limiter raw signal); this measures what actually
            // came out the other end, matching what outputTruePeakDb()
            // reports.
            const float outputPeakEstimateL = estimateTruePeak(
                self->m_outputPeakHistoryL[0], self->m_outputPeakHistoryL[1], self->m_outputPeakHistoryL[2], outL);
            self->m_outputPeakHistoryL[0] = self->m_outputPeakHistoryL[1];
            self->m_outputPeakHistoryL[1] = self->m_outputPeakHistoryL[2];
            self->m_outputPeakHistoryL[2] = outL;
            outputTruePeak = std::max(outputTruePeak, outputPeakEstimateL);
            if (ch > 1) {
                const float outputPeakEstimateR = estimateTruePeak(self->m_outputPeakHistoryR[0],
                    self->m_outputPeakHistoryR[1], self->m_outputPeakHistoryR[2], outR);
                self->m_outputPeakHistoryR[0] = self->m_outputPeakHistoryR[1];
                self->m_outputPeakHistoryR[1] = self->m_outputPeakHistoryR[2];
                self->m_outputPeakHistoryR[2] = outR;
                outputTruePeak = std::max(outputTruePeak, outputPeakEstimateR);
            }
        }
        self->m_masterPeakL.store(peakL, std::memory_order_relaxed);
        self->m_masterPeakR.store(peakR, std::memory_order_relaxed);
        self->m_limiterGainReductionDb.store(
            static_cast<float>(20.0 * std::log10(static_cast<double>(self->m_limiterGainReduction))),
            std::memory_order_relaxed);
        self->m_outputTruePeakLinear.store(outputTruePeak, std::memory_order_relaxed);
        self->m_lowBandGainReductionDb.store(
            static_cast<float>(20.0 * std::log10(static_cast<double>(self->m_lowBandGainReduction))),
            std::memory_order_relaxed);
        self->m_midBandGainReductionDb.store(
            static_cast<float>(20.0 * std::log10(static_cast<double>(self->m_midBandGainReduction))),
            std::memory_order_relaxed);
        self->m_highBandGainReductionDb.store(
            static_cast<float>(20.0 * std::log10(static_cast<double>(self->m_highBandGainReduction))),
            std::memory_order_relaxed);

        // K-weight the side-chain copy (biquads process strictly in order,
        // so this can only happen once the whole callback's worth of
        // samples is populated, not interleaved into the loop above) and
        // measure THAT for the leveler, instead of the flat, un-weighted
        // signal — see kKWeightStage1B0's doc comment.
        float* levelerBuf = self->m_levelerScratch.data();
        ma_biquad_process_pcm_frames(&self->m_levelerKStage1, levelerBuf, levelerBuf, frames);
        ma_biquad_process_pcm_frames(&self->m_levelerKStage2, levelerBuf, levelerBuf, frames);
        double sumSquares = 0.0;
        for (size_t i = 0; i < frames * ch; ++i)
            sumSquares += static_cast<double>(levelerBuf[i]) * levelerBuf[i];

        // Once-per-callback leveler update, for the NEXT callback — no
        // per-frame precision needed at a multi-second time constant. See
        // computeLevelerRmsEstimate()/computeLevelerGainDb()'s own doc
        // comments for the mean-square-vs-amplitude-dB distinction.
        const double meanSquare = frames > 0 ? sumSquares / static_cast<double>(frames * ch) : 0.0;
        const double timeConstantSeconds
            = static_cast<double>(self->m_levelerTimeConstantSeconds.load(std::memory_order_relaxed));
        const float smoothingCoeff = 1.0f
            - static_cast<float>(std::exp(-(static_cast<double>(frames) / sampleRate()) / timeConstantSeconds));
        self->m_levelerRmsEstimate
            = computeLevelerRmsEstimate(self->m_levelerRmsEstimate, static_cast<float>(meanSquare), smoothingCoeff);

        const float gainDb = self->m_levelerEnabled.load(std::memory_order_relaxed)
            ? computeLevelerGainDb(self->m_levelerRmsEstimate, static_cast<float>(kLevelerTargetMeanSquare),
                self->m_requestedLevelerRangeDb.load(std::memory_order_relaxed),
                static_cast<float>(kLevelerNoiseFloorMeanSquare))
            : 0.0f;
        self->m_levelerGain = static_cast<float>(dbToLinear(static_cast<double>(gainDb)));
        self->m_levelerGainDb.store(gainDb, std::memory_order_relaxed);

        // EBU R128/LUFS metering: K-weight the post-limiter side-chain
        // copy (m_r128Scratch, filled inside the loop above) the same way
        // the leveler's own side-chain was just K-weighted above, then fold
        // it into rolling 100ms segments. A reset request is consumed once
        // per callback, before any of THIS callback's segments get folded
        // into the history below -- so the very first sample after a reset
        // genuinely starts a fresh integrated-loudness measurement rather
        // than including a stray already-elapsed segment.
        if (self->m_r128IntegratedResetRequested.exchange(false, std::memory_order_relaxed))
            self->m_r128SegmentHistoryCount.store(0, std::memory_order_release);

        float* r128Buf = self->m_r128Scratch.data();
        ma_biquad_process_pcm_frames(&self->m_r128KStage1, r128Buf, r128Buf, frames);
        ma_biquad_process_pcm_frames(&self->m_r128KStage2, r128Buf, r128Buf, frames);

        for (size_t i = 0; i < frames; ++i) {
            const double kwL = static_cast<double>(r128Buf[i * ch + 0]);
            const double kwR = ch > 1 ? static_cast<double>(r128Buf[i * ch + 1]) : kwL;
            self->m_r128SegmentSumSquaresL += kwL * kwL;
            self->m_r128SegmentSumSquaresR += kwR * kwR;

            if (++self->m_r128SegmentFrameCount < static_cast<size_t>(kR128SegmentFrames))
                continue;

            // Segment complete (100ms) -- finalize it.
            const double segmentMeanSquareL = self->m_r128SegmentSumSquaresL / kR128SegmentFrames;
            const double segmentMeanSquareR = self->m_r128SegmentSumSquaresR / kR128SegmentFrames;
            self->m_r128SegmentSumSquaresL = 0.0;
            self->m_r128SegmentSumSquaresR = 0.0;
            self->m_r128SegmentFrameCount = 0;

            // SUMMED, not averaged -- BS.1770's channel weight is 1.0 for
            // each of stereo L/R, so loudness = -0.691 +
            // 10*log10(sum of weighted per-channel mean squares).
            // Averaging here (dividing by 2) would silently make every
            // reading 3.01dB too quiet -- the exact bug an earlier design
            // review caught before any of this shipped.
            const float segmentValue = static_cast<float>(segmentMeanSquareL + segmentMeanSquareR);

            self->m_r128MomentarySegments[self->m_r128MomentaryIndex] = segmentValue;
            self->m_r128MomentaryIndex = (self->m_r128MomentaryIndex + 1) % kR128MomentarySegmentCount;
            self->m_r128ShortTermSegments[self->m_r128ShortTermIndex] = segmentValue;
            self->m_r128ShortTermIndex = (self->m_r128ShortTermIndex + 1) % kR128ShortTermSegmentCount;

            // Re-summed in full every 100ms rather than kept as a running
            // sum -- cheap at 4/30 entries, and avoids a running sum
            // slowly drifting over a multi-hour session (see
            // m_r128MomentarySegments' doc comment in MixEngine.h).
            double momentarySum = 0.0;
            for (float v : self->m_r128MomentarySegments)
                momentarySum += v;
            double shortTermSum = 0.0;
            for (float v : self->m_r128ShortTermSegments)
                shortTermSum += v;
            self->m_r128MomentaryLufs.store(
                lufsFromMeanSquare(momentarySum / kR128MomentarySegmentCount), std::memory_order_relaxed);
            self->m_r128ShortTermLufs.store(
                lufsFromMeanSquare(shortTermSum / kR128ShortTermSegmentCount), std::memory_order_relaxed);

            // Publish into the fixed-capacity integrated-loudness history
            // -- relaxed store of the value, THEN a release-store of the
            // incremented count, so integratedLoudnessLufs()'s UI-thread
            // acquire-load of the count is guaranteed to also see this
            // value (see m_r128SegmentHistory's doc comment in
            // MixEngine.h).
            const size_t historyIndex = self->m_r128SegmentHistoryCount.load(std::memory_order_relaxed);
            if (historyIndex < self->m_r128SegmentHistory.size()) {
                self->m_r128SegmentHistory[historyIndex] = segmentValue;
                self->m_r128SegmentHistoryCount.store(historyIndex + 1, std::memory_order_release);
            }
            // else: history capacity (~12h) exhausted -- further segments
            // simply stop being recorded into the integrated measurement;
            // momentary/short-term above are unaffected either way.
        }
    }

    // Single graph read (now also master-bus-processed above) feeds two
    // destinations (device output already written; streaming encoder feed
    // below) — deliberately captured AFTER master processing so the stream
    // carries the same bass-boost/loudness/volume as local monitoring,
    // matching the pre-existing "local = stream" design (the node graph
    // has no concept of multiple simultaneous output endpoints, so reading
    // it a second time would double-consume rather than mirror the mix).
    if (self->m_streamingRingBuffer && framesRead > 0)
        self->m_streamingRingBuffer->write(static_cast<const float*>(output), static_cast<size_t>(framesRead));

    // Same mix, mirrored to the optional backup/redundant streaming mount
    // (see readBackupMixedOutput()'s doc comment) — a second independent
    // ring buffer, not a second reader on the one above (RingBuffer is
    // explicitly single-producer/single-consumer).
    if (self->m_backupStreamingRingBuffer && framesRead > 0)
        self->m_backupStreamingRingBuffer->write(static_cast<const float*>(output), static_cast<size_t>(framesRead));

    // Same mix, mirrored to the optional monitor device (see
    // startMonitorDevice()'s doc comment) — this device's callback is the
    // sole writer to BOTH ring buffers, so this is still a one-writer/one-
    // reader relationship for each.
    if (self->m_monitorRingBuffer && framesRead > 0)
        self->m_monitorRingBuffer->write(static_cast<const float*>(output), static_cast<size_t>(framesRead));
}

ma_result MixEngine::pullRead(ma_data_source* dataSource, void* framesOut, ma_uint64 frameCount, ma_uint64* framesRead)
{
    auto* self = reinterpret_cast<PullDataSource*>(dataSource);
    auto* out = static_cast<float*>(framesOut);
    const size_t requested = static_cast<size_t>(frameCount);
    const size_t ch = static_cast<size_t>(channels());

    if (self->scratch.size() < requested * ch)
        self->scratch.resize(requested * ch);

    const size_t got = self->pull ? self->pull(self->scratch.data(), requested) : 0;
    if (got < requested) {
        // Never report a short/zero read for a still-live source:
        // miniaudio's own ma_data_source_read_pcm_frames() loop has an
        // infinite-loop-detection safety net for sources that keep
        // returning 0 frames, intended for genuinely-empty/broken sources
        // — a live, momentarily-underrun source (e.g. GStreamer hasn't
        // decoded the next buffer yet) should pad with silence instead of
        // risking that path treating it as ended.
        std::fill(self->scratch.begin() + static_cast<std::ptrdiff_t>(got * ch),
            self->scratch.begin() + static_cast<std::ptrdiff_t>(requested * ch), 0.0f);
    }

    if (self->hasProcessing) {
        // Re-checked every callback, not gated behind a "did the request
        // change" latch: gradual stepping (see kMaxEqDbStepPerCallback)
        // means a single large gain change can take several callbacks to
        // converge, so appliedXGainDb needs to keep moving toward
        // requestedXDb on every callback until it does, not just once when
        // setEqBandGain() bumps the request. The epsilon check per band
        // already gives the same "skip when nothing to do" cost savings a
        // seq-gate would — matches the master-bus bass/treble blocks below,
        // which never had a seq-gate for the same reason.
        const float lowDb = self->requestedLowShelfGainDb.load(std::memory_order_relaxed);
        const float band2Db = self->requestedBand2GainDb.load(std::memory_order_relaxed);
        const float band3Db = self->requestedBand3GainDb.load(std::memory_order_relaxed);
        const float band4Db = self->requestedBand4GainDb.load(std::memory_order_relaxed);
        const float highDb = self->requestedHighShelfGainDb.load(std::memory_order_relaxed);

        if (std::fabs(lowDb - self->appliedLowShelfGainDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->appliedLowShelfGainDb, lowDb, kMaxEqDbStepPerCallback);
            const ma_loshelf2_config cfg = makeLowShelfConfig(steppedDb, kEqLowShelfFreq);
            if (ma_loshelf2_reinit(&cfg, &self->eqLowShelf) == MA_SUCCESS)
                self->appliedLowShelfGainDb = steppedDb;
        }
        if (std::fabs(band2Db - self->appliedBand2GainDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->appliedBand2GainDb, band2Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kEqBand2Freq);
            if (ma_peak2_reinit(&cfg, &self->eqBand2) == MA_SUCCESS)
                self->appliedBand2GainDb = steppedDb;
        }
        if (std::fabs(band3Db - self->appliedBand3GainDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->appliedBand3GainDb, band3Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kEqBand3Freq);
            if (ma_peak2_reinit(&cfg, &self->eqBand3) == MA_SUCCESS)
                self->appliedBand3GainDb = steppedDb;
        }
        if (std::fabs(band4Db - self->appliedBand4GainDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->appliedBand4GainDb, band4Db, kMaxEqDbStepPerCallback);
            const ma_peak2_config cfg = makePeakConfig(steppedDb, kEqBand4Freq);
            if (ma_peak2_reinit(&cfg, &self->eqBand4) == MA_SUCCESS)
                self->appliedBand4GainDb = steppedDb;
        }
        if (std::fabs(highDb - self->appliedHighShelfGainDb) > kEqGainEpsilonDb) {
            const float steppedDb = stepTowardDb(self->appliedHighShelfGainDb, highDb, kMaxEqDbStepPerCallback);
            const ma_hishelf2_config cfg = makeHighShelfConfig(steppedDb, kEqHighShelfFreq);
            if (ma_hishelf2_reinit(&cfg, &self->eqHighShelf) == MA_SUCCESS)
                self->appliedHighShelfGainDb = steppedDb;
        }

        // In-place, sequential 5-band chain — miniaudio's own source
        // explicitly documents that _process_pcm_frames() supports
        // pFramesOut == pFramesIn ("must support in-place filtering").
        float* buf = self->scratch.data();
        ma_loshelf2_process_pcm_frames(&self->eqLowShelf, buf, buf, requested);
        ma_peak2_process_pcm_frames(&self->eqBand2, buf, buf, requested);
        ma_peak2_process_pcm_frames(&self->eqBand3, buf, buf, requested);
        ma_peak2_process_pcm_frames(&self->eqBand4, buf, buf, requested);
        ma_hishelf2_process_pcm_frames(&self->eqHighShelf, buf, buf, requested);
    }

    // Pick up a newly-requested ramp, if any, since the last buffer —
    // always starting from wherever the gain actually is right now
    // (currentGain), not from whatever the previous target was, so back-
    // to-back setGain() calls (e.g. grabbing the manual crossfader mid-
    // automatic-ramp) blend continuously rather than jumping.
    const uint64_t seq = self->requestSeq.load(std::memory_order_acquire);
    if (seq != self->lastSeenSeq) {
        self->lastSeenSeq = seq;
        self->rampStartGain = self->currentGain;
        self->rampTargetGain = self->requestedTargetGain.load(std::memory_order_relaxed);
        self->rampFramesTotal = self->requestedRampFrames.load(std::memory_order_relaxed);
        self->rampCurve = self->requestedRampCurve.load(std::memory_order_relaxed);
        self->rampFramesElapsed = 0;
        if (self->rampFramesTotal == 0)
            self->currentGain = self->rampTargetGain; // instant, no ramp requested
    }

    // Trim (deck volume * auto-gain-compensation, see MixEngine::setTrim()'s
    // doc comment) — read once per buffer, not ramped: this is driven by
    // discrete UI actions, not something that needs crossfade-style
    // smoothing the way the ramped gain above does. duckGain is a THIRD,
    // independent multiplier (see setDuckGain()'s doc comment for why it's
    // not folded into trim) — DuckingController steps it toward its own
    // target on its own timer, same "discrete control input, no ramp here"
    // reasoning as trim.
    const float trim = self->requestedTrim.load(std::memory_order_relaxed);
    const float duckGain = self->requestedDuckGain.load(std::memory_order_relaxed);

    float peakL = 0.0f;
    float peakR = 0.0f;

    for (size_t i = 0; i < requested; ++i) {
        float gain;
        if (self->rampFramesElapsed < self->rampFramesTotal) {
            const float t = static_cast<float>(self->rampFramesElapsed) / static_cast<float>(self->rampFramesTotal);
            // EqualPower isn't expressible via shapeRampProgress()'s
            // shared shape-then-lerp formula — see RampCurve's and
            // computeEqualPowerGain()'s doc comments.
            if (self->rampCurve == RampCurve::EqualPower) {
                gain = computeEqualPowerGain(t, self->rampStartGain, self->rampTargetGain);
            } else {
                const float shapedT = shapeRampProgress(t, self->rampCurve);
                gain = self->rampStartGain + (self->rampTargetGain - self->rampStartGain) * shapedT;
            }
            ++self->rampFramesElapsed;
            self->currentGain = (self->rampFramesElapsed >= self->rampFramesTotal) ? self->rampTargetGain : gain;
        } else {
            gain = self->currentGain;
        }
        for (size_t c = 0; c < ch; ++c) {
            const float sample = self->scratch[i * ch + c] * gain * trim * duckGain;
            out[i * ch + c] = sample;
            if (self->hasProcessing) {
                if (c == 0)
                    peakL = std::max(peakL, std::fabs(sample));
                else if (c == 1)
                    peakR = std::max(peakR, std::fabs(sample));
            }
        }
    }

    if (self->hasProcessing) {
        self->meterPeakL.store(peakL, std::memory_order_relaxed);
        self->meterPeakR.store(peakR, std::memory_order_relaxed);
    }

    if (framesRead)
        *framesRead = frameCount;
    return MA_SUCCESS;
}

ma_result MixEngine::pullGetDataFormat(ma_data_source* /*dataSource*/, ma_format* format, ma_uint32* channelsOut,
    ma_uint32* sampleRateOut, ma_channel* /*channelMap*/, size_t /*channelMapCap*/)
{
    if (format)
        *format = ma_format_f32;
    if (channelsOut)
        *channelsOut = static_cast<ma_uint32>(channels());
    if (sampleRateOut)
        *sampleRateOut = static_cast<ma_uint32>(sampleRate());
    return MA_SUCCESS;
}

ma_result MixEngine::pullSeek(ma_data_source* /*dataSource*/, ma_uint64 /*frameIndex*/)
{
    // Seeking happens on the decode side (GstSourcePipeline::seek()), not
    // through this real-time pull layer.
    return MA_NOT_IMPLEMENTED;
}

ma_result MixEngine::pullGetCursor(ma_data_source* /*dataSource*/, ma_uint64* cursor)
{
    if (cursor)
        *cursor = 0;
    return MA_NOT_IMPLEMENTED;
}

ma_result MixEngine::pullGetLength(ma_data_source* /*dataSource*/, ma_uint64* length)
{
    if (length)
        *length = 0;
    return MA_NOT_IMPLEMENTED;
}

// --- Output device management (Phase 2). ---------------------------------

bool MixEngine::initDevice(const ma_device_id* id)
{
    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = static_cast<ma_uint32>(channels());
    deviceConfig.sampleRate = static_cast<ma_uint32>(sampleRate());
    deviceConfig.dataCallback = &MixEngine::dataCallback;
    deviceConfig.notificationCallback = &MixEngine::deviceNotificationCallback;
    deviceConfig.pUserData = this;
    deviceConfig.playback.pDeviceID = id;
    // Deliberately NOT setting noClip: miniaudio's default (false) clamps
    // summed output to [-1,1] rather than letting it wrap/distort — this
    // directly addresses the "volume gets clipped" (harsh digital
    // distortion) symptom reported against the old GStreamer-audiomixer
    // architecture, which had no such protection at all.

    if (ma_device_init(&m_context, &deviceConfig, &m_device) == MA_SUCCESS)
        return true;

    if (id != nullptr) {
        // The persisted/selected device no longer exists (e.g. unplugged
        // since last run, or a USB device's id shifted — see
        // AudioDeviceInfo's doc comment) — degrade to the system default
        // rather than failing outright.
        RS_LOG_WARN("audio.pipeline",
            QStringLiteral("MixEngine: preferred playback device unavailable, falling back to system default"));
        deviceConfig.playback.pDeviceID = nullptr;
        return ma_device_init(&m_context, &deviceConfig, &m_device) == MA_SUCCESS;
    }
    return false;
}

std::vector<AudioDeviceInfo> MixEngine::enumeratePlaybackDevices()
{
    std::vector<AudioDeviceInfo> result;

    ma_backend nullBackend = ma_backend_null;
    const bool forceNull = !qgetenv("RS_AUDIO_SINK").isEmpty();

    ma_context context{};
    const ma_result contextResult = forceNull ? ma_context_init(&nullBackend, 1, nullptr, &context)
                                               : ma_context_init(nullptr, 0, nullptr, &context);
    if (contextResult != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize audio context for device enumeration (%1)").arg(contextResult));
        return result;
    }

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(&context, &playbackInfos, &playbackCount, nullptr, nullptr) == MA_SUCCESS) {
        result.reserve(playbackCount);
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            AudioDeviceInfo info;
            info.id = QByteArray(reinterpret_cast<const char*>(&playbackInfos[i].id), sizeof(ma_device_id));
            info.name = QString::fromUtf8(playbackInfos[i].name);
            info.isDefault = playbackInfos[i].isDefault != 0;
            result.push_back(std::move(info));
        }
    } else {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to enumerate playback devices"));
    }

    ma_context_uninit(&context);
    return result;
}

void MixEngine::setPreferredPlaybackDeviceId(const QByteArray& id)
{
    m_preferredPlaybackDeviceId = id;
}

bool MixEngine::switchPlaybackDevice(const QByteArray& id)
{
    ma_device_id localId;
    const ma_device_id* idPtr = nullptr;
    if (!id.isEmpty() && id.size() == sizeof(ma_device_id)) {
        std::memcpy(&localId, id.constData(), sizeof(ma_device_id));
        idPtr = &localId;
    }

    m_intentionalStop.store(true, std::memory_order_relaxed);
    if (m_deviceInitialized) {
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
    }

    if (!initDevice(idPtr)) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to switch playback device"));
        return false; // leaves m_deviceInitialized false -- caller sees silence rather than a half-open device; see pollForDeviceLoss(), the actual recovery path, for the "leave the old one running" case
    }
    m_deviceInitialized = true;

    if (ma_device_start(&m_device) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to start playback device after switch"));
        ma_device_uninit(&m_device);
        m_deviceInitialized = false;
        return false;
    }
    m_intentionalStop.store(false, std::memory_order_relaxed);
    m_lastCallbackEpochMs.store(steadyClockNowMs(), std::memory_order_relaxed);
    // Deliberately NOT updating m_preferredPlaybackDeviceId here: that field
    // only feeds the NEXT start() (see its own doc comment), and this
    // method is called both for a deliberate UI-driven switch (where the
    // settings dialog's own QSettings write is what makes the choice
    // persist across restarts, per this app's convention of dialogs owning
    // QSettings) AND for pollForDeviceLoss()'s automatic, involuntary
    // fallback-to-default — which must NOT silently overwrite the user's
    // actual saved preference just because the device dropped out
    // temporarily.
    return true;
}

void MixEngine::deviceNotificationCallback(const ma_device_notification* pNotification)
{
    // Deliberately does ONLY this one atomic write, nothing else — on this
    // dev machine's likely backend (ALSA), this callback can run on
    // ma_worker_thread, the SAME thread dataCallback() runs on. Calling
    // ma_device_uninit()/ma_device_init() synchronously from here would
    // have ma_device_uninit() try to join that thread FROM that thread —
    // a self-deadlock the moment a device is unexpectedly lost. The actual
    // recovery happens later, from pollForDeviceLoss() on a safe (UI
    // thread) context.
    if (pNotification->type != ma_device_notification_type_stopped)
        return;
    auto* self = static_cast<MixEngine*>(pNotification->pDevice->pUserData);
    if (!self->m_intentionalStop.load(std::memory_order_relaxed))
        self->m_deviceLostPending.store(true, std::memory_order_relaxed);
}

bool MixEngine::pollForDeviceLoss()
{
    const bool notifiedLoss = m_deviceLostPending.exchange(false, std::memory_order_relaxed);
    // Backstop for backends that don't reliably fire the stopped
    // notification on an actual unplug (traced against this vendored
    // miniaudio's ALSA backend: an unhandled poll() POLLERR in the write
    // path can spin without ever unwinding to fire it) — no callback
    // observed in over a second means the device is gone regardless of
    // whether the notification ever arrives. 1000ms is comfortably above
    // any realistic callback period (tens of milliseconds) while still
    // being a fast detection window.
    const bool heartbeatStale = m_deviceInitialized
        && (steadyClockNowMs() - m_lastCallbackEpochMs.load(std::memory_order_relaxed)) > 1000;

    if (!notifiedLoss && !heartbeatStale)
        return false;

    RS_LOG_WARN("audio.pipeline", QStringLiteral("MixEngine: playback device lost — falling back to system default"));
    m_deviceLostRecoverySucceeded = switchPlaybackDevice(QByteArray());
    if (!m_deviceLostRecoverySucceeded)
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to recover after playback device loss"));
    return true;
}

bool MixEngine::deviceLostRecoverySucceeded() const
{
    return m_deviceLostRecoverySucceeded;
}

bool MixEngine::startMonitorDevice(const QByteArray& id)
{
    stopMonitorDevice();

    ma_device_id localId;
    const ma_device_id* idPtr = nullptr;
    if (!id.isEmpty() && id.size() == sizeof(ma_device_id)) {
        std::memcpy(&localId, id.constData(), sizeof(ma_device_id));
        idPtr = &localId;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format = ma_format_f32;
    deviceConfig.playback.channels = static_cast<ma_uint32>(channels());
    deviceConfig.sampleRate = static_cast<ma_uint32>(sampleRate());
    deviceConfig.dataCallback = &MixEngine::monitorDataCallback;
    deviceConfig.pUserData = this;
    deviceConfig.playback.pDeviceID = idPtr;

    if (ma_device_init(&m_context, &deviceConfig, &m_monitorDevice) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize monitor output device"));
        return false;
    }

    m_monitorRingBuffer = std::make_unique<RingBuffer>(kRingBufferCapacityFrames, channels());

    if (ma_device_start(&m_monitorDevice) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to start monitor output device"));
        ma_device_uninit(&m_monitorDevice);
        m_monitorRingBuffer.reset();
        return false;
    }

    m_monitorDeviceInitialized = true;
    RS_LOG_INFO("audio.pipeline", QStringLiteral("MixEngine: monitor output device started"));
    return true;
}

void MixEngine::stopMonitorDevice()
{
    if (m_monitorDeviceInitialized) {
        ma_device_uninit(&m_monitorDevice);
        m_monitorDeviceInitialized = false;
    }
    m_monitorRingBuffer.reset();
}

void MixEngine::monitorDataCallback(ma_device* device, void* output, const void* /*input*/, ma_uint32 frameCount)
{
    auto* self = static_cast<MixEngine*>(device->pUserData);
    auto* out = static_cast<float*>(output);

    const size_t got = self->m_monitorRingBuffer ? self->m_monitorRingBuffer->read(out, frameCount) : 0;
    if (got < static_cast<size_t>(frameCount)) {
        // Never treat a short read as fatal (same convention RingBuffer's
        // own doc comment establishes and pullRead() already follows for
        // source underruns) — pad the shortfall with silence.
        const size_t ch = static_cast<size_t>(channels());
        std::fill(out + got * ch, out + static_cast<size_t>(frameCount) * ch, 0.0f);
    }
}

// --- Microphone input (Phase 4). -----------------------------------------

std::vector<AudioDeviceInfo> MixEngine::enumerateCaptureDevices()
{
    std::vector<AudioDeviceInfo> result;

    ma_backend nullBackend = ma_backend_null;
    const bool forceNull = !qgetenv("RS_AUDIO_SINK").isEmpty();

    ma_context context{};
    const ma_result contextResult = forceNull ? ma_context_init(&nullBackend, 1, nullptr, &context)
                                               : ma_context_init(nullptr, 0, nullptr, &context);
    if (contextResult != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline",
            QStringLiteral("MixEngine: failed to initialize audio context for capture device enumeration (%1)")
                .arg(contextResult));
        return result;
    }

    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;
    if (ma_context_get_devices(&context, nullptr, nullptr, &captureInfos, &captureCount) == MA_SUCCESS) {
        result.reserve(captureCount);
        for (ma_uint32 i = 0; i < captureCount; ++i) {
            AudioDeviceInfo info;
            info.id = QByteArray(reinterpret_cast<const char*>(&captureInfos[i].id), sizeof(ma_device_id));
            info.name = QString::fromUtf8(captureInfos[i].name);
            info.isDefault = captureInfos[i].isDefault != 0;
            result.push_back(std::move(info));
        }
    } else {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to enumerate capture devices"));
    }

    ma_context_uninit(&context);
    return result;
}

bool MixEngine::startMicInput(const QByteArray& id)
{
    stopMicInput();

    ma_device_id localId;
    const ma_device_id* idPtr = nullptr;
    if (!id.isEmpty() && id.size() == sizeof(ma_device_id)) {
        std::memcpy(&localId, id.constData(), sizeof(ma_device_id));
        idPtr = &localId;
    }

    ma_device_config deviceConfig = ma_device_config_init(ma_device_type_capture);
    deviceConfig.capture.format = ma_format_f32;
    deviceConfig.capture.channels = static_cast<ma_uint32>(channels());
    deviceConfig.sampleRate = static_cast<ma_uint32>(sampleRate());
    deviceConfig.dataCallback = &MixEngine::micCaptureDataCallback;
    deviceConfig.pUserData = this;
    deviceConfig.capture.pDeviceID = idPtr;

    if (ma_device_init(&m_context, &deviceConfig, &m_micDevice) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to initialize microphone capture device"));
        return false;
    }

    m_micRingBuffer = std::make_unique<RingBuffer>(kRingBufferCapacityFrames, channels());

    if (ma_device_start(&m_micDevice) != MA_SUCCESS) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("MixEngine: failed to start microphone capture device"));
        ma_device_uninit(&m_micDevice);
        m_micRingBuffer.reset();
        return false;
    }

    m_micDeviceInitialized = true;

    // From the graph's perspective the mic becomes an ordinary pull-based
    // source — withProcessing=true gives it the same 5-band EQ + VU
    // metering every deck already gets for free, no changes needed to
    // attachSource()/pullRead()/SourceEntry.
    RingBuffer* ringBuffer = m_micRingBuffer.get();
    attachSource(
        QStringLiteral("mic"), [ringBuffer](float* out, size_t frameCount) { return ringBuffer->read(out, frameCount); },
        1.0, /*withProcessing=*/true);

    RS_LOG_INFO("audio.pipeline", QStringLiteral("MixEngine: microphone input started"));
    return true;
}

void MixEngine::stopMicInput()
{
    if (m_micDeviceInitialized) {
        detachSource(QStringLiteral("mic"));
        ma_device_uninit(&m_micDevice);
        m_micDeviceInitialized = false;
    }
    m_micRingBuffer.reset();
}

void MixEngine::micCaptureDataCallback(ma_device* device, void* /*output*/, const void* input, ma_uint32 frameCount)
{
    auto* self = static_cast<MixEngine*>(device->pUserData);
    if (self->m_micRingBuffer && input)
        self->m_micRingBuffer->write(static_cast<const float*>(input), static_cast<size_t>(frameCount));
}

} // namespace radio::audio
