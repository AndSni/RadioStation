#include "audio/AudioEngine.h"
#include "audio/StreamEncoderBin.h"

#include <QSignalSpy>
#include <QTest>

using namespace radio::audio;

class StreamEncoderTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void connectSucceedsGraphWiseAgainstUnreachableServer();
    void unreachableServerReportsErrorOnBus();
    void disconnectDoesNotDisturbLocalPlayback();
    void automaticallyRetriesAfterConnectionError();
    void disconnectCancelsAnyPendingReconnect();
};

void StreamEncoderTest::initTestCase()
{
    gst_init(nullptr, nullptr);
}

void StreamEncoderTest::connectSucceedsGraphWiseAgainstUnreachableServer()
{
    AudioEngine engine;
    engine.start();

    StreamEncoderBin::Config config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 19999; // nothing listening here
    config.mount = QStringLiteral("/test");
    config.format = StreamEncoderBin::Format::Mp3;

    engine.connectStreaming(config);
    QTRY_VERIFY_WITH_TIMEOUT(engine.isStreamingConnected(), 3000);

    engine.shutdown();
}

void StreamEncoderTest::unreachableServerReportsErrorOnBus()
{
    // No Icecast server is running on this machine (confirmed during
    // planning). This verifies the required fallback path: a connection
    // failure is caught as a proper bus ERROR — logged at Error level via
    // AudioEngine's existing bus-error handling — rather than failing
    // silently. shout2send only actually attempts its connection once real
    // buffers start flowing to it, so a deck needs to be playing.
    AudioEngine engine;
    engine.start();

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    QSignalSpy errorSpy(&engine, &AudioEngine::pipelineError);

    StreamEncoderBin::Config config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 19999;
    config.mount = QStringLiteral("/test");
    config.format = StreamEncoderBin::Format::Mp3;

    engine.connectStreaming(config);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() > 0, 15000);

    // Regression test for the engine-thread event drain (see AudioEngine::
    // drainEngineEvents()): the stream encoder's onError callback runs on
    // GstEngineThread's bus dispatch same as a deck's would, and is drained
    // through the identical mutex-guarded-holding-area mechanism rather than
    // a direct cross-thread Qt call -- "stream-" prefix preserved
    // deliberately so MainWindow can route it to the streaming status badge
    // specifically (see MainWindow::onPipelineError).
    QVERIFY2(errorSpy.first().at(0).toString() == QStringLiteral("stream-sink"),
        qPrintable(QStringLiteral("expected pipelineError source \"stream-sink\", got \"%1\"").arg(errorSpy.first().at(0).toString())));

    engine.shutdown();
}

void StreamEncoderTest::disconnectDoesNotDisturbLocalPlayback()
{
    AudioEngine engine;
    engine.start();

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    StreamEncoderBin::Config config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 19999;
    config.format = StreamEncoderBin::Format::Ogg;
    engine.connectStreaming(config);
    QTRY_VERIFY_WITH_TIMEOUT(engine.isStreamingConnected(), 3000);

    // Deck A must be completely unaffected by connecting/disconnecting the
    // streaming branch (the whole point of the blocking-pad-probe hot swap).
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);
    engine.disconnectStreaming();
    QTest::qWait(300);
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);

    engine.shutdown();
}

void StreamEncoderTest::automaticallyRetriesAfterConnectionError()
{
    // Regression test for Phase 2's reconnect-with-backoff: previously a
    // connection error just sat disconnected until an operator noticed and
    // reconnected by hand. The first backoff step is 2s (see
    // StreamEncoderBin::scheduleReconnect()) -- this asserts a
    // "Reconnecting..." status report (reusing the same onError channel)
    // eventually arrives on its own, unprompted, well within that plus
    // margin. Doesn't assume it's specifically the SECOND signal emission:
    // one underlying connection failure can legitimately post more than one
    // raw GST_MESSAGE_ERROR (confirmed -- a real "Internal data stream
    // error." followed the initial connection-refused error in this exact
    // scenario), each surfaced via onError before scheduleReconnect()'s own
    // "already scheduled" guard has anything to suppress.
    AudioEngine engine;
    engine.start();

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    QSignalSpy errorSpy(&engine, &AudioEngine::pipelineError);

    StreamEncoderBin::Config config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 19999; // nothing listening here
    config.mount = QStringLiteral("/test");
    config.format = StreamEncoderBin::Format::Mp3;

    engine.connectStreaming(config);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 15000); // the initial connection error

    // No user action here -- the retry has to happen on its own. Poll for a
    // "Reconnecting" message to appear among ALL signals received so far,
    // rather than assuming a specific position/count.
    bool sawReconnectMessage = false;
    QTRY_VERIFY_WITH_TIMEOUT(
        [&]() {
            for (const QList<QVariant>& emission : errorSpy) {
                if (emission.at(1).toString().contains(QStringLiteral("Reconnecting"))) {
                    sawReconnectMessage = true;
                    return true;
                }
            }
            return false;
        }(),
        20000);
    QVERIFY(sawReconnectMessage);

    engine.shutdown();
}

void StreamEncoderTest::disconnectCancelsAnyPendingReconnect()
{
    // A pending reconnect must not fire after an explicit disconnect() --
    // otherwise the encoder would reconnect out from under an operator who
    // just told it to stop.
    AudioEngine engine;
    engine.start();

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    QSignalSpy errorSpy(&engine, &AudioEngine::pipelineError);

    StreamEncoderBin::Config config;
    config.host = QStringLiteral("127.0.0.1");
    config.port = 19999;
    config.mount = QStringLiteral("/test");
    config.format = StreamEncoderBin::Format::Mp3;

    engine.connectStreaming(config);
    QTRY_VERIFY_WITH_TIMEOUT(errorSpy.count() >= 1, 15000);

    engine.disconnectStreaming();
    QVERIFY(!engine.isStreamingConnected());

    const int countRightAfterDisconnect = errorSpy.count();
    QTest::qWait(4000); // past the first backoff step (2s) with margin
    QCOMPARE(errorSpy.count(), countRightAfterDisconnect); // no further "Reconnecting..." reports

    engine.shutdown();
}

QTEST_MAIN(StreamEncoderTest)
#include "StreamEncoderTest.moc"
