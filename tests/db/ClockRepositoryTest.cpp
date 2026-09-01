#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class ClockRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void createRenameRoundTrip();
    void allClocksOrdersByName();
    void clockByIdReturnsSentinelForUnknownId();
    void elementByIdReturnsSentinelForUnknownId();
    void createElementAssignsSequentialPositionScopedToClock();
    void elementFieldsRoundTripIncludingNullableSentinels();
    void setElementOrderRewritesPositions();
    void deleteClockHandCascades();
    void wheelStateUpsertAndClear();
};

void ClockRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationClockRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationClockRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void ClockRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
}

void ClockRepositoryTest::createRenameRoundTrip()
{
    const qint64 id = ClockRepository::createClock(QStringLiteral("Weekday AM"));
    QVERIFY(id >= 0);

    ClockRecord stored = ClockRepository::clockById(id);
    QCOMPARE(stored.id, id);
    QCOMPARE(stored.name, QStringLiteral("Weekday AM"));

    QVERIFY(ClockRepository::renameClock(id, QStringLiteral("Weekday AM (renamed)")));
    QCOMPARE(ClockRepository::clockById(id).name, QStringLiteral("Weekday AM (renamed)"));
}

void ClockRepositoryTest::allClocksOrdersByName()
{
    ClockRepository::createClock(QStringLiteral("Zebra Clock"));
    ClockRepository::createClock(QStringLiteral("apple Clock"));

    const auto all = ClockRepository::allClocks();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).name, QStringLiteral("apple Clock"));
    QCOMPARE(all.at(1).name, QStringLiteral("Zebra Clock"));
}

void ClockRepositoryTest::clockByIdReturnsSentinelForUnknownId()
{
    QCOMPARE(ClockRepository::clockById(987654).id, qint64(-1));
}

void ClockRepositoryTest::elementByIdReturnsSentinelForUnknownId()
{
    QCOMPARE(ClockRepository::elementById(987654).id, qint64(-1));

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.label = QStringLiteral("Morning Mix");
    const qint64 id = ClockRepository::createElement(element);

    const ClockElementRecord found = ClockRepository::elementById(id);
    QCOMPARE(found.id, id);
    QCOMPARE(found.label, QStringLiteral("Morning Mix"));
}

void ClockRepositoryTest::createElementAssignsSequentialPositionScopedToClock()
{
    const qint64 clockA = ClockRepository::createClock(QStringLiteral("Clock A"));
    const qint64 clockB = ClockRepository::createClock(QStringLiteral("Clock B"));

    ClockElementRecord element;
    element.elementType = QStringLiteral("music_playlist");

    element.clockId = clockA;
    const qint64 a0 = ClockRepository::createElement(element);
    const qint64 a1 = ClockRepository::createElement(element);
    const qint64 a2 = ClockRepository::createElement(element);

    element.clockId = clockB;
    const qint64 b0 = ClockRepository::createElement(element);
    const qint64 b1 = ClockRepository::createElement(element);

    const auto elementsA = ClockRepository::elementsForClock(clockA);
    QCOMPARE(elementsA.size(), 3);
    QCOMPARE(elementsA.at(0).id, a0);
    QCOMPARE(elementsA.at(0).position, 0);
    QCOMPARE(elementsA.at(1).id, a1);
    QCOMPARE(elementsA.at(1).position, 1);
    QCOMPARE(elementsA.at(2).id, a2);
    QCOMPARE(elementsA.at(2).position, 2);

    const auto elementsB = ClockRepository::elementsForClock(clockB);
    QCOMPARE(elementsB.size(), 2);
    QCOMPARE(elementsB.at(0).id, b0);
    QCOMPARE(elementsB.at(0).position, 0);
    QCOMPARE(elementsB.at(1).id, b1);
    QCOMPARE(elementsB.at(1).position, 1);
}

void ClockRepositoryTest::elementFieldsRoundTripIncludingNullableSentinels()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Music"));

    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("cart_specific");
    element.label = QStringLiteral("Weather");
    element.itemCount = 1;
    element.minuteOffset = -1; // flow
    element.timingMode = QStringLiteral("soft");
    const qint64 id = ClockRepository::createElement(element);

    ClockElementRecord stored = ClockRepository::elementsForClock(clockId).first();
    QCOMPARE(stored.id, id);
    QCOMPARE(stored.minuteOffset, -1);
    QCOMPARE(stored.playlistId, qint64(-1));
    QCOMPARE(stored.smartPlaylistId, qint64(-1));
    QCOMPARE(stored.cartClipId, qint64(-1));

    stored.elementType = QStringLiteral("music_playlist");
    stored.minuteOffset = 15;
    stored.timingMode = QStringLiteral("hard");
    stored.playlistId = playlistId;
    QVERIFY(ClockRepository::updateElement(stored));

    ClockElementRecord updated = ClockRepository::elementsForClock(clockId).first();
    QCOMPARE(updated.minuteOffset, 15);
    QCOMPARE(updated.timingMode, QStringLiteral("hard"));
    QCOMPARE(updated.playlistId, playlistId);

    // Clearing back to the flow/sentinel state must persist too.
    updated.minuteOffset = -1;
    updated.playlistId = -1;
    QVERIFY(ClockRepository::updateElement(updated));
    ClockElementRecord cleared = ClockRepository::elementsForClock(clockId).first();
    QCOMPARE(cleared.minuteOffset, -1);
    QCOMPARE(cleared.playlistId, qint64(-1));
}

void ClockRepositoryTest::setElementOrderRewritesPositions()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    const qint64 first = ClockRepository::createElement(element);
    const qint64 second = ClockRepository::createElement(element);
    const qint64 third = ClockRepository::createElement(element);

    QVERIFY(ClockRepository::setElementOrder({ third, first, second }));

    const auto elements = ClockRepository::elementsForClock(clockId);
    QCOMPARE(elements.at(0).id, third);
    QCOMPARE(elements.at(0).position, 0);
    QCOMPARE(elements.at(1).id, first);
    QCOMPARE(elements.at(1).position, 1);
    QCOMPARE(elements.at(2).id, second);
    QCOMPARE(elements.at(2).position, 2);
}

void ClockRepositoryTest::deleteClockHandCascades()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    ClockRepository::createElement(element);
    ClockRepository::createElement(element);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 60;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);

    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = 1700000000;
    state.position = 1;
    QVERIFY(ClockRepository::saveWheelState(state));

    QVERIFY(ClockRepository::deleteClock(clockId));

    QCOMPARE(ClockRepository::clockById(clockId).id, qint64(-1));
    QVERIFY(ClockRepository::elementsForClock(clockId).isEmpty());
    QCOMPARE(ClockRepository::wheelStateFor(blockId).scheduleBlockId, qint64(-1));
    QCOMPARE(ScheduleBlockRepository::allBlocks().first().clockId, qint64(-1));
}

void ClockRepositoryTest::wheelStateUpsertAndClear()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 60;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);

    QCOMPARE(ClockRepository::wheelStateFor(blockId).scheduleBlockId, qint64(-1));

    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = 1700000000;
    state.position = 2;
    state.itemsDone = 1;
    QVERIFY(ClockRepository::saveWheelState(state));

    ClockWheelStateRecord stored = ClockRepository::wheelStateFor(blockId);
    QCOMPARE(stored.hourStartEpoch, qint64(1700000000));
    QCOMPARE(stored.position, 2);
    QCOMPARE(stored.itemsDone, 1);

    // Re-saving (a later advance) must UPSERT, not fail on the existing PK.
    state.position = 3;
    state.itemsDone = 0;
    QVERIFY(ClockRepository::saveWheelState(state));
    QCOMPARE(ClockRepository::wheelStateFor(blockId).position, 3);

    QVERIFY(ClockRepository::clearWheelState(blockId));
    QCOMPARE(ClockRepository::wheelStateFor(blockId).scheduleBlockId, qint64(-1));
}

QTEST_MAIN(ClockRepositoryTest)
#include "ClockRepositoryTest.moc"
