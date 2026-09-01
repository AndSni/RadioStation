#include "ui/AudioObjectMime.h"
#include "ui/TrackTableModel.h"

#include "db/TrackRecord.h"

#include <QMimeData>
#include <QTest>

#include <memory>

using namespace radio::ui;
using namespace radio::db;

class TrackTableModelTest : public QObject {
    Q_OBJECT

private slots:
    void mimeDataCarriesFirstSelectedTracksId();
    void mimeDataOnEmptySelectionReturnsWhateverBaseClassGives();
};

void TrackTableModelTest::mimeDataCarriesFirstSelectedTracksId()
{
    TrackTableModel model;
    TrackRecord trackA;
    trackA.id = 11;
    trackA.title = QStringLiteral("Track A");
    TrackRecord trackB;
    trackB.id = 22;
    trackB.title = QStringLiteral("Track B");
    model.setTracks({ trackA, trackB });

    const QModelIndexList indexes = { model.index(1, TrackTableModel::TitleColumn) };
    std::unique_ptr<QMimeData> mime(model.mimeData(indexes));
    QVERIFY(mime != nullptr);

    QVERIFY(mime->hasFormat(AudioObjectMime::kMimeType));
    const auto payload = AudioObjectMime::decode(mime.get());
    QCOMPARE(payload.trackId, qint64(22));
    QVERIFY(!payload.fromQueue); // Library is never a queue-sourced drag
}

void TrackTableModelTest::mimeDataOnEmptySelectionReturnsWhateverBaseClassGives()
{
    TrackTableModel model;
    std::unique_ptr<QMimeData> mime(model.mimeData({}));
    // Not crashing (no indexes.first() on an empty list) is the point here —
    // Qt's base implementation is free to return null for an empty selection.
    if (mime)
        QVERIFY(!mime->hasFormat(AudioObjectMime::kMimeType));
}

QTEST_MAIN(TrackTableModelTest)
#include "TrackTableModelTest.moc"
