#include "ui/AudioObjectMime.h"
#include "ui/PlaylistTrackListWidget.h"

#include <QDropEvent>
#include <QListWidgetItem>
#include <QMimeData>
#include <QSignalSpy>
#include <QTest>

#include <memory>

using namespace radio::ui;

namespace {
// dropEvent() is protected (matching QListWidget's own access level) —
// exposed here the same way DeckWidgetTest.cpp's TestableDeckWidget and
// QueueListWidgetTest.cpp's TestableQueueListWidget do, since a synthetic
// QDropEvent sent via QCoreApplication::sendEvent() is not delivered to a
// widget's dropEvent() override (see feedback_radiostation_dev_discipline
// memory entry) — calling the protected override directly is the reliable
// way to test this.
class TestablePlaylistTrackListWidget : public PlaylistTrackListWidget {
public:
    using PlaylistTrackListWidget::PlaylistTrackListWidget;
    QMimeData* callMimeData(const QList<QListWidgetItem*>& items) const { return mimeData(items); }
    void callDropEvent(QDropEvent* event) { dropEvent(event); }
};
}

class PlaylistTrackListWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void mimeDataCarriesTrackIdNotFromQueue();
    void dropEmitsAudioObjectDroppedForValidPayload();
    void dropIsIgnoredForMalformedPayload();
};

void PlaylistTrackListWidgetTest::mimeDataCarriesTrackIdNotFromQueue()
{
    TestablePlaylistTrackListWidget list;
    auto* item = new QListWidgetItem(QStringLiteral("Some Track"), &list);
    item->setData(PlaylistTrackListWidget::kTrackIdRole, qint64(42));
    item->setData(PlaylistTrackListWidget::kPlaylistItemIdRole, qint64(7));

    std::unique_ptr<QMimeData> mime(list.callMimeData({ item }));
    QVERIFY(mime != nullptr);
    QVERIFY(mime->hasFormat(AudioObjectMime::kMimeType));

    const auto payload = AudioObjectMime::decode(mime.get());
    QCOMPARE(payload.trackId, qint64(42));
    QVERIFY(!payload.fromQueue); // Playlist -> Deck/Queue is never destructive
}

void PlaylistTrackListWidgetTest::dropEmitsAudioObjectDroppedForValidPayload()
{
    TestablePlaylistTrackListWidget list;
    QSignalSpy droppedSpy(&list, &PlaylistTrackListWidget::audioObjectDropped);

    auto* mime = new QMimeData();
    AudioObjectDragPayload payload;
    payload.trackId = 99;
    AudioObjectMime::encode(mime, payload);

    QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    list.callDropEvent(&dropEvent);

    QCOMPARE(droppedSpy.count(), 1);
    const auto emitted = droppedSpy.first().at(0).value<AudioObjectDragPayload>();
    QCOMPARE(emitted.trackId, qint64(99));
    QVERIFY(dropEvent.isAccepted());
}

void PlaylistTrackListWidgetTest::dropIsIgnoredForMalformedPayload()
{
    TestablePlaylistTrackListWidget list;
    QSignalSpy droppedSpy(&list, &PlaylistTrackListWidget::audioObjectDropped);

    auto* mime = new QMimeData(); // no AudioObjectMime payload at all
    QDropEvent dropEvent(QPointF(5, 5), Qt::CopyAction, mime, Qt::LeftButton, Qt::NoModifier);
    list.callDropEvent(&dropEvent);

    QCOMPARE(droppedSpy.count(), 0);
}

QTEST_MAIN(PlaylistTrackListWidgetTest)
#include "PlaylistTrackListWidgetTest.moc"
