#include "audio/AudioEngine.h"

#include <QFile>
#include <QTest>

using namespace radio::audio;

// The definitive regression test for the deadlock this whole independent-
// pipeline-per-source architecture exists to eliminate: the old design's
// per-deck GstBins, dynamically attached/detached into one shared live
// audiomixer (GstAggregator), could deadlock inside GStreamer's own
// STATE_LOCK/STREAM_LOCK ordering machinery when a state change raced the
// aggregator's own streaming activity (GitLab gstreamer#349, #91 — see the
// history this replaced in AudioEngine.cpp). Every deck/cart pipeline is now
// fully independent, with no shared aggregator for their state changes to
// ever contend with, so this should be able to hammer load/play/stop/unload
// across both decks — plus concurrent, overlapping cart triggers — as fast
// as possible without ever hanging.
//
// Deliberately no long QTRY_VERIFY_WITH_TIMEOUT waits inside the hammering
// loop itself: the whole point is to fire state changes back-to-back, not
// to wait for each one to settle first (that would just avoid exercising
// the race this test exists to catch). The ctest TIMEOUT set in
// CMakeLists.txt is the actual safety net — if this regresses to a real
// hang, ctest kills it and fails fast instead of wedging the whole suite,
// same reasoning the old AutoDjIntegrationTest TIMEOUT workaround used
// before the underlying deadlock was eliminated (see that test's history).
class DeckEngineStressTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void hammersDecksAndCartsWithoutHanging();
    void rapidReloadSettlesOnTheLatestTrackNotAStaleOne();
};

void DeckEngineStressTest::initTestCase()
{
    gst_init(nullptr, nullptr);
}

void DeckEngineStressTest::hammersDecksAndCartsWithoutHanging()
{
    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    const QString trackB = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    const QString cart = QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav");
    QVERIFY(QFile::exists(trackA));
    QVERIFY(QFile::exists(trackB));
    QVERIFY(QFile::exists(cart));

    AudioEngine engine;
    engine.start();

    constexpr int kIterations = 40;
    for (int i = 0; i < kIterations; ++i) {
        engine.loadTrack(QStringLiteral("A"), trackA);
        engine.loadTrack(QStringLiteral("B"), trackB);
        engine.play(QStringLiteral("A"));
        engine.play(QStringLiteral("B"));

        // Overlapping cart triggers land while both decks are actively
        // state-changing — this interleaving is exactly what the old
        // shared-aggregator design couldn't survive.
        engine.triggerCart(cart);
        engine.triggerCart(cart);

        QTest::qWait(15);

        engine.pause(QStringLiteral("A"));
        engine.stopDeck(QStringLiteral("B"));
        engine.seek(QStringLiteral("A"), 100);

        QTest::qWait(15);

        engine.stopAllCarts();
        engine.unloadDeck(QStringLiteral("A"));
        engine.unloadDeck(QStringLiteral("B"));
    }

    // Reaching here at all (rather than the ctest TIMEOUT firing) already
    // proves the deadlock class is gone. Also confirm the engine is still
    // genuinely responsive afterward, not just "not hung yet".
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    engine.shutdown();
}

void DeckEngineStressTest::rapidReloadSettlesOnTheLatestTrackNotAStaleOne()
{
    // Regression test for the async preroll gate's generation counter
    // (GstSourcePipeline::m_loadGeneration/m_prerollGeneration): a second
    // loadTrack() issued before the first one's PAUSED transition has
    // settled must not let a stale GST_MESSAGE_ASYNC_DONE from the FIRST
    // load resolve the second load's m_prerollPending early. tone440.wav
    // (~4644ms) and tone880.wav (~9288ms) have distinctly different
    // durations specifically so this can assert the deck settles on the
    // SECOND track's actual content, not just "some" track.
    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    const QString trackB = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    QVERIFY(QFile::exists(trackA));
    QVERIFY(QFile::exists(trackB));

    AudioEngine engine;
    engine.start();

    // No intervening play()/wait between these two loads -- deliberately
    // exercises the race the generation counter guards against.
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.loadTrack(QStringLiteral("A"), trackB);
    engine.play(QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QVERIFY2(qAbs(engine.duration(QStringLiteral("A")) - 9288) < 200,
        qPrintable(QStringLiteral("expected duration near tone880.wav's 9288ms, got %1").arg(engine.duration(QStringLiteral("A")))));

    engine.shutdown();
}

QTEST_MAIN(DeckEngineStressTest)
#include "DeckEngineStressTest.moc"
