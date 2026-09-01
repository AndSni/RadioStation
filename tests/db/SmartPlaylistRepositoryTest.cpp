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

class SmartPlaylistRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void createByIdUpdateRoundTrip();
    void allSmartPlaylistsOrdersByName();
    void byIdReturnsSentinelForUnknownId();
    void removeClearsReferencingScheduleBlocks();
};

void SmartPlaylistRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationSmartPlaylistRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationSmartPlaylistRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void SmartPlaylistRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
}

void SmartPlaylistRepositoryTest::createByIdUpdateRoundTrip()
{
    const qint64 id = SmartPlaylistRepository::create(
        QStringLiteral("High Energy Ambient"), QStringLiteral(R"({"genre": "Ambient", "energyMin": 0.7})"));
    QVERIFY(id >= 0);

    SmartPlaylistRecord stored = SmartPlaylistRepository::byId(id);
    QCOMPARE(stored.name, QStringLiteral("High Energy Ambient"));
    QCOMPARE(stored.filterJson, QStringLiteral(R"({"genre": "Ambient", "energyMin": 0.7})"));

    QVERIFY(SmartPlaylistRepository::update(
        id, QStringLiteral("Renamed"), QStringLiteral(R"({"genre": "Metal"})")));
    stored = SmartPlaylistRepository::byId(id);
    QCOMPARE(stored.name, QStringLiteral("Renamed"));
    QCOMPARE(stored.filterJson, QStringLiteral(R"({"genre": "Metal"})"));
}

void SmartPlaylistRepositoryTest::allSmartPlaylistsOrdersByName()
{
    SmartPlaylistRepository::create(QStringLiteral("Zebra"), QStringLiteral("{}"));
    SmartPlaylistRepository::create(QStringLiteral("apple"), QStringLiteral("{}"));

    const auto all = SmartPlaylistRepository::allSmartPlaylists();
    QCOMPARE(all.size(), 2);
    QCOMPARE(all.at(0).name, QStringLiteral("apple"));
    QCOMPARE(all.at(1).name, QStringLiteral("Zebra"));
}

void SmartPlaylistRepositoryTest::byIdReturnsSentinelForUnknownId()
{
    QCOMPARE(SmartPlaylistRepository::byId(987654).id, qint64(-1));
}

void SmartPlaylistRepositoryTest::removeClearsReferencingScheduleBlocks()
{
    const qint64 smartPlaylistId = SmartPlaylistRepository::create(QStringLiteral("Targeted"), QStringLiteral("{}"));
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Fallback Static"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Block");
    block.playlistId = playlistId;
    block.smartPlaylistId = smartPlaylistId;
    block.endMinute = 60;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);

    QVERIFY(SmartPlaylistRepository::remove(smartPlaylistId));
    QCOMPARE(SmartPlaylistRepository::byId(smartPlaylistId).id, qint64(-1));

    const auto blocks = ScheduleBlockRepository::allBlocks();
    QCOMPARE(blocks.size(), 1);
    QCOMPARE(blocks.first().id, blockId);
    QCOMPARE(blocks.first().smartPlaylistId, qint64(-1));
}

QTEST_MAIN(SmartPlaylistRepositoryTest)
#include "SmartPlaylistRepositoryTest.moc"
