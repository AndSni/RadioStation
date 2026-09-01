#include "ui/AutoDjPanelWidget.h"

#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/TrackRepository.h"

#include <QColor>
#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QLabel>
#include <QListWidget>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::db;

// The master Auto DJ on/off toggle this widget used to hold (and this test
// file used to cover: titleAndToggleButtonShareOneRow,
// toggleButtonReflectsAutoRunningByDefault, toggleButtonReflectsManualOverride,
// toggleButtonStaysFixedSizeAcrossStates) moved to MainWindow's toolbar —
// see AutoDjPanelWidgetTest.cpp -> MainWindowTest.cpp and
// AutoDjPanelWidget's own doc comment. This widget is now just the
// schedule block list, with no CrossfadeController dependency.
class AutoDjPanelWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void titleLabelIsPresent();
    void blockTargetingDeletedPlaylistIsFlaggedBroken();
    void blockTargetingHealthyPlaylistIsNotFlagged();
};

void AutoDjPanelWidgetTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationAutoDjPanelWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationAutoDjPanelWidgetTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void AutoDjPanelWidgetTest::init()
{
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM tracks"));
}

void AutoDjPanelWidgetTest::titleLabelIsPresent()
{
    AutoDjPanelWidget widget;
    widget.show();

    auto* titleLabel = widget.findChild<QLabel*>(QStringLiteral("autoDjTitleLabel"));
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->text(), QStringLiteral("Auto DJ"));
}

void AutoDjPanelWidgetTest::blockTargetingDeletedPlaylistIsFlaggedBroken()
{
    // playlistId 999999 was never created -- simulates a block left behind
    // after its playlist was deleted.
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Orphaned Block");
    block.playlistId = 999999;
    block.endMinute = 60;
    ScheduleBlockRepository::createBlock(block);

    AutoDjPanelWidget widget;
    widget.show();

    auto* blockList = widget.findChild<QListWidget*>(QStringLiteral("autoDjBlockList"));
    QVERIFY(blockList != nullptr);
    QCOMPARE(blockList->count(), 1);
    QVERIFY(blockList->item(0)->text().contains(QStringLiteral("deleted")));
    QCOMPARE(blockList->item(0)->foreground().color(), QColor(0xdc, 0x26, 0x26));
}

void AutoDjPanelWidgetTest::blockTargetingHealthyPlaylistIsNotFlagged()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/healthy-block-track.mp3");
    track.title = QStringLiteral("Healthy Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Healthy Playlist"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Healthy Block");
    block.playlistId = playlistId;
    block.endMinute = 60;
    ScheduleBlockRepository::createBlock(block);

    AutoDjPanelWidget widget;
    widget.show();

    auto* blockList = widget.findChild<QListWidget*>(QStringLiteral("autoDjBlockList"));
    QVERIFY(blockList != nullptr);
    QCOMPARE(blockList->count(), 1);
    QVERIFY(!blockList->item(0)->text().contains(QStringLiteral("\xE2\x9A\xA0"))); // no warning glyph
}

QTEST_MAIN(AutoDjPanelWidgetTest)
#include "AutoDjPanelWidgetTest.moc"
