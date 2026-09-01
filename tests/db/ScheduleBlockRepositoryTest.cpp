#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/SmartPlaylistRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class ScheduleBlockRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void createListDeleteBlock();
    void createBlockAssignsSequentialPosition();
    void updateBlockPersistsAllFields();
    void setBlockOrderRewritesPositions();
    void deletePlaylistRefusesWhenReferencedByBlock();
    void smartPlaylistIdRoundTripsAndDefaultsToUnset();
    void clockIdRoundTripsAndDefaultsToUnset();
    void updateBlockPreservesClockId();

private:
    qint64 m_playlistId = -1;
};

void ScheduleBlockRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationScheduleBlockRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationScheduleBlockRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void ScheduleBlockRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
    m_playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Test Playlist"));
}

void ScheduleBlockRepositoryTest::createListDeleteBlock()
{
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Morning Show");
    block.daysMask = 0b0011111; // Mon-Fri
    block.startMinute = 6 * 60;
    block.endMinute = 10 * 60;
    block.playlistId = m_playlistId;
    block.selectionMetric = QStringLiteral("rating");
    block.selectionDirection = QStringLiteral("desc");
    block.selectionMode = QStringLiteral("deterministic");
    block.cartFrequency = 5;
    block.cartMode = QStringLiteral("color_sequence");
    block.cartColor = QStringLiteral("#ff8800");
    block.cartOffsetSeconds = 12;
    block.cartPlaybackType = QStringLiteral("insert");
    block.queueMode = QStringLiteral("remaining");
    block.queueSize = 15;

    const qint64 id = ScheduleBlockRepository::createBlock(block);
    QVERIFY(id >= 0);

    auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.first().name, QStringLiteral("Morning Show"));
    QCOMPARE(blocks.first().daysMask, 0b0011111);
    QCOMPARE(blocks.first().startMinute, 6 * 60);
    QCOMPARE(blocks.first().endMinute, 10 * 60);
    QCOMPARE(blocks.first().playlistId, m_playlistId);
    QCOMPARE(blocks.first().selectionMetric, QStringLiteral("rating"));
    QVERIFY(blocks.first().enabled);
    QCOMPARE(blocks.first().cartFrequency, 5);
    QCOMPARE(blocks.first().cartMode, QStringLiteral("color_sequence"));
    QCOMPARE(blocks.first().cartColor, QStringLiteral("#ff8800"));
    QCOMPARE(blocks.first().cartOffsetSeconds, 12);
    QCOMPARE(blocks.first().cartPlaybackType, QStringLiteral("insert"));
    QCOMPARE(blocks.first().queueMode, QStringLiteral("remaining"));
    QCOMPARE(blocks.first().queueSize, 15);

    QVERIFY(ScheduleBlockRepository::deleteBlock(id));
    QVERIFY(ScheduleBlockRepository::allBlocks().isEmpty());
}

void ScheduleBlockRepositoryTest::createBlockAssignsSequentialPosition()
{
    ScheduleBlockRecord a;
    a.name = QStringLiteral("A");
    a.playlistId = m_playlistId;
    a.endMinute = 60;
    ScheduleBlockRecord b;
    b.name = QStringLiteral("B");
    b.playlistId = m_playlistId;
    b.endMinute = 60;

    ScheduleBlockRepository::createBlock(a);
    ScheduleBlockRepository::createBlock(b);

    const auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.size(), 2);
    QCOMPARE(blocks.at(0).position, 0);
    QCOMPARE(blocks.at(1).position, 1);
    QCOMPARE(blocks.at(0).name, QStringLiteral("A"));
    QCOMPARE(blocks.at(1).name, QStringLiteral("B"));
    // Default queue settings round-trip through the DB unchanged - "count"
    // mode, size 0 (inherit AutoDjEngine's global default watermark).
    QCOMPARE(blocks.at(0).queueMode, QStringLiteral("count"));
    QCOMPARE(blocks.at(0).queueSize, 0);
}

void ScheduleBlockRepositoryTest::updateBlockPersistsAllFields()
{
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Original");
    block.playlistId = m_playlistId;
    block.endMinute = 60;
    const qint64 id = ScheduleBlockRepository::createBlock(block);

    ScheduleBlockRecord updated = ScheduleBlockRepository::allBlocks().first();
    updated.name = QStringLiteral("Renamed");
    updated.enabled = false;
    updated.daysMask = 0b1000000; // Sunday only
    updated.startMinute = 100;
    updated.endMinute = 200;
    updated.selectionMetric = QStringLiteral("play_count");
    updated.selectionDirection = QStringLiteral("asc");
    updated.selectionMode = QStringLiteral("random_pool");
    updated.cartFrequency = 10;
    updated.cartMode = QStringLiteral("sequence");
    updated.cartColor = QStringLiteral("#123456");
    updated.cartOffsetSeconds = 7;
    updated.cartPlaybackType = QStringLiteral("overlay");
    updated.queueMode = QStringLiteral("remaining");
    updated.queueSize = 25;

    QVERIFY(ScheduleBlockRepository::updateBlock(updated));

    const auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.size(), 1);
    const auto& stored = blocks.first();
    QCOMPARE(stored.id, id);
    QCOMPARE(stored.name, QStringLiteral("Renamed"));
    QVERIFY(!stored.enabled);
    QCOMPARE(stored.daysMask, 0b1000000);
    QCOMPARE(stored.startMinute, 100);
    QCOMPARE(stored.endMinute, 200);
    QCOMPARE(stored.selectionMetric, QStringLiteral("play_count"));
    QCOMPARE(stored.selectionDirection, QStringLiteral("asc"));
    QCOMPARE(stored.selectionMode, QStringLiteral("random_pool"));
    QCOMPARE(stored.cartFrequency, 10);
    QCOMPARE(stored.cartMode, QStringLiteral("sequence"));
    QCOMPARE(stored.cartColor, QStringLiteral("#123456"));
    QCOMPARE(stored.cartOffsetSeconds, 7);
    QCOMPARE(stored.cartPlaybackType, QStringLiteral("overlay"));
    QCOMPARE(stored.queueMode, QStringLiteral("remaining"));
    QCOMPARE(stored.queueSize, 25);
}

void ScheduleBlockRepositoryTest::setBlockOrderRewritesPositions()
{
    ScheduleBlockRecord a;
    a.name = QStringLiteral("First");
    a.playlistId = m_playlistId;
    a.endMinute = 60;
    ScheduleBlockRecord b;
    b.name = QStringLiteral("Second");
    b.playlistId = m_playlistId;
    b.endMinute = 60;

    const qint64 idA = ScheduleBlockRepository::createBlock(a);
    const qint64 idB = ScheduleBlockRepository::createBlock(b);

    QVERIFY(ScheduleBlockRepository::setBlockOrder({ idB, idA }));

    const auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.at(0).id, idB);
    QCOMPARE(blocks.at(1).id, idA);
}

void ScheduleBlockRepositoryTest::deletePlaylistRefusesWhenReferencedByBlock()
{
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Referencing Block");
    block.playlistId = m_playlistId;
    block.endMinute = 60;
    ScheduleBlockRepository::createBlock(block);

    QVERIFY(!PlaylistRepository::deletePlaylist(m_playlistId));
    QCOMPARE(PlaylistRepository::listPlaylists().size(), 1);
}

void ScheduleBlockRepositoryTest::smartPlaylistIdRoundTripsAndDefaultsToUnset()
{
    ScheduleBlockRecord defaulted;
    defaulted.name = QStringLiteral("Static Only");
    defaulted.playlistId = m_playlistId;
    defaulted.endMinute = 60;
    const qint64 defaultedId = ScheduleBlockRepository::createBlock(defaulted);
    QCOMPARE(ScheduleBlockRepository::allBlocks().first().smartPlaylistId, qint64(-1));

    const qint64 smartPlaylistId = SmartPlaylistRepository::create(QStringLiteral("Targeted"), QStringLiteral("{}"));

    ScheduleBlockRecord updated = ScheduleBlockRepository::allBlocks().first();
    updated.smartPlaylistId = smartPlaylistId;
    QVERIFY(ScheduleBlockRepository::updateBlock(updated));

    auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.first().id, defaultedId);
    QCOMPARE(blocks.first().smartPlaylistId, smartPlaylistId);

    // Clearing it back to -1 (switching back to static) must persist too.
    ScheduleBlockRecord cleared = blocks.first();
    cleared.smartPlaylistId = -1;
    QVERIFY(ScheduleBlockRepository::updateBlock(cleared));
    QCOMPARE(ScheduleBlockRepository::allBlocks().first().smartPlaylistId, qint64(-1));
}

void ScheduleBlockRepositoryTest::clockIdRoundTripsAndDefaultsToUnset()
{
    ScheduleBlockRecord defaulted;
    defaulted.name = QStringLiteral("Static Only");
    defaulted.playlistId = m_playlistId;
    defaulted.endMinute = 60;
    const qint64 defaultedId = ScheduleBlockRepository::createBlock(defaulted);
    QCOMPARE(ScheduleBlockRepository::allBlocks().first().clockId, qint64(-1));

    QSqlQuery insertClock(Database::handle());
    insertClock.exec(QStringLiteral("INSERT INTO clocks (name) VALUES ('Test Clock')"));
    const qint64 clockId = insertClock.lastInsertId().toLongLong();

    ScheduleBlockRecord updated = ScheduleBlockRepository::allBlocks().first();
    updated.clockId = clockId;
    QVERIFY(ScheduleBlockRepository::updateBlock(updated));

    auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.first().id, defaultedId);
    QCOMPARE(blocks.first().clockId, clockId);

    // Clearing it back to -1 (switching back to a simple block) must persist too.
    ScheduleBlockRecord cleared = blocks.first();
    cleared.clockId = -1;
    QVERIFY(ScheduleBlockRepository::updateBlock(cleared));
    QCOMPARE(ScheduleBlockRepository::allBlocks().first().clockId, qint64(-1));
}

void ScheduleBlockRepositoryTest::updateBlockPreservesClockId()
{
    // Regression guard: updateBlock() is a full-column UPDATE. Omitting
    // clock_id from that statement would silently clear a block's clock on
    // any unrelated edit (e.g. just renaming it) -- exactly the bug a
    // design review caught before this shipped.
    QSqlQuery insertClock(Database::handle());
    insertClock.exec(QStringLiteral("INSERT INTO clocks (name) VALUES ('Test Clock')"));
    const qint64 clockId = insertClock.lastInsertId().toLongLong();

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Clock Driven");
    block.endMinute = 60;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);

    ScheduleBlockRecord renamed = ScheduleBlockRepository::allBlocks().first();
    QCOMPARE(renamed.clockId, clockId);
    renamed.name = QStringLiteral("Clock Driven (renamed)");
    QVERIFY(ScheduleBlockRepository::updateBlock(renamed));

    QCOMPARE(ScheduleBlockRepository::allBlocks().first().clockId, clockId);
}

QTEST_MAIN(ScheduleBlockRepositoryTest)
#include "ScheduleBlockRepositoryTest.moc"
