#include "scheduler/ClockEngine.h"

#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>
#include <QTime>

using namespace radio::scheduler;
using namespace radio::db;

namespace {
qint64 insertTrack(const QString& path, const QString& artist, qint64 durationMs = 180000)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "INSERT INTO tracks (file_path, title, artist, duration_ms, missing, play_count) "
        "VALUES (:path, :path, :artist, :duration_ms, 0, 0)"));
    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":artist"), artist);
    query.bindValue(QStringLiteral(":duration_ms"), durationMs);
    query.exec();
    return query.lastInsertId().toLongLong();
}

// Builds a QDateTime at an explicit minute/second within "today"'s given
// hour, matching the "inject QDateTime, never mock the clock" convention
// this codebase's other time-dependent code (BlockTimeResolver,
// BlockTransitionController::evaluateAt()) already establishes.
QDateTime atHour(int hour, int minute = 0, int second = 0)
{
    return QDateTime(QDate::currentDate(), QTime(hour, minute, second));
}
}

class ClockEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void resumesWithinTheSameHour();
    void restartsOnANewHour();
    void fillLimitReturnsOneWhileParkedOnACart();
    void fillLimitReturnsRemainingItemCountForAMusicElement();
    void generateNextFromClockFillsFromPrecedingMusicElementWhileParkedOnACart();
    void generateNextFromClockReturnsMinusOneWithNoPrecedingMusicElement();
    void noteCartFiredAdvancesPastTheCart();
    void dueHardCutElementIdOnlyFiresWhenHardAndDue();
    void dueHardCutElementIdNeverFiresForCartElements();
    void clockWithZeroElementsIsAlwaysExhausted();
    void changingBlocksClockResetsWheelState();

private:
    qint64 m_playlistId = -1;
    qint64 m_trackId = -1;
};

void ClockEngineTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationClockEngineTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationClockEngineTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void ClockEngineTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));

    m_playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Clock Playlist"));
    m_trackId = insertTrack(QStringLiteral("/fake/track.mp3"), QStringLiteral("Artist"));
    PlaylistRepository::addTrackToPlaylist(m_playlistId, m_trackId, QStringLiteral("manual"));
}

void ClockEngineTest::resumesWithinTheSameHour()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.playlistId = m_playlistId;
    element.itemCount = 3;
    ClockRepository::createElement(element);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();
    QCOMPARE(block.id, blockId);

    ClockEngine engine;
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 5), {}, {}), m_trackId);
    QCOMPARE(ClockRepository::wheelStateFor(blockId).itemsDone, 1);

    // A second call later in the SAME hour must resume (itemsDone
    // continues from 1, not restart at 0).
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 40), {}, {}), m_trackId);
    QCOMPARE(ClockRepository::wheelStateFor(blockId).itemsDone, 2);
}

void ClockEngineTest::restartsOnANewHour()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.playlistId = m_playlistId;
    element.itemCount = 3;
    const qint64 elementId = ClockRepository::createElement(element);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    // Pre-seed a stale wheel state belonging to an earlier hour, mid-way
    // through the (only) element.
    ClockWheelStateRecord stale;
    stale.scheduleBlockId = blockId;
    stale.clockId = clockId;
    stale.hourStartEpoch = atHour(9).toUTC().toSecsSinceEpoch();
    stale.position = 0;
    stale.itemsDone = 2;
    ClockRepository::saveWheelState(stale);

    ClockEngine engine;
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 0), {}, {}), m_trackId);

    const ClockWheelStateRecord fresh = ClockRepository::wheelStateFor(blockId);
    QCOMPARE(fresh.hourStartEpoch, atHour(10).toUTC().toSecsSinceEpoch());
    QCOMPARE(fresh.position, 0);
    QCOMPARE(fresh.itemsDone, 1); // restarted at 0, then this call queued the first item of the new pass
    Q_UNUSED(elementId);
}

void ClockEngineTest::fillLimitReturnsOneWhileParkedOnACart()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord music;
    music.clockId = clockId;
    music.elementType = QStringLiteral("music_playlist");
    music.playlistId = m_playlistId;
    music.itemCount = 1;
    ClockRepository::createElement(music);

    ClockElementRecord cart;
    cart.clockId = clockId;
    cart.elementType = QStringLiteral("cart_specific");
    ClockRepository::createElement(cart);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    // Position the wheel directly on the cart element (index 1).
    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = atHour(10).toUTC().toSecsSinceEpoch();
    state.position = 1;
    ClockRepository::saveWheelState(state);

    ClockEngine engine;
    QCOMPARE(engine.fillLimit(block, atHour(10, 5)), 1);
}

void ClockEngineTest::fillLimitReturnsRemainingItemCountForAMusicElement()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord music;
    music.clockId = clockId;
    music.elementType = QStringLiteral("music_playlist");
    music.playlistId = m_playlistId;
    music.itemCount = 3;
    ClockRepository::createElement(music);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = atHour(10).toUTC().toSecsSinceEpoch();
    state.position = 0;
    state.itemsDone = 1;
    ClockRepository::saveWheelState(state);

    ClockEngine engine;
    QCOMPARE(engine.fillLimit(block, atHour(10, 5)), 2);
}

void ClockEngineTest::generateNextFromClockFillsFromPrecedingMusicElementWhileParkedOnACart()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord music;
    music.clockId = clockId;
    music.elementType = QStringLiteral("music_playlist");
    music.playlistId = m_playlistId;
    music.itemCount = 5;
    ClockRepository::createElement(music);

    ClockElementRecord cart;
    cart.clockId = clockId;
    cart.elementType = QStringLiteral("cart_specific");
    ClockRepository::createElement(cart);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = atHour(10).toUTC().toSecsSinceEpoch();
    state.position = 1; // parked on the cart
    ClockRepository::saveWheelState(state);

    ClockEngine engine;
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 5), {}, {}), m_trackId);

    // The cursor must NOT have advanced past the cart just from a fill.
    QCOMPARE(ClockRepository::wheelStateFor(blockId).position, 1);
}

void ClockEngineTest::generateNextFromClockReturnsMinusOneWithNoPrecedingMusicElement()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord cart;
    cart.clockId = clockId;
    cart.elementType = QStringLiteral("cart_specific");
    ClockRepository::createElement(cart); // the ONLY element -- nothing precedes it

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockEngine engine;
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 5), {}, {}), qint64(-1));
}

void ClockEngineTest::noteCartFiredAdvancesPastTheCart()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord music;
    music.clockId = clockId;
    music.elementType = QStringLiteral("music_playlist");
    music.playlistId = m_playlistId;
    music.itemCount = 1;
    ClockRepository::createElement(music);

    ClockElementRecord cart;
    cart.clockId = clockId;
    cart.elementType = QStringLiteral("cart_specific");
    const qint64 cartElementId = ClockRepository::createElement(cart);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockWheelStateRecord state;
    state.scheduleBlockId = blockId;
    state.clockId = clockId;
    state.hourStartEpoch = atHour(10).toUTC().toSecsSinceEpoch();
    state.position = 1; // parked on the cart
    ClockRepository::saveWheelState(state);

    ClockEngine engine;
    const auto due = engine.dueCartElement(block, atHour(10, 5));
    QVERIFY(due.has_value());
    QCOMPARE(due->id, cartElementId);

    engine.noteCartFired(block, atHour(10, 5));

    const ClockWheelStateRecord after = ClockRepository::wheelStateFor(blockId);
    QCOMPARE(after.position, 2);
    QCOMPARE(after.itemsDone, 0);

    // Once advanced, the same cart must not be reported due again.
    QVERIFY(!engine.dueCartElement(block, atHour(10, 6)).has_value());
}

void ClockEngineTest::dueHardCutElementIdOnlyFiresWhenHardAndDue()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord music;
    music.clockId = clockId;
    music.elementType = QStringLiteral("music_playlist");
    music.playlistId = m_playlistId;
    music.minuteOffset = 15;
    music.timingMode = QStringLiteral("hard");
    const qint64 elementId = ClockRepository::createElement(music);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockEngine engine;
    QVERIFY(!engine.dueHardCutElementId(block, atHour(10, 10)).has_value()); // before due
    const auto due = engine.dueHardCutElementId(block, atHour(10, 15));
    QVERIFY(due.has_value());
    QCOMPARE(*due, elementId);
}

void ClockEngineTest::dueHardCutElementIdNeverFiresForCartElements()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Clock"));
    ClockElementRecord cart;
    cart.clockId = clockId;
    cart.elementType = QStringLiteral("cart_specific");
    cart.minuteOffset = 15;
    cart.timingMode = QStringLiteral("hard");
    ClockRepository::createElement(cart);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockEngine engine;
    // Well past due, still must never report a hard cut for a cart element
    // -- see dueHardCutElementId()'s doc comment for why.
    QVERIFY(!engine.dueHardCutElementId(block, atHour(10, 20)).has_value());
}

void ClockEngineTest::clockWithZeroElementsIsAlwaysExhausted()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Empty Clock"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockEngine engine;
    QCOMPARE(engine.generateNextFromClock(block, atHour(10, 5), {}, {}), qint64(-1));
    QCOMPARE(engine.fillLimit(block, atHour(10, 5)), 0);
    QVERIFY(!engine.dueCartElement(block, atHour(10, 5)).has_value());
    QVERIFY(!engine.dueHardCutElementId(block, atHour(10, 5)).has_value());
}

void ClockEngineTest::changingBlocksClockResetsWheelState()
{
    const qint64 clockA = ClockRepository::createClock(QStringLiteral("Clock A"));
    ClockElementRecord elementA;
    elementA.clockId = clockA;
    elementA.elementType = QStringLiteral("music_playlist");
    elementA.playlistId = m_playlistId;
    elementA.itemCount = 5;
    ClockRepository::createElement(elementA);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.endMinute = 1440;
    block.clockId = clockA;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);
    block = ScheduleBlockRepository::allBlocks().first();

    ClockEngine engine;
    engine.generateNextFromClock(block, atHour(10, 5), {}, {});
    QCOMPARE(ClockRepository::wheelStateFor(blockId).itemsDone, 1);

    // Reassign the block to a DIFFERENT clock.
    const qint64 clockB = ClockRepository::createClock(QStringLiteral("Clock B"));
    ClockElementRecord elementB;
    elementB.clockId = clockB;
    elementB.elementType = QStringLiteral("music_playlist");
    elementB.playlistId = m_playlistId;
    elementB.itemCount = 5;
    ClockRepository::createElement(elementB);
    block.clockId = clockB;
    QVERIFY(ScheduleBlockRepository::updateBlock(block));
    block = ScheduleBlockRepository::allBlocks().first();

    // Even within the SAME hour, the stale clockA-scoped wheel state must
    // not be resumed for clockB.
    engine.generateNextFromClock(block, atHour(10, 6), {}, {});
    const ClockWheelStateRecord after = ClockRepository::wheelStateFor(blockId);
    QCOMPARE(after.clockId, clockB);
    QCOMPARE(after.position, 0);
    QCOMPARE(after.itemsDone, 1); // fresh pass, not resumed from clockA's itemsDone=1
}

QTEST_MAIN(ClockEngineTest)
#include "ClockEngineTest.moc"
