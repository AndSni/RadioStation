#include "audio/GstSourcePipeline.h"

#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include <gst/gst.h>

using namespace radio::audio;

class GstSourcePipelineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void extractsReplayGainTagsFromRealFile();
    void reportsNoReplayGainForUntaggedFile();
    void playImmediatelyAfterLoadUriReachesPlayingOnceStateSettles();
};

void GstSourcePipelineTest::initTestCase()
{
    gst_init(nullptr, nullptr);
}

void GstSourcePipelineTest::extractsReplayGainTagsFromRealFile()
{
    // tone_replaygain.mp3 carries TXXX:REPLAYGAIN_TRACK_GAIN="-6.50 dB" and
    // TXXX:REPLAYGAIN_TRACK_PEAK="0.750000" (written via mutagen, matching
    // the exact real-world format confirmed against an actual library file
    // — see MixEngine plan's Context section). GStreamer's id3demux should
    // parse these into GST_TAG_TRACK_GAIN/GST_TAG_TRACK_PEAK automatically.
    GstSourcePipeline pipeline;
    QVERIFY(pipeline.build());

    bool replayGainChangedFired = false;
    pipeline.onReplayGainChanged = [&replayGainChangedFired]() { replayGainChangedFired = true; };

    QVERIFY(!pipeline.hasReplayGain()); // nothing loaded yet

    pipeline.loadUri(QUrl::fromLocalFile(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone_replaygain.mp3")).toString());
    pipeline.play();

    // Tags typically arrive during/shortly after preroll, well before any
    // meaningful playback — generous timeout for CI-class scheduling.
    QTRY_VERIFY_WITH_TIMEOUT(pipeline.hasReplayGain(), 3000);
    QVERIFY(replayGainChangedFired);

    QVERIFY2(qAbs(pipeline.replayGainDb() - (-6.5)) < 0.05,
        qPrintable(QStringLiteral("expected replayGainDb near -6.5, got %1").arg(pipeline.replayGainDb())));
    QVERIFY2(qAbs(pipeline.replayGainPeak() - 0.75) < 0.01,
        qPrintable(QStringLiteral("expected replayGainPeak near 0.75, got %1").arg(pipeline.replayGainPeak())));

    pipeline.stop();
}

void GstSourcePipelineTest::reportsNoReplayGainForUntaggedFile()
{
    // tone440.wav has no ReplayGain tags at all (and WAV/id3demux isn't
    // even in play here) — hasReplayGain() must stay false rather than
    // reporting stale/default values as if they were real.
    GstSourcePipeline pipeline;
    QVERIFY(pipeline.build());

    pipeline.loadUri(QUrl::fromLocalFile(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav")).toString());
    pipeline.play();

    QTest::qWait(500); // let it play a while — plenty of time for tags to arrive if any existed
    QVERIFY(!pipeline.hasReplayGain());

    pipeline.stop();
}

void GstSourcePipelineTest::playImmediatelyAfterLoadUriReachesPlayingOnceStateSettles()
{
    // loadUri() no longer blocks until PAUSED (preroll) genuinely settles --
    // it defers a play() called before that internally (m_prerollPending /
    // m_pendingPlayAfterLoad) rather than making the caller wait. This makes
    // that deferred-play contract explicit rather than only incidentally
    // exercised by the ReplayGain tests above.
    GstSourcePipeline pipeline;
    QVERIFY(pipeline.build());

    pipeline.loadUri(QUrl::fromLocalFile(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav")).toString());
    pipeline.play();

    QTRY_COMPARE_WITH_TIMEOUT(pipeline.gstState(), GST_STATE_PLAYING, 3000);

    pipeline.stop();
}

QTEST_MAIN(GstSourcePipelineTest)
#include "GstSourcePipelineTest.moc"
