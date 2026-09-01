#include "ui/AudioObjectMime.h"

#include <QMimeData>
#include <QTest>

using namespace radio::ui;

class AudioObjectMimeTest : public QObject {
    Q_OBJECT

private slots:
    void encodeDecodeRoundTrips();
    void decodeReturnsSentinelWhenFormatMissing();
    void decodeReturnsSentinelForNullMimeData();
    void encodeLayersOntoExistingFormatsRatherThanReplacingThem();
};

void AudioObjectMimeTest::encodeDecodeRoundTrips()
{
    QMimeData mime;
    AudioObjectDragPayload payload;
    payload.trackId = 42;
    payload.playlistItemId = 7;
    payload.fromQueue = true;

    AudioObjectMime::encode(&mime, payload);
    QVERIFY(mime.hasFormat(AudioObjectMime::kMimeType));

    const auto decoded = AudioObjectMime::decode(&mime);
    QCOMPARE(decoded.trackId, qint64(42));
    QCOMPARE(decoded.playlistItemId, qint64(7));
    QVERIFY(decoded.fromQueue);
}

void AudioObjectMimeTest::decodeReturnsSentinelWhenFormatMissing()
{
    QMimeData mime;
    const auto decoded = AudioObjectMime::decode(&mime);
    QCOMPARE(decoded.trackId, qint64(-1));
}

void AudioObjectMimeTest::decodeReturnsSentinelForNullMimeData()
{
    const auto decoded = AudioObjectMime::decode(nullptr);
    QCOMPARE(decoded.trackId, qint64(-1));
}

void AudioObjectMimeTest::encodeLayersOntoExistingFormatsRatherThanReplacingThem()
{
    // QueueListWidget::mimeData() relies on encode() adding to an
    // already-populated QMimeData (the base class's own internal-move
    // payload) rather than clobbering it.
    QMimeData mime;
    mime.setText(QStringLiteral("pre-existing"));

    AudioObjectDragPayload payload;
    payload.trackId = 1;
    AudioObjectMime::encode(&mime, payload);

    QCOMPARE(mime.text(), QStringLiteral("pre-existing"));
    QVERIFY(mime.hasFormat(AudioObjectMime::kMimeType));
}

QTEST_MAIN(AudioObjectMimeTest)
#include "AudioObjectMimeTest.moc"
