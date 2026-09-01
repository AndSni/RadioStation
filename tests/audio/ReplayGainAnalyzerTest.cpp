#include "audio/MetadataScanner.h"
#include "audio/ReplayGainAnalyzer.h"
#include "db/TrackRecord.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gst/gst.h>

using namespace radio::audio;
using namespace radio::db;

namespace {

// Independent re-read of whatever ReplayGainAnalyzer just wrote into a
// file, via MetadataScanner's completely separate GstDiscoverer-based tag
// path -- this is the "does the tag round-trip through a real, independent
// reader" proof the plan calls for, not just a check that our own writer
// agrees with itself.
TrackRecord rescanFile(const QString& path)
{
    MetadataScanner scanner({ path });
    QSignalSpy discoveredSpy(&scanner, &MetadataScanner::trackDiscovered);
    QSignalSpy finishedSpy(&scanner, &MetadataScanner::scanFinished);

    scanner.start();
    finishedSpy.wait(30000);
    scanner.wait();

    if (discoveredSpy.isEmpty())
        return {};
    return discoveredSpy.at(0).at(0).value<TrackRecord>();
}

} // namespace

class ReplayGainAnalyzerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void computesAndWritesReplayGainForMp3();
    void computesAndWritesReplayGainForFlac();
    void wavIsAnalyzedButNotTaggedInFile();
    void reportsFailureForAnUnreadableFile();

private:
    QTemporaryDir m_tempDir;
};

void ReplayGainAnalyzerTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    QVERIFY(m_tempDir.isValid());
}

void ReplayGainAnalyzerTest::computesAndWritesReplayGainForMp3()
{
    const QString workPath = m_tempDir.filePath(QStringLiteral("mp3_untagged_copy.mp3"));
    QVERIFY(QFile::copy(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone_untagged.mp3"), workPath));

    TrackRecord track;
    track.id = 1;
    track.filePath = workPath;

    ReplayGainAnalyzer analyzer({ track });
    QSignalSpy analyzedSpy(&analyzer, &ReplayGainAnalyzer::trackAnalyzed);
    QSignalSpy failedSpy(&analyzer, &ReplayGainAnalyzer::analysisFailed);
    QSignalSpy finishedSpy(&analyzer, &ReplayGainAnalyzer::analysisFinished);

    analyzer.start();
    QVERIFY(finishedSpy.wait(30000));
    analyzer.wait();

    QVERIFY2(failedSpy.isEmpty(), "expected no analysis failures for a plain untagged mp3");
    QCOMPARE(analyzedSpy.size(), 1);

    const qint64 reportedId = analyzedSpy.at(0).at(0).toLongLong();
    const double reportedGainDb = analyzedSpy.at(0).at(1).toDouble();
    const double reportedPeak = analyzedSpy.at(0).at(2).toDouble();
    QCOMPARE(reportedId, track.id);
    // Not a hardcoded "correct" value (that's rganalysis's own algorithm's
    // job to compute, not this test's) -- just a sanity range wide enough
    // to catch a genuinely broken computation (e.g. reading the wrong tag,
    // a unit mix-up) without being tied to the fixture's exact content.
    QVERIFY2(qAbs(reportedGainDb) < 30.0, qPrintable(QString::number(reportedGainDb)));
    QVERIFY2(reportedPeak > 0.0 && reportedPeak <= 1.0001, qPrintable(QString::number(reportedPeak)));

    const TrackRecord rescanned = rescanFile(workPath);
    QVERIFY2(rescanned.hasReplayGain, "the written REPLAYGAIN tag should round-trip through an independent reader");
    QVERIFY2(qAbs(rescanned.replayGainDb - reportedGainDb) < 0.01,
        qPrintable(QStringLiteral("written %1 vs re-read %2").arg(reportedGainDb).arg(rescanned.replayGainDb)));
    QVERIFY2(qAbs(rescanned.replayGainPeak - reportedPeak) < 0.001,
        qPrintable(QStringLiteral("written %1 vs re-read %2").arg(reportedPeak).arg(rescanned.replayGainPeak)));
}

void ReplayGainAnalyzerTest::computesAndWritesReplayGainForFlac()
{
    const QString workPath = m_tempDir.filePath(QStringLiteral("flac_untagged_copy.flac"));
    QVERIFY(QFile::copy(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone_untagged.flac"), workPath));

    TrackRecord track;
    track.id = 2;
    track.filePath = workPath;

    ReplayGainAnalyzer analyzer({ track });
    QSignalSpy analyzedSpy(&analyzer, &ReplayGainAnalyzer::trackAnalyzed);
    QSignalSpy failedSpy(&analyzer, &ReplayGainAnalyzer::analysisFailed);
    QSignalSpy finishedSpy(&analyzer, &ReplayGainAnalyzer::analysisFinished);

    analyzer.start();
    QVERIFY(finishedSpy.wait(30000));
    analyzer.wait();

    QVERIFY2(failedSpy.isEmpty(), "expected no analysis failures for a plain untagged flac");
    QCOMPARE(analyzedSpy.size(), 1);

    const double reportedGainDb = analyzedSpy.at(0).at(1).toDouble();
    const double reportedPeak = analyzedSpy.at(0).at(2).toDouble();

    // Exercises the Xiph-comment writer path (writeFlacReplayGain), the
    // same shared code Ogg/Opus use -- proves it round-trips through a
    // real, independently-implemented reader too, not just MP3/ID3.
    const TrackRecord rescanned = rescanFile(workPath);
    QVERIFY2(rescanned.hasReplayGain, "the written REPLAYGAIN Xiph comment should round-trip through an independent reader");
    QVERIFY2(qAbs(rescanned.replayGainDb - reportedGainDb) < 0.01,
        qPrintable(QStringLiteral("written %1 vs re-read %2").arg(reportedGainDb).arg(rescanned.replayGainDb)));
    QVERIFY2(qAbs(rescanned.replayGainPeak - reportedPeak) < 0.001,
        qPrintable(QStringLiteral("written %1 vs re-read %2").arg(reportedPeak).arg(rescanned.replayGainPeak)));
}

void ReplayGainAnalyzerTest::wavIsAnalyzedButNotTaggedInFile()
{
    const QString workPath = m_tempDir.filePath(QStringLiteral("wav_copy.wav"));
    QVERIFY(QFile::copy(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), workPath));

    TrackRecord track;
    track.id = 3;
    track.filePath = workPath;

    ReplayGainAnalyzer analyzer({ track });
    QSignalSpy analyzedSpy(&analyzer, &ReplayGainAnalyzer::trackAnalyzed);
    QSignalSpy finishedSpy(&analyzer, &ReplayGainAnalyzer::analysisFinished);

    analyzer.start();
    QVERIFY(finishedSpy.wait(30000));
    analyzer.wait();

    // The gain is still computed and reported (the DB record isn't
    // skipped)...
    QCOMPARE(analyzedSpy.size(), 1);

    // ...but per ReplayGainAnalyzer.h's documented WAV gap, nothing gets
    // written into the file itself -- a rescan must come back with no
    // ReplayGain tag at all, not the value just computed above.
    const TrackRecord rescanned = rescanFile(workPath);
    QVERIFY2(!rescanned.hasReplayGain, "WAV files must not have a ReplayGain tag written into them");
}

void ReplayGainAnalyzerTest::reportsFailureForAnUnreadableFile()
{
    TrackRecord track;
    track.id = 4;
    track.filePath = m_tempDir.filePath(QStringLiteral("does_not_exist.mp3"));

    ReplayGainAnalyzer analyzer({ track });
    QSignalSpy analyzedSpy(&analyzer, &ReplayGainAnalyzer::trackAnalyzed);
    QSignalSpy failedSpy(&analyzer, &ReplayGainAnalyzer::analysisFailed);
    QSignalSpy finishedSpy(&analyzer, &ReplayGainAnalyzer::analysisFinished);

    analyzer.start();
    QVERIFY(finishedSpy.wait(30000));
    analyzer.wait();

    QCOMPARE(analyzedSpy.size(), 0);
    QCOMPARE(failedSpy.size(), 1);
    QCOMPARE(failedSpy.at(0).at(0).toLongLong(), track.id);

    const int analyzedCount = finishedSpy.at(0).at(0).toInt();
    const int failedCount = finishedSpy.at(0).at(1).toInt();
    QCOMPARE(analyzedCount, 0);
    QCOMPARE(failedCount, 1);
}

QTEST_MAIN(ReplayGainAnalyzerTest)
#include "ReplayGainAnalyzerTest.moc"
