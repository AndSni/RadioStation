#include "audio/GstSourcePipeline.h"
#include "audio/MixEngine.h"

#include <QTest>
#include <QUrl>
#include <cmath>
#include <memory>
#include <vector>

#include <gst/gst.h>

using namespace radio::audio;

namespace {
double rms(const std::vector<float>& samples)
{
    if (samples.empty())
        return 0.0;
    double sumSquares = 0.0;
    for (float s : samples)
        sumSquares += static_cast<double>(s) * static_cast<double>(s);
    return std::sqrt(sumSquares / samples.size());
}

// A synthetic PullFn (not a real decoded file) generating a fixed-amplitude
// 1kHz sine -- gives exact per-channel control a fixture file can't (a
// fixture's mono content is always upmixed to identical L=R by
// GstSourcePipeline's audioconvert). stereoContent=false leaves the right
// channel silent.
MixEngine::PullFn makeSineSource(bool stereoContent)
{
    auto phase = std::make_shared<double>(0.0);
    return [stereoContent, phase](float* out, size_t frameCount) {
        constexpr double kAmplitude = 0.3;
        constexpr double kFrequencyHz = 1000.0;
        const double increment = 2.0 * M_PI * kFrequencyHz / MixEngine::sampleRate();
        for (size_t i = 0; i < frameCount; ++i) {
            const float sample = static_cast<float>(kAmplitude * std::sin(*phase));
            *phase += increment;
            out[i * 2 + 0] = sample;
            out[i * 2 + 1] = stereoContent ? sample : 0.0f;
        }
        return frameCount;
    };
}

// Drains whatever the streaming tap has accumulated so far, across a real
// wall-clock wait (the null backend paces callbacks to real time, same as
// a genuine device), and returns the RMS energy of everything collected.
double collectRms(MixEngine& engine, int waitMs)
{
    QTest::qWait(waitMs);

    std::vector<float> collected;
    float buf[4096];
    size_t got;
    while ((got = engine.readMixedOutput(buf, 2048)) > 0)
        collected.insert(collected.end(), buf, buf + got * MixEngine::channels());
    return rms(collected);
}
}

class MixEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void singleSourceProducesAudibleOutput();
    void twoSourcesMixLouderThanEither();
    void detachedSourceStopsContributing();
    void gainRampReachesTargetSmoothly();
    void curvedGainRampReachesTargetSmoothly();
    void shapeRampProgressLinearIsUnchanged();
    void shapeRampProgressSlowDownStartsFastEndsSlow();
    void shapeRampProgressSpeedUpStartsSlowEndsFast();
    void shapeRampProgressAllCurvesAgreeAtEndpoints();
    void computeEqualPowerGainMatchesCosSineAtEndpointsAndMidpoint();
    void computeEqualPowerGainConstantPowerAcrossFade();
    void eqBandBoostIncreasesEnergy();
    void eqBandGainRoundTrips();
    void eqBandGainIsZeroWithoutProcessing();
    void eqBandGainIsZeroForUnknownIdOrBand();
    void stepTowardDbClampsLargeStepToMax();
    void stepTowardDbReachesTargetWhenWithinRange();
    void stepTowardDbHandlesNegativeDirection();
    void eqGainEventuallyReachesLargeTarget();
    void trimReducesOutputProportionally();
    void deckPeakLevelReflectsAudibleSignal();
    void masterVolumeScalesOutput();
    void masterBassBoostIncreasesEnergy();
    void masterTrebleIncreasesEnergy();
    void masterBassBoostAndLoudnessCombineAdditively();
    void masterEqBandGainRoundTrips();
    void masterEqBandGainIsZeroForOutOfRangeBand();
    void masterMidBandBoostIncreasesEnergy();
    void masterHighPassAttenuatesSubsonicContent();
    void masterHighPassLeavesAudibleContentUnaffected();
    void estimateTruePeakDetectsInterSampleOverAbsentFromEitherSampleAlone();
    void estimateTruePeakEqualsSamplePeakForSlowlyVaryingSignal();
    void computeLimiterGainReductionSnapsDownImmediatelyOnAttack();
    void computeLimiterGainReductionRecoversGraduallyOnRelease();
    void computeLimiterGainReductionNeverExceedsUnity();
    void computeLimiterGainReductionPassesQuietSignalUnchanged();
    void limiterCapsPeakAtCeiling();
    void limiterLookaheadStartsWithSilentWarmup();
    void limiterLeavesQuietSignalUnreduced();
    void computeLevelerRmsEstimateConvergesTowardMeasuredValue();
    void computeLevelerRmsEstimateNeverOvershoots();
    void computeLevelerGainDbBoostsWhenBelowTarget();
    void computeLevelerGainDbAttenuatesWhenAboveTarget();
    void computeLevelerGainDbIsZeroAtTarget();
    void computeLevelerGainDbClampsToRange();
    void computeLevelerGainDbIsZeroBelowNoiseFloor();
    void computeLoudnessBoostFactorZeroAtUnity();
    void computeLoudnessBoostFactorFullAtOrBelowFloor();
    void computeLoudnessBoostFactorMonotonicBetween();
    void levelerBoostsAQuietSignalTowardTarget();
    void levelerAttenuatesALoudSignalTowardTarget();
    void levelerNeverBoostsNearSilence();
    void levelerRespondsMoreToHighFrequencyEnergyThanLowFrequencyEnergy();
    void levelerDisabledLeavesOutputUnchanged();

    void enumeratePlaybackDevicesReturnsAtLeastOneDeviceUnderNullBackend();
    void switchPlaybackDeviceRoundTripsWithoutDisruptingAnAttachedSource();
    void monitorDeviceStartsAndStopsCleanlyAlongsideTheAirDevice();

    void lufsFromMeanSquareMatchesClosedFormAndStaysFiniteAtSilence();
    void computeIntegratedLoudnessLufsReturnsZeroWithFewerThanFourSegments();
    void computeIntegratedLoudnessLufsMatchesClosedFormForAConstantLevelSignal();
    void computeIntegratedLoudnessLufsSummedNotAveragedAcrossChannels();
    void computeIntegratedLoudnessLufsGatesOutSilentSegments();
    void momentaryLoudnessLufsRespondsToSignalLevel();
    void resetIntegratedLoudnessMeasurementRestartsTheHistory();
    void outputTruePeakDbReflectsThePostLimiterCeiling();

    void computeBandCompressorGainReductionPassesQuietSignalAtUnity();
    void computeBandCompressorGainReductionMatchesClosedFormAboveTheKnee();
    void computeBandCompressorGainReductionCollapsesToLimiterBehaviorAtHighRatio();
    void computeBandCompressorGainReductionSilenceConvergesToUnity();
    void multibandCompressorReducesOnlyTheDrivenBand();
    void multibandCompressorIsTransparentWhenThresholdsAreHigh();

    void micInputStartsAndStopsCleanlyAlongsideTheAirDevice();
    void setDuckGainIsIndependentFromTrim();

private:
    std::unique_ptr<GstSourcePipeline> makePlayingSource(const QString& fixture);

    std::vector<std::unique_ptr<GstSourcePipeline>> m_pipelines; // kept alive for the running test
};

void MixEngineTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    qputenv("RS_AUDIO_SINK", "null"); // never touch real hardware in this test
}

void MixEngineTest::init()
{
    m_pipelines.clear();
}

void MixEngineTest::cleanup()
{
    m_pipelines.clear();
}

std::unique_ptr<GstSourcePipeline> MixEngineTest::makePlayingSource(const QString& fixture)
{
    auto pipeline = std::make_unique<GstSourcePipeline>();
    if (!pipeline->build())
        return nullptr;
    pipeline->loadUri(QUrl::fromLocalFile(fixture).toString());
    pipeline->play();
    return pipeline;
}

void MixEngineTest::singleSourceProducesAudibleOutput()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(QStringLiteral("A"),
        [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    const double energy = collectRms(engine, 500);
    QVERIFY2(energy > 0.01, qPrintable(QStringLiteral("expected audible output, got RMS=%1").arg(energy)));

    engine.stop();
}

void MixEngineTest::twoSourcesMixLouderThanEither()
{
    // The actual point of this rewrite's Phase 1: prove real summation
    // happens (two independent GstSourcePipelines, each its own pipeline,
    // mixed via MixEngine's node graph), not just "didn't crash."
    MixEngine soloEngine;
    QVERIFY(soloEngine.start());
    soloEngine.setCompressorEnabled(false); // this test is about summation, not the compressor
    auto soloSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(soloSource != nullptr);
    GstSourcePipeline* soloRaw = soloSource.get();
    m_pipelines.push_back(std::move(soloSource));
    soloEngine.attachSource(QStringLiteral("A"),
        [soloRaw](float* out, size_t frameCount) { return soloRaw->read(out, frameCount); });
    const double soloEnergy = collectRms(soloEngine, 500);
    soloEngine.stop();

    MixEngine mixedEngine;
    QVERIFY(mixedEngine.start());
    // Two summed tones can peak well above the limiter's default -0.3dB
    // ceiling — this test is about summation, not the limiter, so push the
    // ceiling out of the way (2 summed full-scale tones top out around
    // +6dB; +20dB leaves it inert here).
    mixedEngine.setLimiterCeilingDb(20.0);
    mixedEngine.setCompressorEnabled(false); // this test is about summation, not the compressor
    auto a = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    auto b = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    QVERIFY(a != nullptr);
    QVERIFY(b != nullptr);
    GstSourcePipeline* aRaw = a.get();
    GstSourcePipeline* bRaw = b.get();
    m_pipelines.push_back(std::move(a));
    m_pipelines.push_back(std::move(b));
    mixedEngine.attachSource(QStringLiteral("A"), [aRaw](float* out, size_t frameCount) { return aRaw->read(out, frameCount); });
    mixedEngine.attachSource(QStringLiteral("B"), [bRaw](float* out, size_t frameCount) { return bRaw->read(out, frameCount); });
    const double mixedEnergy = collectRms(mixedEngine, 500);
    mixedEngine.stop();

    // Threshold lowered from 1.2 to 1.15: the master bus now applies an
    // explicit clamp-to-[-1,1] to the buffer captured for streaming (see
    // MixEngine::dataCallback()) — previously that capture happened before
    // miniaudio's own device-level noClip protection (which only clips
    // *after* the data callback returns, confirmed by reading its doc
    // comment), so this measurement was unknowingly reading an unclamped
    // sum. A properly clamped mix of these two tones lands at ~1.18x, not
    // the old unclamped ~1.2x+ — the lowered threshold reflects the correct
    // (now clip-protected) behavior, not a weakened test; it still requires
    // a real, substantial loudness increase from summing two sources.
    QVERIFY2(mixedEnergy > soloEnergy * 1.15,
        qPrintable(QStringLiteral("expected two summed tones louder than one (solo=%1, mixed=%2)")
                       .arg(soloEnergy)
                       .arg(mixedEnergy)));
}

void MixEngineTest::detachedSourceStopsContributing()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    QVERIFY(collectRms(engine, 300) > 0.01);

    engine.detachSource(QStringLiteral("A"));
    // Drain anything already in flight from before the detach, then verify
    // a FRESH window is silent.
    collectRms(engine, 100);
    const double afterDetach = collectRms(engine, 300);
    QVERIFY2(afterDetach < 0.001, qPrintable(QStringLiteral("expected silence after detach, got RMS=%1").arg(afterDetach)));

    engine.stop();
}

void MixEngineTest::gainRampReachesTargetSmoothly()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
        /*initialGain=*/0.0);
    const double silentEnergy = collectRms(engine, 200);
    QVERIFY2(silentEnergy < 0.001, qPrintable(QStringLiteral("expected silence at gain 0, got RMS=%1").arg(silentEnergy)));

    engine.setGain(QStringLiteral("A"), 1.0, 100);
    const double afterRamp = collectRms(engine, 400); // well past the 100ms ramp
    QVERIFY2(afterRamp > 0.01, qPrintable(QStringLiteral("expected audible output after ramping to gain 1.0, got RMS=%1").arg(afterRamp)));

    engine.stop();
}

void MixEngineTest::curvedGainRampReachesTargetSmoothly()
{
    // Regression coverage for the curve parameter threaded through setGain()
    // — SlowDown/SpeedUp still have to actually reach the target gain by the
    // time the ramp duration elapses, same as Linear already proves above.
    for (RampCurve curve : { RampCurve::SlowDown, RampCurve::SpeedUp, RampCurve::EqualPower }) {
        MixEngine engine;
        QVERIFY(engine.start());

        auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
        QVERIFY(source != nullptr);
        GstSourcePipeline* raw = source.get();
        m_pipelines.push_back(std::move(source));

        engine.attachSource(
            QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
            /*initialGain=*/0.0);
        engine.setGain(QStringLiteral("A"), 1.0, 100, curve);
        const double afterRamp = collectRms(engine, 400); // well past the 100ms ramp
        QVERIFY2(afterRamp > 0.01,
            qPrintable(QStringLiteral("curve %1: expected audible output after ramping to gain 1.0, got RMS=%2")
                           .arg(static_cast<int>(curve))
                           .arg(afterRamp)));

        engine.stop();
    }
}

void MixEngineTest::shapeRampProgressLinearIsUnchanged()
{
    QCOMPARE(MixEngine::shapeRampProgress(0.0f, RampCurve::Linear), 0.0f);
    QCOMPARE(MixEngine::shapeRampProgress(0.25f, RampCurve::Linear), 0.25f);
    QCOMPARE(MixEngine::shapeRampProgress(0.5f, RampCurve::Linear), 0.5f);
    QCOMPARE(MixEngine::shapeRampProgress(0.75f, RampCurve::Linear), 0.75f);
    QCOMPARE(MixEngine::shapeRampProgress(1.0f, RampCurve::Linear), 1.0f);
}

void MixEngineTest::shapeRampProgressSlowDownStartsFastEndsSlow()
{
    // "Fades fast at first, slows toward the end" — ahead of linear in the
    // first half (already made more progress than a straight line would by
    // this point), behind linear's own PACE by the second half (still
    // moving, just less per unit time than it did early on).
    const float quarter = MixEngine::shapeRampProgress(0.25f, RampCurve::SlowDown);
    const float threeQuarters = MixEngine::shapeRampProgress(0.75f, RampCurve::SlowDown);
    QVERIFY2(quarter > 0.25f, qPrintable(QStringLiteral("expected SlowDown ahead of linear at t=0.25, got %1").arg(quarter)));
    QVERIFY2(threeQuarters > 0.75f,
        qPrintable(QStringLiteral("expected SlowDown still ahead of linear at t=0.75, got %1").arg(threeQuarters)));
    // The defining shape: more progress made in the first quarter than the last.
    const float firstQuarterDelta = quarter - 0.0f;
    const float lastQuarterDelta = 1.0f - threeQuarters;
    QVERIFY2(firstQuarterDelta > lastQuarterDelta,
        qPrintable(QStringLiteral("expected first-quarter progress (%1) > last-quarter progress (%2)")
                       .arg(firstQuarterDelta)
                       .arg(lastQuarterDelta)));
}

void MixEngineTest::shapeRampProgressSpeedUpStartsSlowEndsFast()
{
    // Mirror image of SlowDown — behind linear early, ahead of nothing (by
    // definition symmetric: SpeedUp(t) == 1 - SlowDown(1-t)), less progress
    // in the first quarter than the last.
    const float quarter = MixEngine::shapeRampProgress(0.25f, RampCurve::SpeedUp);
    const float threeQuarters = MixEngine::shapeRampProgress(0.75f, RampCurve::SpeedUp);
    QVERIFY2(quarter < 0.25f, qPrintable(QStringLiteral("expected SpeedUp behind linear at t=0.25, got %1").arg(quarter)));
    QVERIFY2(threeQuarters < 0.75f,
        qPrintable(QStringLiteral("expected SpeedUp still behind linear at t=0.75, got %1").arg(threeQuarters)));
    const float firstQuarterDelta = quarter - 0.0f;
    const float lastQuarterDelta = 1.0f - threeQuarters;
    QVERIFY2(lastQuarterDelta > firstQuarterDelta,
        qPrintable(QStringLiteral("expected last-quarter progress (%1) > first-quarter progress (%2)")
                       .arg(lastQuarterDelta)
                       .arg(firstQuarterDelta)));
}

void MixEngineTest::shapeRampProgressAllCurvesAgreeAtEndpoints()
{
    // Every curve must start at exactly 0 and land at exactly 1 — a ramp
    // that overshoots or undershoots its actual gain target regardless of
    // curve would be a real, audible bug, not just a shape preference.
    for (RampCurve curve : { RampCurve::Linear, RampCurve::SlowDown, RampCurve::SpeedUp }) {
        QCOMPARE(MixEngine::shapeRampProgress(0.0f, curve), 0.0f);
        QCOMPARE(MixEngine::shapeRampProgress(1.0f, curve), 1.0f);
    }
}

void MixEngineTest::computeEqualPowerGainMatchesCosSineAtEndpointsAndMidpoint()
{
    // Fading 1 -> 0 (the outgoing deck of a crossfade) must trace cos(t*pi/2).
    QCOMPARE(MixEngine::computeEqualPowerGain(0.0f, 1.0f, 0.0f), 1.0f);
    QVERIFY(qFuzzyCompare(MixEngine::computeEqualPowerGain(0.5f, 1.0f, 0.0f), static_cast<float>(std::cos(M_PI / 4.0))));
    QVERIFY(std::fabs(MixEngine::computeEqualPowerGain(1.0f, 1.0f, 0.0f)) < 1e-6f);

    // Fading 0 -> 1 (the incoming deck) must trace sin(t*pi/2).
    QVERIFY(std::fabs(MixEngine::computeEqualPowerGain(0.0f, 0.0f, 1.0f)) < 1e-6f);
    QVERIFY(qFuzzyCompare(MixEngine::computeEqualPowerGain(0.5f, 0.0f, 1.0f), static_cast<float>(std::sin(M_PI / 4.0))));
    QCOMPARE(MixEngine::computeEqualPowerGain(1.0f, 0.0f, 1.0f), 1.0f);
}

void MixEngineTest::computeEqualPowerGainConstantPowerAcrossFade()
{
    // The actual "equal power" property: the OUTGOING and INCOMING deck's
    // gains, squared and summed, must stay ~1 (constant perceived loudness)
    // at every point through the transition — a linear crossfade dips to
    // 0.5 here at the midpoint, which is exactly the ~3dB loudness dip this
    // curve exists to fix.
    for (float t : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f }) {
        const float outgoing = MixEngine::computeEqualPowerGain(t, 1.0f, 0.0f);
        const float incoming = MixEngine::computeEqualPowerGain(t, 0.0f, 1.0f);
        const float power = outgoing * outgoing + incoming * incoming;
        QVERIFY2(std::fabs(power - 1.0f) < 1e-5f,
            qPrintable(QStringLiteral("t=%1: expected constant power ~1.0, got %2").arg(t).arg(power)));
    }
}

void MixEngineTest::eqBandBoostIncreasesEnergy()
{
    // tone440.wav's energy sits closest to band index 1 (250Hz peak) among
    // the 5 fixed bands — not an exact match, but a large enough boost on a
    // Q=0.707 peaking filter still meaningfully affects a tone this close
    // to its center frequency. This proves the EQ chain genuinely processes
    // audio (band-index -> correct filter -> correct frequency), not just
    // that setEqBandGain() doesn't crash.
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine boostedEngine;
    QVERIFY(boostedEngine.start());
    auto boostedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(boostedSource != nullptr);
    GstSourcePipeline* boostedRaw = boostedSource.get();
    m_pipelines.push_back(std::move(boostedSource));
    boostedEngine.attachSource(QStringLiteral("A"),
        [boostedRaw](float* out, size_t frameCount) { return boostedRaw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    boostedEngine.setEqBandGain(QStringLiteral("A"), 1, 18.0); // band 1 = 250Hz peak
    // An +18dB boost can push this tone's peak past the master limiter's
    // default ceiling — this test is about the EQ chain, not the limiter,
    // so push the ceiling out of the way (same reasoning as
    // twoSourcesMixLouderThanEither()'s own limiter workaround).
    boostedEngine.setLimiterCeilingDb(20.0);
    const double boostedEnergy = collectRms(boostedEngine, 500);
    boostedEngine.stop();

    QVERIFY2(boostedEnergy > flatEnergy * 1.2,
        qPrintable(QStringLiteral("expected +18dB band boost to raise energy (flat=%1, boosted=%2)")
                       .arg(flatEnergy)
                       .arg(boostedEnergy)));
}

void MixEngineTest::eqBandGainRoundTrips()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(
        QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);

    for (int band = 0; band < 5; ++band)
        QCOMPARE(engine.eqBandGain(QStringLiteral("A"), band), 0.0); // flat by default

    engine.setEqBandGain(QStringLiteral("A"), 1, 6.0);
    QCOMPARE(engine.eqBandGain(QStringLiteral("A"), 1), 6.0);
    // Other bands unaffected by setting one.
    QCOMPARE(engine.eqBandGain(QStringLiteral("A"), 0), 0.0);

    engine.stop();
}

void MixEngineTest::eqBandGainIsZeroWithoutProcessing()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    // Attached WITHOUT processing (matches cart clips before this fix, and
    // still matches any source deliberately attached that way).
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    QCOMPARE(engine.eqBandGain(QStringLiteral("A"), 0), 0.0);

    engine.stop();
}

void MixEngineTest::eqBandGainIsZeroForUnknownIdOrBand()
{
    MixEngine engine;
    QVERIFY(engine.start());

    QCOMPARE(engine.eqBandGain(QStringLiteral("nonexistent"), 0), 0.0);

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(
        QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    engine.setEqBandGain(QStringLiteral("A"), 2, 9.0);

    QCOMPARE(engine.eqBandGain(QStringLiteral("A"), -1), 0.0);
    QCOMPARE(engine.eqBandGain(QStringLiteral("A"), 5), 0.0);

    engine.stop();
}

void MixEngineTest::stepTowardDbClampsLargeStepToMax()
{
    // A 24dB jump with a 6dB/callback cap must land exactly 6dB closer, not
    // overshoot or jump straight to the target — this is what keeps a large
    // EQ change from clicking (see kMaxEqDbStepPerCallback's doc comment).
    QCOMPARE(MixEngine::stepTowardDb(0.0f, 24.0f, 6.0f), 6.0f);
    QCOMPARE(MixEngine::stepTowardDb(6.0f, 24.0f, 6.0f), 12.0f);
}

void MixEngineTest::stepTowardDbReachesTargetWhenWithinRange()
{
    // A change already smaller than the max step must land exactly on
    // target in one call, not overshoot past it.
    QCOMPARE(MixEngine::stepTowardDb(0.0f, 3.0f, 6.0f), 3.0f);
    QCOMPARE(MixEngine::stepTowardDb(3.0f, 3.0f, 6.0f), 3.0f);
}

void MixEngineTest::stepTowardDbHandlesNegativeDirection()
{
    QCOMPARE(MixEngine::stepTowardDb(0.0f, -24.0f, 6.0f), -6.0f);
    QCOMPARE(MixEngine::stepTowardDb(-18.0f, -24.0f, 6.0f), -24.0f);
}

void MixEngineTest::eqGainEventuallyReachesLargeTarget()
{
    // Regression coverage at the MixEngine level: a large EQ jump (well
    // beyond kMaxEqDbStepPerCallback) must still fully converge given
    // enough callbacks, not get stuck partway — stepTowardDb()'s own tests
    // above cover the per-step math, this proves the wiring around it
    // (pullRead()'s per-callback re-check, not gated behind a seq latch)
    // actually drives convergence to completion.
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    const double flatEnergy = collectRms(engine, 300);

    engine.setEqBandGain(QStringLiteral("A"), 1, 12.0); // band 1 (250Hz) sits close to tone440.wav's energy
    const double boostedEnergy = collectRms(engine, 500); // well past several callbacks' worth of stepping

    engine.stop();

    QVERIFY2(boostedEnergy > flatEnergy * 1.15,
        qPrintable(QStringLiteral("expected a large EQ boost to fully converge, flat=%1 boosted=%2")
                       .arg(flatEnergy)
                       .arg(boostedEnergy)));
}

void MixEngineTest::trimReducesOutputProportionally()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));

    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    const double fullEnergy = collectRms(engine, 400);

    // setTrim() is a SEPARATE multiplier from setGain()'s ramped crossfade
    // gain (see MixEngine.h's doc comment) — this proves it actually
    // multiplies into the final output, not just that it's stored.
    engine.setTrim(QStringLiteral("A"), 0.3);
    const double trimmedEnergy = collectRms(engine, 400);

    engine.stop();

    QVERIFY2(trimmedEnergy < fullEnergy * 0.5,
        qPrintable(QStringLiteral("expected trim=0.3 to meaningfully reduce output (full=%1, trimmed=%2)")
                       .arg(fullEnergy)
                       .arg(trimmedEnergy)));
}

void MixEngineTest::deckPeakLevelReflectsAudibleSignal()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto processedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    auto plainSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    QVERIFY(processedSource != nullptr);
    QVERIFY(plainSource != nullptr);
    GstSourcePipeline* processedRaw = processedSource.get();
    GstSourcePipeline* plainRaw = plainSource.get();
    m_pipelines.push_back(std::move(processedSource));
    m_pipelines.push_back(std::move(plainSource));

    engine.attachSource(QStringLiteral("A"),
        [processedRaw](float* out, size_t frameCount) { return processedRaw->read(out, frameCount); },
        /*initialGain=*/1.0, /*withProcessing=*/true);
    // Attached WITHOUT processing (matches cart-wall clips) — deckPeakLevel()
    // should report false for this one, never having levels to report.
    engine.attachSource(QStringLiteral("B"), [plainRaw](float* out, size_t frameCount) { return plainRaw->read(out, frameCount); });

    QTest::qWait(400); // let real audio actually flow

    float left = 0.0f;
    float right = 0.0f;
    QVERIFY(engine.deckPeakLevel(QStringLiteral("A"), left, right));
    QVERIFY2(left > 0.05f && left <= 1.0f, qPrintable(QStringLiteral("left peak out of expected range: %1").arg(left)));
    QVERIFY2(right > 0.05f && right <= 1.0f, qPrintable(QStringLiteral("right peak out of expected range: %1").arg(right)));

    float unusedL = -1.0f;
    float unusedR = -1.0f;
    QVERIFY(!engine.deckPeakLevel(QStringLiteral("B"), unusedL, unusedR));
    QCOMPARE(unusedL, 0.0f); // outputs reset to 0 even on the false path
    QCOMPARE(unusedR, 0.0f);

    QVERIFY(!engine.deckPeakLevel(QStringLiteral("nonexistent"), unusedL, unusedR));

    engine.stop();
}

void MixEngineTest::masterVolumeScalesOutput()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    const double fullVolumeEnergy = collectRms(engine, 400);

    engine.setMasterVolume(0.25);
    const double quietEnergy = collectRms(engine, 400);

    engine.stop();

    QVERIFY2(quietEnergy < fullVolumeEnergy * 0.5,
        qPrintable(QStringLiteral("expected masterVolume=0.25 to meaningfully reduce output (full=%1, quiet=%2)")
                       .arg(fullVolumeEnergy)
                       .arg(quietEnergy)));
}

void MixEngineTest::masterBassBoostIncreasesEnergy()
{
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    // Needs an actual bass-frequency signal — a 60Hz shelf has negligible
    // effect ~3 octaves up at tone440.wav's 440Hz (confirmed empirically:
    // a first attempt at this test using tone440.wav measured only a 0.3%
    // change against an 18dB boost). tone80.wav sits well within the
    // shelf's boosted region.
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine boostedEngine;
    QVERIFY(boostedEngine.start());
    auto boostedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(boostedSource != nullptr);
    GstSourcePipeline* boostedRaw = boostedSource.get();
    m_pipelines.push_back(std::move(boostedSource));
    boostedEngine.attachSource(
        QStringLiteral("A"), [boostedRaw](float* out, size_t frameCount) { return boostedRaw->read(out, frameCount); });
    boostedEngine.setMasterEqBandGain(0, 18.0); // band 0 = 60Hz low shelf
    const double boostedEnergy = collectRms(boostedEngine, 500);
    boostedEngine.stop();

    QVERIFY2(boostedEnergy > flatEnergy * 1.1,
        qPrintable(QStringLiteral("expected +18dB master bass boost to raise energy (flat=%1, boosted=%2)")
                       .arg(flatEnergy)
                       .arg(boostedEnergy)));
}

void MixEngineTest::masterTrebleIncreasesEnergy()
{
    // Mirrors masterBassBoostIncreasesEnergy exactly, but for the 12kHz
    // high shelf — tone12000.wav sits right at the shelf's own corner
    // frequency, well within its boosted region.
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone12000.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine boostedEngine;
    QVERIFY(boostedEngine.start());
    auto boostedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone12000.wav"));
    QVERIFY(boostedSource != nullptr);
    GstSourcePipeline* boostedRaw = boostedSource.get();
    m_pipelines.push_back(std::move(boostedSource));
    boostedEngine.attachSource(
        QStringLiteral("A"), [boostedRaw](float* out, size_t frameCount) { return boostedRaw->read(out, frameCount); });
    boostedEngine.setMasterEqBandGain(5, 18.0); // band 5 = 12kHz high shelf
    const double boostedEnergy = collectRms(boostedEngine, 500);
    boostedEngine.stop();

    QVERIFY2(boostedEnergy > flatEnergy * 1.1,
        qPrintable(QStringLiteral("expected +18dB master treble boost to raise energy (flat=%1, boosted=%2)")
                       .arg(flatEnergy)
                       .arg(boostedEnergy)));
}

void MixEngineTest::masterBassBoostAndLoudnessCombineAdditively()
{
    // Proves setMasterEqBandGain(0, ...) and the loudness low-shelf combine
    // into ONE filter's summed gain (see dataCallback()'s combinedLowDb) rather than
    // each running through its own separate 60Hz shelf — engaging both
    // should boost noticeably MORE than bass boost alone, not the same
    // amount twice over (which a broken merge, e.g. the second one
    // silently no-op'ing, could still pass a weaker "boosted > flat" check
    // without catching).
    MixEngine bassOnlyEngine;
    QVERIFY(bassOnlyEngine.start());
    auto bassOnlySource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(bassOnlySource != nullptr);
    GstSourcePipeline* bassOnlyRaw = bassOnlySource.get();
    m_pipelines.push_back(std::move(bassOnlySource));
    bassOnlyEngine.attachSource(
        QStringLiteral("A"), [bassOnlyRaw](float* out, size_t frameCount) { return bassOnlyRaw->read(out, frameCount); });
    bassOnlyEngine.setCompressorEnabled(false); // this test is about the bass-boost/loudness shelf merge, not the compressor
    bassOnlyEngine.setMasterEqBandGain(0, 9.0);
    bassOnlyEngine.setMasterVolume(0.5); // held identical across both engines below, isolating the EQ difference
    const double bassOnlyEnergy = collectRms(bassOnlyEngine, 500);
    bassOnlyEngine.stop();

    MixEngine bassPlusLoudnessEngine;
    QVERIFY(bassPlusLoudnessEngine.start());
    auto combinedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(combinedSource != nullptr);
    GstSourcePipeline* combinedRaw = combinedSource.get();
    m_pipelines.push_back(std::move(combinedSource));
    bassPlusLoudnessEngine.attachSource(
        QStringLiteral("A"), [combinedRaw](float* out, size_t frameCount) { return combinedRaw->read(out, frameCount); });
    bassPlusLoudnessEngine.setCompressorEnabled(false); // this test is about the bass-boost/loudness shelf merge, not the compressor
    bassPlusLoudnessEngine.setMasterEqBandGain(0, 9.0);
    bassPlusLoudnessEngine.setMasterVolume(0.5); // boostFactor = 1-0.5 = 0.5 -> +3dB loudness low-shelf on top of the +9dB bass boost
    bassPlusLoudnessEngine.setLoudnessEnabled(true);
    const double combinedEnergy = collectRms(bassPlusLoudnessEngine, 500);
    bassPlusLoudnessEngine.stop();

    QVERIFY2(combinedEnergy > bassOnlyEnergy * 1.05,
        qPrintable(QStringLiteral("expected loudness to add further boost on top of bass boost via the shared "
                                   "filter (bassOnly=%1, bassPlusLoudness=%2)")
                       .arg(bassOnlyEnergy)
                       .arg(combinedEnergy)));
}

void MixEngineTest::masterEqBandGainRoundTrips()
{
    MixEngine engine;
    QVERIFY(engine.start());

    for (int band = 0; band < 6; ++band)
        QCOMPARE(engine.masterEqBandGain(band), 0.0); // flat by default

    engine.setMasterEqBandGain(2, 6.0); // one of the new mid peaking bands
    QCOMPARE(engine.masterEqBandGain(2), 6.0);
    // Other bands unaffected by setting one.
    QCOMPARE(engine.masterEqBandGain(0), 0.0);
    QCOMPARE(engine.masterEqBandGain(5), 0.0);

    engine.stop();
}

void MixEngineTest::masterEqBandGainIsZeroForOutOfRangeBand()
{
    MixEngine engine;
    QVERIFY(engine.start());

    engine.setMasterEqBandGain(3, 9.0);

    QCOMPARE(engine.masterEqBandGain(-1), 0.0);
    QCOMPARE(engine.masterEqBandGain(6), 0.0);

    engine.stop();
}

void MixEngineTest::masterMidBandBoostIncreasesEnergy()
{
    // Band 2 (400Hz peak, see setMasterEqBandGain()'s doc comment) is one
    // of the 4 new mid bands between the master's bass/treble shelves.
    // tone440.wav sits close enough to 400Hz for a Q=0.707 peaking filter
    // to meaningfully affect it — same "close enough, not exact" precedent
    // eqBandBoostIncreasesEnergy's own per-source band test used to rely on
    // against the old 250Hz band.
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine boostedEngine;
    QVERIFY(boostedEngine.start());
    auto boostedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(boostedSource != nullptr);
    GstSourcePipeline* boostedRaw = boostedSource.get();
    m_pipelines.push_back(std::move(boostedSource));
    boostedEngine.attachSource(
        QStringLiteral("A"), [boostedRaw](float* out, size_t frameCount) { return boostedRaw->read(out, frameCount); });
    boostedEngine.setMasterEqBandGain(2, 18.0);
    // Same limiter-out-of-the-way reasoning as eqBandBoostIncreasesEnergy.
    boostedEngine.setLimiterCeilingDb(20.0);
    const double boostedEnergy = collectRms(boostedEngine, 500);
    boostedEngine.stop();

    QVERIFY2(boostedEnergy > flatEnergy * 1.1,
        qPrintable(QStringLiteral("expected +18dB master mid-band boost to raise energy (flat=%1, boosted=%2)")
                       .arg(flatEnergy)
                       .arg(boostedEnergy)));
}

void MixEngineTest::masterHighPassAttenuatesSubsonicContent()
{
    // tone20.wav (20Hz) sits below the 30Hz cutoff, unlike tone80.wav which
    // sits above it (that fixture is the bass-BOOST test's, not this one's).
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone20.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine filteredEngine;
    QVERIFY(filteredEngine.start());
    auto filteredSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone20.wav"));
    QVERIFY(filteredSource != nullptr);
    GstSourcePipeline* filteredRaw = filteredSource.get();
    m_pipelines.push_back(std::move(filteredSource));
    filteredEngine.attachSource(
        QStringLiteral("A"), [filteredRaw](float* out, size_t frameCount) { return filteredRaw->read(out, frameCount); });
    filteredEngine.setMasterHighPassEnabled(true);
    const double filteredEnergy = collectRms(filteredEngine, 500);
    filteredEngine.stop();

    QVERIFY2(filteredEnergy < flatEnergy * 0.5,
        qPrintable(QStringLiteral("expected 30Hz high-pass to meaningfully attenuate a 20Hz tone (flat=%1, filtered=%2)")
                       .arg(flatEnergy)
                       .arg(filteredEnergy)));
}

void MixEngineTest::masterHighPassLeavesAudibleContentUnaffected()
{
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine filteredEngine;
    QVERIFY(filteredEngine.start());
    auto filteredSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(filteredSource != nullptr);
    GstSourcePipeline* filteredRaw = filteredSource.get();
    m_pipelines.push_back(std::move(filteredSource));
    filteredEngine.attachSource(
        QStringLiteral("A"), [filteredRaw](float* out, size_t frameCount) { return filteredRaw->read(out, frameCount); });
    filteredEngine.setMasterHighPassEnabled(true);
    const double filteredEnergy = collectRms(filteredEngine, 500);
    filteredEngine.stop();

    QVERIFY2(filteredEnergy > flatEnergy * 0.9,
        qPrintable(QStringLiteral("expected 30Hz high-pass to leave a 440Hz tone essentially unaffected (flat=%1, filtered=%2)")
                       .arg(flatEnergy)
                       .arg(filteredEnergy)));
}

void MixEngineTest::estimateTruePeakDetectsInterSampleOverAbsentFromEitherSampleAlone()
{
    // A brief plateau at full scale (0, 1, 1, 0) — the realistic case for
    // already-limited/clipped program material — must read as EXCEEDING 1.0
    // between the two interior samples, even though neither sample alone
    // (nor a plain 2-point average, which is provably bounded by its
    // endpoints) reveals it.
    const float result = MixEngine::estimateTruePeak(0.0f, 1.0f, 1.0f, 0.0f);
    QVERIFY2(result > 1.0f, qPrintable(QStringLiteral("expected an inter-sample over > 1.0, got %1").arg(result)));
    QVERIFY(qFuzzyCompare(result, 1.125f));
}

void MixEngineTest::estimateTruePeakEqualsSamplePeakForSlowlyVaryingSignal()
{
    // A gently, linearly ramping signal has no curvature between p1 and
    // p2 — the half-sample estimate must land ON the straight line between
    // them (0.25), never exceeding max(|p1|,|p2|) = 0.3.
    const float result = MixEngine::estimateTruePeak(0.1f, 0.2f, 0.3f, 0.4f);
    QVERIFY(result <= 0.3f + 1e-5f);
}

void MixEngineTest::computeLimiterGainReductionSnapsDownImmediatelyOnAttack()
{
    // A frame twice the ceiling needs exactly half gain to land on it —
    // instant attack means this is exact, not eased toward, regardless of
    // releaseCoeff (attack ignores it entirely).
    const float result = MixEngine::computeLimiterGainReduction(1.0f, 2.0f, 1.0f, 0.01f);
    QCOMPARE(result, 0.5f);
}

void MixEngineTest::computeLimiterGainReductionRecoversGraduallyOnRelease()
{
    // Already reduced (0.5), and this frame is well under the ceiling —
    // should move toward 1.0 but not jump there in one step.
    const float result = MixEngine::computeLimiterGainReduction(0.5f, 0.1f, 1.0f, 0.1f);
    QVERIFY2(result > 0.5f, qPrintable(QStringLiteral("expected release to move gain reduction up from 0.5, got %1").arg(result)));
    QVERIFY2(result < 1.0f, qPrintable(QStringLiteral("expected a single release step to still be short of 1.0, got %1").arg(result)));
}

void MixEngineTest::computeLimiterGainReductionNeverExceedsUnity()
{
    // Already at 1.0 (idle) with a quiet frame - must stay exactly 1.0, not
    // drift above it.
    const float result = MixEngine::computeLimiterGainReduction(1.0f, 0.1f, 1.0f, 0.1f);
    QCOMPARE(result, 1.0f);
}

void MixEngineTest::computeLimiterGainReductionPassesQuietSignalUnchanged()
{
    // Idle state, quiet frame - no reduction should be introduced at all.
    const float result = MixEngine::computeLimiterGainReduction(1.0f, 0.2f, 1.0f, 0.1f);
    QCOMPARE(result, 1.0f);
}

void MixEngineTest::limiterLookaheadStartsWithSilentWarmup()
{
    // The lookahead delay line (setLimiterCeilingDb()'s doc comment: ~5ms
    // = 240 frames @ 48kHz) starts zero-filled. Since the ring buffer
    // preserves write order, the very FIRST samples ever read back must be
    // exactly that silent warm-up, deterministically — not an approximate/
    // timing-sensitive property but a hard guarantee: frame k (0-indexed)
    // can only be non-zero once the delay line has actually been written
    // at that slot, which can't happen before k >= 240 samples have been
    // processed. A zero-lookahead design would NOT have this property —
    // it would start passing/limiting real signal from sample 0.
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    engine.setGain(QStringLiteral("A"), 3.0); // deliberately hot, same as limiterCapsPeakAtCeiling
    engine.setLimiterCeilingDb(-1.0);

    QTest::qWait(50); // just ensure the null-backend callback has actually run at least once

    // Read fewer than the 240-frame lookahead window itself, with margin,
    // so this can't cross into the "just past warm-up" region even
    // accounting for however the device happened to chunk its callbacks.
    float warmupBuf[200 * MixEngine::channels()];
    const size_t warmupGot = engine.readMixedOutput(warmupBuf, 200);
    QVERIFY(warmupGot > 0);
    double sumSquares = 0.0;
    for (size_t i = 0; i < warmupGot * MixEngine::channels(); ++i)
        sumSquares += static_cast<double>(warmupBuf[i]) * warmupBuf[i];
    const double warmupRms = std::sqrt(sumSquares / static_cast<double>(warmupGot * MixEngine::channels()));
    QVERIFY2(warmupRms < 1e-6,
        qPrintable(QStringLiteral("expected exact silence during the lookahead warm-up, got RMS=%1").arg(warmupRms)));

    // Once past the warm-up, the mechanism must still engage and cap the
    // hot signal at the ceiling, same property limiterCapsPeakAtCeiling
    // proves for the steady-state case.
    const double ceilingLinear = std::pow(10.0, -1.0 / 20.0);
    QTest::qWait(300);
    float maxPeak = 0.0f;
    for (int i = 0; i < 10; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        engine.masterPeakLevel(left, right);
        maxPeak = std::max({ maxPeak, left, right });
        QTest::qWait(20);
    }
    engine.stop();

    QVERIFY2(maxPeak <= static_cast<float>(ceilingLinear) + 0.02f,
        qPrintable(QStringLiteral("expected the limiter to still cap the peak past warm-up, observed %1").arg(maxPeak)));
}

void MixEngineTest::limiterCapsPeakAtCeiling()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    engine.setGain(QStringLiteral("A"), 3.0); // deliberately hot — well above any sane ceiling
    const double ceilingDb = -1.0;
    engine.setLimiterCeilingDb(ceilingDb);
    const double ceilingLinear = std::pow(10.0, ceilingDb / 20.0);

    QTest::qWait(300); // let the envelope follower settle

    float maxPeak = 0.0f;
    for (int i = 0; i < 10; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        engine.masterPeakLevel(left, right);
        maxPeak = std::max({ maxPeak, left, right });
        QTest::qWait(20);
    }
    engine.stop();

    QVERIFY2(maxPeak <= static_cast<float>(ceilingLinear) + 0.02f,
        qPrintable(QStringLiteral("expected limiter to cap peak at ~%1, observed %2").arg(ceilingLinear).arg(maxPeak)));
}

void MixEngineTest::limiterLeavesQuietSignalUnreduced()
{
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    engine.setLimiterCeilingDb(-0.3); // default ceiling, unmodified signal
    // The leveler (on by default) can itself boost this tone toward its own
    // target, which could then tip the limiter — this test is about the
    // limiter's OWN baseline behavior on an unmodified signal, so isolate it
    // the same way twoSourcesMixLouderThanEither()/eqBandBoostIncreasesEnergy()
    // already isolate themselves from the limiter.
    engine.setLevelerEnabled(false);

    QTest::qWait(400);

    QVERIFY2(engine.limiterGainReductionDb() > -0.1,
        qPrintable(QStringLiteral("expected an unboosted single tone to not engage the limiter, got %1 dB")
                       .arg(engine.limiterGainReductionDb())));

    engine.stop();
}

void MixEngineTest::computeLevelerRmsEstimateConvergesTowardMeasuredValue()
{
    float estimate = 0.0f;
    const float measured = 0.5f;
    for (int i = 0; i < 50; ++i)
        estimate = MixEngine::computeLevelerRmsEstimate(estimate, measured, 0.1f);
    QVERIFY2(std::fabs(estimate - measured) < 0.01f,
        qPrintable(QStringLiteral("expected repeated EMA updates to converge near %1, got %2").arg(measured).arg(estimate)));
}

void MixEngineTest::computeLevelerRmsEstimateNeverOvershoots()
{
    // A single step must land strictly between the previous and measured
    // values, never past the measured value.
    const float result = MixEngine::computeLevelerRmsEstimate(0.0f, 1.0f, 0.3f);
    QVERIFY(result > 0.0f);
    QVERIFY(result < 1.0f);
}

void MixEngineTest::computeLevelerGainDbBoostsWhenBelowTarget()
{
    // Target is 1.0 (0dB); estimate well below it should call for a positive correction.
    const float result = MixEngine::computeLevelerGainDb(0.1f, 1.0f, 6.0f, 1e-6f);
    QVERIFY2(result > 0.0f, qPrintable(QStringLiteral("expected a positive correction, got %1").arg(result)));
}

void MixEngineTest::computeLevelerGainDbAttenuatesWhenAboveTarget()
{
    const float result = MixEngine::computeLevelerGainDb(10.0f, 1.0f, 6.0f, 1e-6f);
    QVERIFY2(result < 0.0f, qPrintable(QStringLiteral("expected a negative correction, got %1").arg(result)));
}

void MixEngineTest::computeLevelerGainDbIsZeroAtTarget()
{
    const float result = MixEngine::computeLevelerGainDb(1.0f, 1.0f, 6.0f, 1e-6f);
    QVERIFY2(std::fabs(result) < 0.001f, qPrintable(QStringLiteral("expected ~0 correction at target, got %1").arg(result)));
}

void MixEngineTest::computeLevelerGainDbClampsToRange()
{
    // Wildly below target - the raw correction would be enormous; must clamp to +rangeDb exactly.
    const float boosted = MixEngine::computeLevelerGainDb(1e-9f, 1.0f, 6.0f, 1e-12f);
    QCOMPARE(boosted, 6.0f);
    // Wildly above target - must clamp to -rangeDb exactly.
    const float attenuated = MixEngine::computeLevelerGainDb(1e9f, 1.0f, 6.0f, 1e-6f);
    QCOMPARE(attenuated, -6.0f);
}

void MixEngineTest::computeLevelerGainDbIsZeroBelowNoiseFloor()
{
    // Below the noise floor, even though the estimate is far below target
    // (which would otherwise call for a large boost), the result must be
    // exactly 0 - never amplify silence toward the target.
    const float result = MixEngine::computeLevelerGainDb(1e-8f, 1.0f, 6.0f, 1e-6f);
    QCOMPARE(result, 0.0f);
}

void MixEngineTest::computeLoudnessBoostFactorZeroAtUnity()
{
    QCOMPARE(MixEngine::computeLoudnessBoostFactor(0.0f, -12.0f), 0.0f);
    // Above unity (a positive dB boost, not attenuation) must also stay at
    // 0 -- loudness compensation only ever kicks in below unity.
    QCOMPARE(MixEngine::computeLoudnessBoostFactor(6.0f, -12.0f), 0.0f);
}

void MixEngineTest::computeLoudnessBoostFactorFullAtOrBelowFloor()
{
    QCOMPARE(MixEngine::computeLoudnessBoostFactor(-12.0f, -12.0f), 1.0f);
    // Further below the floor must clamp at 1, not keep growing.
    QCOMPARE(MixEngine::computeLoudnessBoostFactor(-30.0f, -12.0f), 1.0f);
}

void MixEngineTest::computeLoudnessBoostFactorMonotonicBetween()
{
    const float quarter = MixEngine::computeLoudnessBoostFactor(-3.0f, -12.0f);
    const float half = MixEngine::computeLoudnessBoostFactor(-6.0f, -12.0f);
    const float threeQuarters = MixEngine::computeLoudnessBoostFactor(-9.0f, -12.0f);
    QVERIFY(quarter > 0.0f && quarter < half);
    QVERIFY(half > quarter && half < threeQuarters);
    QVERIFY(threeQuarters > half && threeQuarters < 1.0f);
    QCOMPARE(half, 0.5f);
}

void MixEngineTest::levelerBoostsAQuietSignalTowardTarget()
{
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    flatEngine.setLevelerEnabled(false);
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/quiet440.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine leveledEngine;
    QVERIFY(leveledEngine.start());
    leveledEngine.setLevelerTimeConstantSeconds(0.2); // fast, deterministic convergence for this test
    auto leveledSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/quiet440.wav"));
    QVERIFY(leveledSource != nullptr);
    GstSourcePipeline* leveledRaw = leveledSource.get();
    m_pipelines.push_back(std::move(leveledSource));
    leveledEngine.attachSource(
        QStringLiteral("A"), [leveledRaw](float* out, size_t frameCount) { return leveledRaw->read(out, frameCount); });
    collectRms(leveledEngine, 1000); // let it converge (~5 time constants); also drains the buffer
    const double leveledEnergy = collectRms(leveledEngine, 500); // measure the converged state
    leveledEngine.stop();

    QVERIFY2(leveledEnergy > flatEnergy * 1.3,
        qPrintable(QStringLiteral("expected the leveler to boost a quiet signal toward its target (flat=%1, leveled=%2)")
                       .arg(flatEnergy)
                       .arg(leveledEnergy)));
}

void MixEngineTest::levelerAttenuatesALoudSignalTowardTarget()
{
    MixEngine flatEngine;
    QVERIFY(flatEngine.start());
    flatEngine.setLevelerEnabled(false);
    auto flatSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(flatSource != nullptr);
    GstSourcePipeline* flatRaw = flatSource.get();
    m_pipelines.push_back(std::move(flatSource));
    flatEngine.attachSource(QStringLiteral("A"), [flatRaw](float* out, size_t frameCount) { return flatRaw->read(out, frameCount); });
    const double flatEnergy = collectRms(flatEngine, 500);
    flatEngine.stop();

    MixEngine leveledEngine;
    QVERIFY(leveledEngine.start());
    leveledEngine.setLevelerTimeConstantSeconds(0.2);
    auto leveledSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(leveledSource != nullptr);
    GstSourcePipeline* leveledRaw = leveledSource.get();
    m_pipelines.push_back(std::move(leveledSource));
    leveledEngine.attachSource(
        QStringLiteral("A"), [leveledRaw](float* out, size_t frameCount) { return leveledRaw->read(out, frameCount); });
    collectRms(leveledEngine, 1000);
    const double leveledEnergy = collectRms(leveledEngine, 500);
    leveledEngine.stop();

    QVERIFY2(leveledEnergy < flatEnergy * 0.9,
        qPrintable(QStringLiteral("expected the leveler to pull a loud signal down toward its target (flat=%1, leveled=%2)")
                       .arg(flatEnergy)
                       .arg(leveledEnergy)));
}

void MixEngineTest::levelerRespondsMoreToHighFrequencyEnergyThanLowFrequencyEnergy()
{
    // Proves the leveler actually measures through the K-weighting filters
    // (see kKWeightStage1B0's doc comment) rather than flat RMS: BS.1770
    // K-weighting scores high-frequency content as perceptually LOUDER than
    // flat RMS would, so a 12kHz tone needs LESS boost to reach the same
    // target than an 80Hz tone at the same nominal input level — if the
    // K-filter were missing or inert, both would converge to the same gain.
    // tone12000.wav and tone80.wav are NOT amplitude-matched as fixtures
    // (measured RMS ~0.088 vs ~0.212 respectively, i.e. tone80.wav is
    // ~2.4x louder on its own) — compensate with initialGain so both
    // present the same flat-RMS level into the master bus, isolating the
    // K-weighting effect from that unrelated fixture-level difference.
    constexpr double kFixtureLevelCompensationGain = 0.21213203436331854 / 0.08837066692559169;

    MixEngine highFreqEngine;
    QVERIFY(highFreqEngine.start());
    highFreqEngine.setLevelerTimeConstantSeconds(0.2); // fast, deterministic convergence
    auto highFreqSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone12000.wav"));
    QVERIFY(highFreqSource != nullptr);
    GstSourcePipeline* highFreqRaw = highFreqSource.get();
    m_pipelines.push_back(std::move(highFreqSource));
    highFreqEngine.attachSource(
        QStringLiteral("A"), [highFreqRaw](float* out, size_t frameCount) { return highFreqRaw->read(out, frameCount); },
        kFixtureLevelCompensationGain);
    collectRms(highFreqEngine, 1000); // let it converge (~5 time constants)
    const double highFreqGainDb = highFreqEngine.levelerGainDb();
    highFreqEngine.stop();

    MixEngine lowFreqEngine;
    QVERIFY(lowFreqEngine.start());
    lowFreqEngine.setLevelerTimeConstantSeconds(0.2);
    auto lowFreqSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(lowFreqSource != nullptr);
    GstSourcePipeline* lowFreqRaw = lowFreqSource.get();
    m_pipelines.push_back(std::move(lowFreqSource));
    lowFreqEngine.attachSource(
        QStringLiteral("A"), [lowFreqRaw](float* out, size_t frameCount) { return lowFreqRaw->read(out, frameCount); });
    collectRms(lowFreqEngine, 1000);
    const double lowFreqGainDb = lowFreqEngine.levelerGainDb();
    lowFreqEngine.stop();

    QVERIFY2(highFreqGainDb < lowFreqGainDb,
        qPrintable(QStringLiteral("expected K-weighting to need less boost for high-frequency content at the same "
                                   "input level (12kHz gain=%1dB, 80Hz gain=%2dB)")
                       .arg(highFreqGainDb)
                       .arg(lowFreqGainDb)));
}

void MixEngineTest::levelerNeverBoostsNearSilence()
{
    // No source attached at all - the master bus processes pure silence.
    // The leveler must not try to amplify it toward the target.
    MixEngine engine;
    QVERIFY(engine.start());
    engine.setLevelerTimeConstantSeconds(0.2);

    QTest::qWait(1000);

    QVERIFY2(engine.levelerGainDb() == 0.0,
        qPrintable(QStringLiteral("expected no leveler correction on silence, got %1 dB").arg(engine.levelerGainDb())));

    engine.stop();
}

void MixEngineTest::levelerDisabledLeavesOutputUnchanged()
{
    MixEngine engine;
    QVERIFY(engine.start());
    engine.setLevelerEnabled(false);
    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/quiet440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    collectRms(engine, 1000);
    QCOMPARE(engine.levelerGainDb(), 0.0);

    engine.stop();
}

void MixEngineTest::enumeratePlaybackDevicesReturnsAtLeastOneDeviceUnderNullBackend()
{
    // Static -- independent of whether any MixEngine instance is running
    // (initTestCase() already forces RS_AUDIO_SINK=null for this whole
    // suite; the null backend reports one virtual device for enumeration).
    const auto devices = MixEngine::enumeratePlaybackDevices();
    QVERIFY(!devices.empty());
    QVERIFY(!devices.front().id.isEmpty());
}

void MixEngineTest::switchPlaybackDeviceRoundTripsWithoutDisruptingAnAttachedSource()
{
    // Phase 2 regression test: m_graph/m_sources must survive a live
    // device swap untouched -- an attached source should keep producing
    // audible output through the NEW device object, not just the one that
    // was open at attachSource() time.
    MixEngine engine;
    QVERIFY(engine.start());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    QVERIFY2(collectRms(engine, 300) > 0.01, "expected audible output before the device switch");

    QVERIFY(engine.switchPlaybackDevice(QByteArray())); // empty = system default -- under the null backend, its own default

    QVERIFY2(collectRms(engine, 300) > 0.01, "expected audible output to continue after the device switch");

    engine.stop();
}

void MixEngineTest::monitorDeviceStartsAndStopsCleanlyAlongsideTheAirDevice()
{
    // Shallow (doesn't assert on mirrored sample content -- the null
    // backend has nowhere observable to assert that against) but still a
    // real regression guard: monitorDataCallback() runs against a second,
    // genuinely separate ma_device the whole time a source is attached and
    // producing output, so a bug there (e.g. an out-of-bounds write) would
    // crash or hang this test.
    MixEngine engine;
    QVERIFY(engine.start());
    QVERIFY(!engine.isMonitorDeviceRunning());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    QVERIFY(engine.startMonitorDevice(QByteArray()));
    QVERIFY(engine.isMonitorDeviceRunning());

    QTest::qWait(300); // let both devices' callbacks actually run for a while

    engine.stopMonitorDevice();
    QVERIFY(!engine.isMonitorDeviceRunning());

    // The air device/graph must be completely unaffected by the monitor
    // device's whole lifecycle.
    QVERIFY2(collectRms(engine, 300) > 0.01, "expected the air device to still produce audible output");

    engine.stop();
}

void MixEngineTest::lufsFromMeanSquareMatchesClosedFormAndStaysFiniteAtSilence()
{
    QVERIFY2(qAbs(MixEngine::lufsFromMeanSquare(1.0) - (-0.691f)) < 0.001f,
        qPrintable(QString::number(MixEngine::lufsFromMeanSquare(1.0))));

    const float atSilence = MixEngine::lufsFromMeanSquare(0.0);
    QVERIFY2(std::isfinite(atSilence), "silence must not produce -infinity/NaN");
    QVERIFY(atSilence < -100.0f); // clamped to a tiny epsilon floor -- reads as extremely, but finitely, quiet

    // Monotonic: louder input -> higher (less negative) LUFS.
    QVERIFY(MixEngine::lufsFromMeanSquare(0.1) > MixEngine::lufsFromMeanSquare(0.01));
}

void MixEngineTest::computeIntegratedLoudnessLufsReturnsZeroWithFewerThanFourSegments()
{
    QCOMPARE(MixEngine::computeIntegratedLoudnessLufs({}), 0.0);
    QCOMPARE(MixEngine::computeIntegratedLoudnessLufs({ 0.01f, 0.01f, 0.01f }), 0.0);
}

void MixEngineTest::computeIntegratedLoudnessLufsMatchesClosedFormForAConstantLevelSignal()
{
    // 10 identical segments -> every reconstructed 400ms block has exactly
    // the same value, so both gates are no-ops and the result must equal
    // the closed-form LUFS of that one value, within float precision.
    constexpr float kSegmentValue = 0.01f; // already the summed per-channel mean-square for one 100ms segment
    const std::vector<float> segments(10, kSegmentValue);

    const double result = MixEngine::computeIntegratedLoudnessLufs(segments);
    const double expected = static_cast<double>(MixEngine::lufsFromMeanSquare(kSegmentValue));
    QVERIFY2(qAbs(result - expected) < 0.01, qPrintable(QStringLiteral("expected %1, got %2").arg(expected).arg(result)));
}

void MixEngineTest::computeIntegratedLoudnessLufsSummedNotAveragedAcrossChannels()
{
    // Identical content in both channels must read ~3.01dB LOUDER than the
    // same content in only one channel -- BS.1770 sums per-channel mean
    // squares for stereo (weight 1.0 each). An "averaged" implementation
    // (the exact bug an earlier design review caught before any of this
    // shipped) would read IDENTICALLY for both cases instead, since
    // averaging two equal channels reproduces the single-channel value.
    MixEngine dualEngine;
    QVERIFY(dualEngine.start());
    dualEngine.setLevelerEnabled(false); // isolate the measurement from the leveler's own adaptive gain
    dualEngine.attachSource(QStringLiteral("A"), makeSineSource(/*stereoContent=*/true));
    QTest::qWait(500);
    const double dualLufs = dualEngine.momentaryLoudnessLufs();
    dualEngine.stop();

    MixEngine monoEngine;
    QVERIFY(monoEngine.start());
    monoEngine.setLevelerEnabled(false);
    monoEngine.attachSource(QStringLiteral("A"), makeSineSource(/*stereoContent=*/false));
    QTest::qWait(500);
    const double monoLufs = monoEngine.momentaryLoudnessLufs();
    monoEngine.stop();

    const double delta = dualLufs - monoLufs;
    QVERIFY2(delta > 2.0 && delta < 4.5,
        qPrintable(QStringLiteral("expected ~3.01dB difference between dual- and single-channel content, got %1 "
                                   "(dual=%2, mono=%3)")
                       .arg(delta)
                       .arg(dualLufs)
                       .arg(monoLufs)));
}

void MixEngineTest::computeIntegratedLoudnessLufsGatesOutSilentSegments()
{
    // A run of loud segments followed by a much longer run of near-silent
    // segments (well below the -70 LUFS absolute gate) -- the silent
    // segments must not meaningfully pull the integrated figure down. The
    // handful of segments straddling the loud/silent boundary partially
    // dilute a few reconstructed 400ms blocks (real, expected, and small —
    // well under 1dB here), which is why this compares against a loose
    // tolerance rather than an exact value; a genuinely broken gate would
    // miss by several dB (unweighted straight averaging over mostly-silent
    // content craters the result), which this tolerance still clearly
    // catches.
    constexpr float kLoudSegmentValue = 0.01f;
    constexpr float kSilentSegmentValue = 1e-10f; // well below -70 LUFS
    std::vector<float> segments(20, kLoudSegmentValue);
    segments.resize(70, kSilentSegmentValue); // append 50 near-silent segments

    const double result = MixEngine::computeIntegratedLoudnessLufs(segments);
    const double expected = static_cast<double>(MixEngine::lufsFromMeanSquare(kLoudSegmentValue));
    QVERIFY2(qAbs(result - expected) < 2.0,
        qPrintable(QStringLiteral("expected silent segments to be gated out: expected ~%1, got %2").arg(expected).arg(result)));
}

void MixEngineTest::momentaryLoudnessLufsRespondsToSignalLevel()
{
    MixEngine quietEngine;
    QVERIFY(quietEngine.start());
    quietEngine.setLevelerEnabled(false);
    auto quietSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(quietSource != nullptr);
    GstSourcePipeline* quietRaw = quietSource.get();
    m_pipelines.push_back(std::move(quietSource));
    quietEngine.attachSource(
        QStringLiteral("A"), [quietRaw](float* out, size_t frameCount) { return quietRaw->read(out, frameCount); });
    quietEngine.setGain(QStringLiteral("A"), 0.1); // -20dB
    QTest::qWait(500);
    const double quietLufs = quietEngine.momentaryLoudnessLufs();
    quietEngine.stop();

    MixEngine loudEngine;
    QVERIFY(loudEngine.start());
    loudEngine.setLevelerEnabled(false);
    auto loudSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(loudSource != nullptr);
    GstSourcePipeline* loudRaw = loudSource.get();
    m_pipelines.push_back(std::move(loudSource));
    loudEngine.attachSource(
        QStringLiteral("A"), [loudRaw](float* out, size_t frameCount) { return loudRaw->read(out, frameCount); });
    loudEngine.setGain(QStringLiteral("A"), 1.0);
    QTest::qWait(500);
    const double loudLufs = loudEngine.momentaryLoudnessLufs();
    loudEngine.stop();

    QVERIFY2(loudLufs > quietLufs + 5.0,
        qPrintable(QStringLiteral("expected the louder signal to read meaningfully louder: quiet=%1 loud=%2")
                       .arg(quietLufs)
                       .arg(loudLufs)));
}

void MixEngineTest::resetIntegratedLoudnessMeasurementRestartsTheHistory()
{
    MixEngine engine;
    QVERIFY(engine.start());
    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    QTest::qWait(600); // several 100ms segments' worth
    QVERIFY2(engine.integratedLoudnessLufs() != 0.0,
        "expected a real integrated-loudness measurement after 600ms of audible signal");

    engine.resetIntegratedLoudnessMeasurement();
    QTest::qWait(50); // give the audio thread at least one more callback to consume the reset request

    QCOMPARE(engine.integratedLoudnessLufs(), 0.0);

    engine.stop();
}

void MixEngineTest::outputTruePeakDbReflectsThePostLimiterCeiling()
{
    MixEngine engine;
    QVERIFY(engine.start());
    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    engine.setCompressorEnabled(false); // this test is about the limiter/true-peak relationship, not the compressor
    engine.setGain(QStringLiteral("A"), 3.0); // deliberately hot -- well above any sane ceiling
    const double ceilingDb = -3.0;
    engine.setLimiterCeilingDb(ceilingDb);

    QTest::qWait(500); // let the envelope follower settle

    const double truePeakDb = engine.outputTruePeakDb();
    engine.stop();

    // A genuine true-peak estimate can slightly exceed the sample-peak
    // ceiling (that's the whole point -- see estimateTruePeak()'s doc
    // comment), but must stay in the same ballpark, not read as silence
    // (the -100dBFS sentinel) or wildly above the ceiling.
    QVERIFY2(truePeakDb > ceilingDb - 1.0 && truePeakDb < ceilingDb + 3.0,
        qPrintable(QStringLiteral("expected true peak near the %1dB ceiling, got %2").arg(ceilingDb).arg(truePeakDb)));
}

void MixEngineTest::computeBandCompressorGainReductionPassesQuietSignalAtUnity()
{
    // Well below the knee -- must not compress at all, regardless of how
    // far below.
    const float gain = MixEngine::computeBandCompressorGainReduction(
        /*previousGainReduction=*/1.0f, /*levelEstimateMeanSquare=*/1e-6f, /*thresholdDb=*/-20.0f, /*ratio=*/4.0f,
        /*kneeWidthDb=*/6.0f, /*attackCoeff=*/1.0f, /*releaseCoeff=*/0.1f);
    QVERIFY2(qAbs(gain - 1.0f) < 0.001f, qPrintable(QString::number(gain)));
}

void MixEngineTest::computeBandCompressorGainReductionMatchesClosedFormAboveTheKnee()
{
    // Well above the knee: targetReductionDb = (levelDb - thresholdDb) *
    // (1/ratio - 1) -- the plain fixed-ratio compressor formula, verified
    // directly against a hand-computed expected value.
    constexpr float kThresholdDb = -20.0f;
    constexpr float kRatio = 4.0f;
    constexpr float kLevelDb = 0.0f; // well above threshold + knee/2
    const float levelMeanSquare = std::pow(10.0f, kLevelDb / 10.0f); // 10*log10(meanSquare) == kLevelDb

    const float gain = MixEngine::computeBandCompressorGainReduction(/*previousGainReduction=*/1.0f, levelMeanSquare,
        kThresholdDb, kRatio, /*kneeWidthDb=*/1.0f, /*attackCoeff=*/1.0f, /*releaseCoeff=*/0.1f);

    const float expectedReductionDb = (kLevelDb - kThresholdDb) * (1.0f / kRatio - 1.0f);
    const float expectedGain = std::pow(10.0f, expectedReductionDb / 20.0f);
    QVERIFY2(qAbs(gain - expectedGain) < 0.001f,
        qPrintable(QStringLiteral("expected %1, got %2").arg(expectedGain).arg(gain)));
}

void MixEngineTest::computeBandCompressorGainReductionCollapsesToLimiterBehaviorAtHighRatio()
{
    // A very high ratio with a near-zero knee width is, for anything above
    // threshold, indistinguishable from a hard ceiling at thresholdDb --
    // exactly what computeLimiterGainReduction() computes for the same
    // signal (both take a dB-equivalent threshold; 10*log10(amplitude^2)
    // == 20*log10(amplitude), so the two functions' domains line up for a
    // pure-tone comparison like this one).
    constexpr float kThresholdDb = -6.0f;
    constexpr float kAmplitude = 1.0f; // 0dB, i.e. well above threshold
    const float levelMeanSquare = kAmplitude * kAmplitude;

    const float compressorGain = MixEngine::computeBandCompressorGainReduction(/*previousGainReduction=*/1.0f,
        levelMeanSquare, kThresholdDb, /*ratio=*/1.0e6f, /*kneeWidthDb=*/1.0e-6f, /*attackCoeff=*/1.0f,
        /*releaseCoeff=*/0.1f);

    const float ceilingLinear = std::pow(10.0f, kThresholdDb / 20.0f);
    const float limiterGain
        = MixEngine::computeLimiterGainReduction(/*previousGainReduction=*/1.0f, kAmplitude, ceilingLinear, 0.1f);

    QVERIFY2(qAbs(compressorGain - limiterGain) < 0.001f,
        qPrintable(QStringLiteral("expected the two to agree: compressor=%1, limiter=%2").arg(compressorGain).arg(limiterGain)));
}

void MixEngineTest::computeBandCompressorGainReductionSilenceConvergesToUnity()
{
    // A compressor must pass silence through at unity, not hold whatever
    // reduction was last computed -- unlike the leveler, which deliberately
    // DOES hold/gate near silence (see computeLevelerGainDb()'s own doc
    // comment).
    float gain = 0.3f; // simulates "was heavily reducing a moment ago"
    for (int i = 0; i < 200; ++i)
        gain = MixEngine::computeBandCompressorGainReduction(
            gain, /*levelEstimateMeanSquare=*/0.0f, -20.0f, 4.0f, 6.0f, 1.0f, /*releaseCoeff=*/0.1f);
    QVERIFY2(gain > 0.99f, qPrintable(QStringLiteral("expected convergence to unity, got %1").arg(gain)));
}

void MixEngineTest::multibandCompressorReducesOnlyTheDrivenBand()
{
    MixEngine engine;
    QVERIFY(engine.start());
    // tone80.wav sits well within the Low band (<200Hz) and has
    // negligible energy in Mid/High (same fixture already used to isolate
    // the bass-boost shelf in masterBassBoostIncreasesEnergy()).
    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone80.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });
    engine.setGain(QStringLiteral("A"), 2.0); // comfortably above the Low band's default -20dB threshold
    engine.setLevelerEnabled(false); // isolate the compressor from the leveler's own adaptive gain

    QTest::qWait(500); // let the per-band envelope followers settle

    const double lowGrDb = engine.bandGainReductionDb(0);
    const double midGrDb = engine.bandGainReductionDb(1);
    const double highGrDb = engine.bandGainReductionDb(2);
    engine.stop();

    QVERIFY2(lowGrDb < -0.5, qPrintable(QStringLiteral("expected the driven Low band to show real gain reduction, got %1 dB").arg(lowGrDb)));
    QVERIFY2(midGrDb > -0.5, qPrintable(QStringLiteral("expected the undriven Mid band to stay near unity, got %1 dB").arg(midGrDb)));
    QVERIFY2(highGrDb > -0.5, qPrintable(QStringLiteral("expected the undriven High band to stay near unity, got %1 dB").arg(highGrDb)));
}

void MixEngineTest::multibandCompressorIsTransparentWhenThresholdsAreHigh()
{
    // With every band's threshold set far above the signal, the compressor
    // never actually engages (gain stays at ~unity on all three bands) --
    // the 3-band split-then-recombine itself must reconstruct the
    // original signal level (the defining property of an LR4 crossover),
    // not introduce a level change of its own via a broken recombination
    // (e.g. a sign error or band overlap/gap would show up here as a
    // measurable RMS difference from the compressor fully disabled).
    MixEngine bypassedEngine;
    QVERIFY(bypassedEngine.start());
    bypassedEngine.setCompressorEnabled(false);
    bypassedEngine.setLevelerEnabled(false);
    auto bypassedSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(bypassedSource != nullptr);
    GstSourcePipeline* bypassedRaw = bypassedSource.get();
    m_pipelines.push_back(std::move(bypassedSource));
    bypassedEngine.attachSource(
        QStringLiteral("A"), [bypassedRaw](float* out, size_t frameCount) { return bypassedRaw->read(out, frameCount); });
    const double bypassedRms = collectRms(bypassedEngine, 500);
    bypassedEngine.stop();

    MixEngine transparentEngine;
    QVERIFY(transparentEngine.start());
    transparentEngine.setLevelerEnabled(false);
    for (int band = 0; band < 3; ++band) {
        transparentEngine.setBandThresholdDb(band, 20.0); // never triggers for any realistic signal
    }
    auto transparentSource = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(transparentSource != nullptr);
    GstSourcePipeline* transparentRaw = transparentSource.get();
    m_pipelines.push_back(std::move(transparentSource));
    transparentEngine.attachSource(QStringLiteral("A"),
        [transparentRaw](float* out, size_t frameCount) { return transparentRaw->read(out, frameCount); });
    const double transparentRms = collectRms(transparentEngine, 500);
    transparentEngine.stop();

    QVERIFY2(bypassedRms > 0.01, "expected an audible baseline signal");
    const double relativeDelta = qAbs(transparentRms - bypassedRms) / bypassedRms;
    QVERIFY2(relativeDelta < 0.05,
        qPrintable(QStringLiteral("expected the crossover split+recombine to be level-transparent: bypassed=%1, "
                                   "transparent=%2, relative delta=%3")
                       .arg(bypassedRms)
                       .arg(transparentRms)
                       .arg(relativeDelta)));
}

void MixEngineTest::micInputStartsAndStopsCleanlyAlongsideTheAirDevice()
{
    // Shallow (the null backend has nowhere observable to assert captured
    // sample content against) but still a real regression guard:
    // micCaptureDataCallback() runs against a second, genuinely separate
    // ma_device the whole time capture is active, and startMicInput()
    // internally attaches "mic" as an ordinary pull-based source — a bug
    // in either path (an out-of-bounds write, a bad attach/detach) would
    // crash or hang this test.
    MixEngine engine;
    QVERIFY(engine.start());
    QVERIFY(!engine.isMicInputRunning());

    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    QVERIFY(engine.startMicInput(QByteArray()));
    QVERIFY(engine.isMicInputRunning());

    float micLeft = 0.0f;
    float micRight = 0.0f;
    QVERIFY2(engine.deckPeakLevel(QStringLiteral("mic"), micLeft, micRight),
        "expected the mic to be attached as an ordinary withProcessing source");

    QTest::qWait(300); // let both devices' callbacks actually run for a while

    engine.stopMicInput();
    QVERIFY(!engine.isMicInputRunning());
    QVERIFY2(!engine.deckPeakLevel(QStringLiteral("mic"), micLeft, micRight),
        "expected the mic source to be detached after stopMicInput()");

    // The air device/graph must be completely unaffected by the mic's
    // whole lifecycle.
    QVERIFY2(collectRms(engine, 300) > 0.01, "expected the air device to still produce audible output");

    engine.stop();
}

void MixEngineTest::setDuckGainIsIndependentFromTrim()
{
    MixEngine engine;
    QVERIFY(engine.start());
    auto source = makePlayingSource(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    QVERIFY(source != nullptr);
    GstSourcePipeline* raw = source.get();
    m_pipelines.push_back(std::move(source));
    engine.attachSource(QStringLiteral("A"), [raw](float* out, size_t frameCount) { return raw->read(out, frameCount); });

    engine.setTrim(QStringLiteral("A"), 0.5);
    const double trimOnlyRms = collectRms(engine, 300);

    engine.setDuckGain(QStringLiteral("A"), 0.5);
    const double trimAndDuckRms = collectRms(engine, 300);

    engine.stop();

    // Setting duck gain must further reduce the already-trimmed signal
    // (out = ... * trim * duckGain), not overwrite or ignore whatever trim
    // already set — confirms the deck volume slider (trim) and
    // DuckingController (duck gain) write to genuinely independent gain
    // stages (see setDuckGain()'s own doc comment for the real bug this
    // guards against: an earlier design would have had both write the
    // same value, each clobbering the other).
    QVERIFY2(trimAndDuckRms < trimOnlyRms * 0.75,
        qPrintable(QStringLiteral("expected duck gain to further reduce the trimmed signal: trimOnly=%1, trimAndDuck=%2")
                       .arg(trimOnlyRms)
                       .arg(trimAndDuckRms)));
}

QTEST_MAIN(MixEngineTest)
#include "MixEngineTest.moc"
