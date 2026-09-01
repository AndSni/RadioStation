#include "audio/AudioEngine.h"

#include "core/Logging.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTest>

using namespace radio::audio;

class AudioEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void playsRealFileAndReportsPosition();
    void missingFileReportsError();
    void longTrackPlaysPastRingBufferCapacityWithoutTruncation();
    void deadAirIsDetectedThenClearedOnceAudioResumes();
};

void AudioEngineTest::initTestCase()
{
    gst_init(nullptr, nullptr);
}

void AudioEngineTest::playsRealFileAndReportsPosition()
{
    AudioEngine engine;
    engine.start();

    const QString fixture = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    QVERIFY(QFile::exists(fixture));

    engine.loadTrack(QStringLiteral("A"), fixture);
    engine.play(QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.duration(QStringLiteral("A")) > 0, 3000);

    QTest::qWait(300);
    QVERIFY(engine.position(QStringLiteral("A")) >= 0);
    QVERIFY(engine.duration(QStringLiteral("A")) > 0);

    engine.stopDeck(QStringLiteral("A"));
    QTest::qWait(100);
    engine.shutdown();
}

void AudioEngineTest::longTrackPlaysPastRingBufferCapacityWithoutTruncation()
{
    // Regression test for a real bug found via manual testing (not caught
    // by any other test, since they all use fixtures short enough to fit
    // within the ring buffer's ~1s capacity): appsink's "new-sample"
    // callback used to run with sync=FALSE, so GStreamer decoded far faster
    // than real-time — an 8s (or longer) file's worth of buffers arrived
    // well before the real-time consumer could drain even the first
    // second, and everything past the ring buffer's capacity was silently
    // dropped by RingBuffer::write()'s overflow handling. The audible
    // symptom: any real track longer than ~1s cut off after ~1s of audio,
    // with EOS firing shortly after (see GstSourcePipeline::build()'s
    // sync=TRUE comment for the fix). This fixture (long_tone.wav, 8s) is
    // deliberately longer than the 1s ring buffer to catch a regression.
    AudioEngine engine;
    engine.start();

    const QString fixture = QStringLiteral(RS_TEST_FIXTURES_DIR "/long_tone.wav");
    QVERIFY(QFile::exists(fixture));

    QSignalSpy eosSpy(&engine, &AudioEngine::deckEos);

    QElapsedTimer wallClock;
    engine.loadTrack(QStringLiteral("A"), fixture);
    engine.play(QStringLiteral("A"));
    wallClock.start();

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    // Under the old bug this would already be truncated/EOS'd well before
    // 3 real seconds elapse. Assert it's genuinely still mid-playback.
    QTest::qWait(3000);
    QVERIFY2(eosSpy.isEmpty(), "track reported EOS well before its real 8s duration — ring buffer truncation regressed");
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);
    QVERIFY2(engine.position(QStringLiteral("A")) > 2000,
        "playback position barely advanced — decode/consumption pacing regressed");

    // Let it play out to genuine completion and confirm the total elapsed
    // wall time roughly matches the file's real ~8s duration, not the ~1-2s
    // the old truncation bug produced.
    QTRY_VERIFY_WITH_TIMEOUT(!eosSpy.isEmpty(), 10000);
    QVERIFY2(wallClock.elapsed() > 7000,
        qPrintable(QStringLiteral("EOS arrived after only %1ms of wall time — expected close to the file's real 8000ms duration")
                       .arg(wallClock.elapsed())));

    engine.shutdown();
}

void AudioEngineTest::missingFileReportsError()
{
    AudioEngine engine;
    engine.start();

    QSignalSpy errorSpy(&engine, &AudioEngine::pipelineError);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral("/nonexistent/path/does-not-exist.mp3"));
    engine.play(QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 3000);

    engine.shutdown();
}

void AudioEngineTest::deadAirIsDetectedThenClearedOnceAudioResumes()
{
    // Reuses masterPeakLevel() (no new DSP) -- an engine with nothing
    // playing is exactly the silent condition dead-air detection exists to
    // catch. setDeadAirThresholdSeconds() (short here, same "expose the
    // time constant so tests can force it fast" precedent as
    // CrossfadeController::setCrossfadeLeadMs()) keeps this test fast
    // rather than waiting out the real 8s default.
    AudioEngine engine;
    engine.start();
    engine.setDeadAirThresholdSeconds(0.3);

    QSignalSpy detectedSpy(&engine, &AudioEngine::deadAirDetected);
    QSignalSpy clearedSpy(&engine, &AudioEngine::deadAirCleared);

    // Nothing loaded/playing -- silence from the moment the engine starts.
    QTRY_VERIFY_WITH_TIMEOUT(detectedSpy.count() >= 1, 3000);
    QCOMPARE(clearedSpy.count(), 0);

    // Real audio resuming must clear the alert.
    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(clearedSpy.count() >= 1, 3000);

    // Must not have re-fired detected() while genuinely playing.
    QCOMPARE(detectedSpy.count(), 1);

    engine.shutdown();
}

QTEST_MAIN(AudioEngineTest)
#include "AudioEngineTest.moc"
