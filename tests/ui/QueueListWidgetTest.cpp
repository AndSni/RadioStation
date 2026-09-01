#include "ui/AudioObjectMime.h"
#include "ui/QueueListWidget.h"

#include <QDropEvent>
#include <QListWidgetItem>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace radio::ui;

namespace {
// mimeData()/dropEvent() are protected (matching QListWidget's own access
// level for the former) — this test-only subclass exposes them, the
// standard Qt pattern for testing a protected virtual without widening the
// production class's public API. Note dropEvent() specifically cannot be
// exercised via QCoreApplication::sendEvent() — Qt does not route a
// synthetic QDropEvent through a widget's dropEvent() override the way it
// does for mouse/keyboard events (see feedback_radiostation_dev_discipline
// memory entry) — calling the protected override directly, as done here, is
// the reliable way to test it.
class TestableQueueListWidget : public QueueListWidget {
public:
    using QueueListWidget::QueueListWidget;
    QMimeData* callMimeData(const QList<QListWidgetItem*>& items) const { return mimeData(items); }
    QStringList callMimeTypes() const { return mimeTypes(); }
    void callDropEvent(QDropEvent* event) { dropEvent(event); }
};
}

class QueueListWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void mimeDataCarriesCustomPayloadAlongsideInternalMoveData();
    void mimeDataCarriesFirstSelectedItemWhenMultipleGiven();
    void foreignDropEmitsAudioObjectDropped();
    void foreignDropWithMalformedPayloadIsIgnored();
};

void QueueListWidgetTest::mimeDataCarriesCustomPayloadAlongsideInternalMoveData()
{
    TestableQueueListWidget list;
    auto* item = new QListWidgetItem(QStringLiteral("Some Track"), &list);
    item->setData(QueueListWidget::kTrackIdRole, qint64(42));
    item->setData(QueueListWidget::kPlaylistItemIdRole, qint64(7));

    // Whatever formats the base class itself declares as what it needs for
    // its own internal-move handling — checked at runtime rather than
    // hardcoding Qt's undocumented internal MIME type string, so this stays
    // correct across Qt versions.
    const QStringList reorderFormats = list.callMimeTypes();
    QVERIFY(!reorderFormats.isEmpty());

    std::unique_ptr<QMimeData> mime(list.callMimeData({ item }));
    QVERIFY(mime != nullptr);

    for (const QString& format : reorderFormats)
        QVERIFY2(mime->hasFormat(format), qPrintable(QStringLiteral("missing base-class reorder format: %1").arg(format)));

    // Layered custom payload for a foreign drop target (a Deck) to read.
    QVERIFY(mime->hasFormat(AudioObjectMime::kMimeType));
    const auto payload = AudioObjectMime::decode(mime.get());
    QCOMPARE(payload.trackId, qint64(42));
    QCOMPARE(payload.playlistItemId, qint64(7));
    QVERIFY(payload.fromQueue);
}

void QueueListWidgetTest::mimeDataCarriesFirstSelectedItemWhenMultipleGiven()
{
    TestableQueueListWidget list;
    auto* first = new QListWidgetItem(QStringLiteral("First"), &list);
    first->setData(QueueListWidget::kTrackIdRole, qint64(1));
    first->setData(QueueListWidget::kPlaylistItemIdRole, qint64(10));
    auto* second = new QListWidgetItem(QStringLiteral("Second"), &list);
    second->setData(QueueListWidget::kTrackIdRole, qint64(2));
    second->setData(QueueListWidget::kPlaylistItemIdRole, qint64(20));

    std::unique_ptr<QMimeData> mime(list.callMimeData({ first, second }));
    QVERIFY(mime != nullptr);

    // A multi-selection drag only makes sense as a foreign drop target for
    // a single track (a Deck holds one track) — the custom payload
    // deliberately only describes the first item; Qt's own reorder handling
    // still moves every selected row correctly via its own payload.
    const auto payload = AudioObjectMime::decode(mime.get());
    QCOMPARE(payload.trackId, qint64(1));
    QCOMPARE(payload.playlistItemId, qint64(10));
}

void QueueListWidgetTest::foreignDropEmitsAudioObjectDropped()
{
    // A manually-constructed QDropEvent's source() is null, never `&list`
    // itself — indistinguishable, for this handler's purposes, from a real
    // drop originating in a different widget (Library/Playlist), which is
    // exactly the case this test targets. A genuine self-drop (dragging
    // within the Queue to reorder) can only be produced by a real drag
    // gesture — see QueueListWidget's own doc comment; that path is
    // manual-test-only, already confirmed working by the user.
    TestableQueueListWidget list;
    QSignalSpy droppedSpy(&list, &QueueListWidget::audioObjectDropped);

    auto* mime = new QMimeData();
    AudioObjectDragPayload payload;
    payload.trackId = 55;
    AudioObjectMime::encode(mime, payload);

    QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    list.callDropEvent(&dropEvent);

    QCOMPARE(droppedSpy.count(), 1);
    const auto emitted = droppedSpy.first().at(0).value<AudioObjectDragPayload>();
    QCOMPARE(emitted.trackId, qint64(55));
    QVERIFY(dropEvent.isAccepted());
}

void QueueListWidgetTest::foreignDropWithMalformedPayloadIsIgnored()
{
    TestableQueueListWidget list;
    QSignalSpy droppedSpy(&list, &QueueListWidget::audioObjectDropped);

    auto* mime = new QMimeData(); // no AudioObjectMime payload at all
    QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    list.callDropEvent(&dropEvent);

    QCOMPARE(droppedSpy.count(), 0);
}

QTEST_MAIN(QueueListWidgetTest)
#include "QueueListWidgetTest.moc"
