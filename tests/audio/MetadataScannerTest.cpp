#include "audio/MetadataScanner.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <gst/gst.h>

#include <taglib/id3v2tag.h>
#include <taglib/mpegfile.h>
#include <taglib/textidentificationframe.h>

using namespace radio::audio;
using namespace radio::db;

namespace {

// Embeds a TBPM frame and REPLAYGAIN_TRACK_GAIN/_PEAK TXXX frames directly
// via TagLib -- simulates a tag already present on disk, whether written by
// tools/library_cleanup (BPM) or RadioStation's own ReplayGainAnalyzer
// (ReplayGain, see ReplayGainAnalyzerTest.cpp for that writer's own tests).
// MetadataScanner just reads whatever's there either way.
void embedTags(const QString& path)
{
    TagLib::MPEG::File file(path.toUtf8().constData());
    QVERIFY(file.isValid());
    auto* id3v2 = file.ID3v2Tag(true);

    auto* bpmFrame = new TagLib::ID3v2::TextIdentificationFrame("TBPM", TagLib::String::Latin1);
    bpmFrame->setText("140");
    id3v2->addFrame(bpmFrame);

    auto* gainFrame = new TagLib::ID3v2::UserTextIdentificationFrame(TagLib::String::Latin1);
    gainFrame->setDescription("REPLAYGAIN_TRACK_GAIN");
    gainFrame->setText("-5.00 dB");
    id3v2->addFrame(gainFrame);

    auto* peakFrame = new TagLib::ID3v2::UserTextIdentificationFrame(TagLib::String::Latin1);
    peakFrame->setDescription("REPLAYGAIN_TRACK_PEAK");
    peakFrame->setText("0.800000");
    id3v2->addFrame(peakFrame);

    QVERIFY(file.save());
}

// Returns a null-optional-like default TrackRecord (id == -1) if discovery
// never fired -- callers assert on discoveredCount separately for a clear
// failure message rather than this helper trying to QVERIFY from a non-void
// return type.
TrackRecord scanSingleFile(const QString& path, int& outDiscoveredCount)
{
    MetadataScanner scanner({ path });
    QSignalSpy discoveredSpy(&scanner, &MetadataScanner::trackDiscovered);
    QSignalSpy finishedSpy(&scanner, &MetadataScanner::scanFinished);

    scanner.start();
    finishedSpy.wait(30000);
    scanner.wait();

    outDiscoveredCount = discoveredSpy.size();
    if (discoveredSpy.isEmpty())
        return {};
    return discoveredSpy.at(0).at(0).value<TrackRecord>();
}

} // namespace

class MetadataScannerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void bpmAndReplayGainAreReadFromEmbeddedTags();

private:
    QTemporaryDir m_tempDir;
};

void MetadataScannerTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    QVERIFY(m_tempDir.isValid());
}

void MetadataScannerTest::bpmAndReplayGainAreReadFromEmbeddedTags()
{
    const QString workPath = m_tempDir.filePath(QStringLiteral("tagged.mp3"));
    QVERIFY(QFile::copy(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone_untagged.mp3"), workPath));
    embedTags(workPath);

    int discoveredCount = 0;
    const TrackRecord record = scanSingleFile(workPath, discoveredCount);
    QCOMPARE(discoveredCount, 1);

    // RadioStation trusts an embedded BPM/ReplayGain tag unconditionally,
    // regardless of who wrote it (tools/library_cleanup for BPM,
    // ReplayGainAnalyzer or an external tool for ReplayGain) -- there's no
    // verified-fields marker distinguishing "trusted" from "untrusted"
    // writers (see Migrations.cpp's applyV9() doc comment for that
    // mechanism's history).
    QCOMPARE(record.bpm, 140.0);
    QVERIFY(record.hasReplayGain);
    QVERIFY2(qAbs(record.replayGainDb - (-5.0)) < 0.01, qPrintable(QString::number(record.replayGainDb)));
    QVERIFY2(qAbs(record.replayGainPeak - 0.8) < 0.001, qPrintable(QString::number(record.replayGainPeak)));
}

QTEST_MAIN(MetadataScannerTest)
#include "MetadataScannerTest.moc"
