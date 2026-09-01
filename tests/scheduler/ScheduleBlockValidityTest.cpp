#include "scheduler/ScheduleBlockValidity.h"

#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;
using namespace radio::scheduler;

class ScheduleBlockValidityTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void deletedStaticPlaylistIsFlagged();
    void emptyStaticPlaylistIsFlagged();
    void staticPlaylistWithOnlyMissingTracksIsFlagged();
    void healthyStaticPlaylistIsOk();

    void deletedSmartPlaylistIsFlagged();
    void emptySmartPlaylistIsFlagged();
    void healthySmartPlaylistIsOk();

    void deletedClockIsFlagged();
    void emptyClockIsFlagged();
    void clockDrivenBlockIsNotFlaggedBroken();
};

void ScheduleBlockValidityTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationScheduleBlockValidityTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationScheduleBlockValidityTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void ScheduleBlockValidityTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
}

void ScheduleBlockValidityTest::deletedStaticPlaylistIsFlagged()
{
    ScheduleBlockRecord block;
    block.playlistId = 987654; // never created
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::TargetDeleted);
    QCOMPARE(ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block)), QStringLiteral("playlist deleted"));
}

void ScheduleBlockValidityTest::emptyStaticPlaylistIsFlagged()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Empty"));

    ScheduleBlockRecord block;
    block.playlistId = playlistId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::TargetEmpty);
}

void ScheduleBlockValidityTest::staticPlaylistWithOnlyMissingTracksIsFlagged()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/validity-missing.mp3");
    track.title = QStringLiteral("Missing Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);
    TrackRepository::syncMissingForRoot(QStringLiteral("/fake"), {}); // flags it missing

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("All Missing"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.playlistId = playlistId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::TargetEmpty);
}

void ScheduleBlockValidityTest::healthyStaticPlaylistIsOk()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/validity-healthy.mp3");
    track.title = QStringLiteral("Healthy Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Healthy"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.playlistId = playlistId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::Ok);
    QVERIFY(ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block)).isEmpty());
}

void ScheduleBlockValidityTest::deletedSmartPlaylistIsFlagged()
{
    ScheduleBlockRecord block;
    block.smartPlaylistId = 987654; // never created
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::TargetDeleted);
}

void ScheduleBlockValidityTest::emptySmartPlaylistIsFlagged()
{
    const qint64 smartPlaylistId
        = SmartPlaylistRepository::create(QStringLiteral("Nothing Matches"), QStringLiteral(R"({"genre": "Nonexistent"})"));

    ScheduleBlockRecord block;
    block.smartPlaylistId = smartPlaylistId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::TargetEmpty);
}

void ScheduleBlockValidityTest::healthySmartPlaylistIsOk()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/validity-smart-healthy.mp3");
    track.title = QStringLiteral("Smart Healthy Track");
    track.genre = QStringLiteral("Ambient");
    TrackRepository::upsertScannedTrack(track);

    const qint64 smartPlaylistId = SmartPlaylistRepository::create(QStringLiteral("Ambient"), QStringLiteral(R"({"genre": "Ambient"})"));

    ScheduleBlockRecord block;
    block.smartPlaylistId = smartPlaylistId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::Ok);
}

void ScheduleBlockValidityTest::deletedClockIsFlagged()
{
    ScheduleBlockRecord block;
    block.clockId = 987654; // never created
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::ClockDeleted);
    QCOMPARE(ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block)), QStringLiteral("clock deleted"));
}

void ScheduleBlockValidityTest::emptyClockIsFlagged()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Empty Clock"));

    ScheduleBlockRecord block;
    block.clockId = clockId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::ClockEmpty);
    QCOMPARE(
        ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block)), QStringLiteral("clock has no elements"));
}

void ScheduleBlockValidityTest::clockDrivenBlockIsNotFlaggedBroken()
{
    // Before the clockId >= 0 branch existed, a clock-driven block (whose
    // playlistId/smartPlaylistId both stay unset, -1) would fall straight
    // into the static-playlist branch and get wrongly flagged
    // TargetDeleted -- the exact bug this test guards against.
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Music Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    ClockRepository::createElement(element);

    ScheduleBlockRecord block;
    block.clockId = clockId;
    QCOMPARE(ScheduleBlockValidity::evaluate(block), ScheduleBlockHealth::Ok);
    QVERIFY(ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block)).isEmpty());
}

QTEST_MAIN(ScheduleBlockValidityTest)
#include "ScheduleBlockValidityTest.moc"
