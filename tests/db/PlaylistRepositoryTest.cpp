#include "db/Database.h"
#include "db/PlaylistFolderRepository.h"
#include "db/PlaylistRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class PlaylistRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void createListRenameDeletePlaylist();
    void listPlaylistsExcludesQueue();
    void deletePlaylistCascadesItsItems();
    void deletePlaylistRefusesForQueue();
    void addTrackToPlaylistAssignsSequentialPosition();
    void setItemOrderRewritesPositions();
    void removeItemDeletesRow();
    void setPlaylistFolderAssignsAndClears();
    void queueMethodsStillWorkAfterRefactor();
    void createPlaylistFromTracksSnapshotsGivenTracksInOrder();

private:
    static qint64 insertTrack(const QString& path);
};

void PlaylistRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationPlaylistRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationPlaylistRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void PlaylistRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_folders"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
}

qint64 PlaylistRepositoryTest::insertTrack(const QString& path)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("INSERT INTO tracks (file_path, title) VALUES (:path, :path)"));
    query.bindValue(QStringLiteral(":path"), path);
    query.exec();
    return query.lastInsertId().toLongLong();
}

void PlaylistRepositoryTest::createListRenameDeletePlaylist()
{
    const qint64 id = PlaylistRepository::createPlaylist(QStringLiteral("Rock Hits"));
    QVERIFY(id >= 0);

    auto playlists = PlaylistRepository::listPlaylists();
    QCOMPARE(playlists.size(), 1);
    QCOMPARE(playlists.first().name, QStringLiteral("Rock Hits"));

    QVERIFY(PlaylistRepository::renamePlaylist(id, QStringLiteral("Rock Anthems")));
    playlists = PlaylistRepository::listPlaylists();
    QCOMPARE(playlists.first().name, QStringLiteral("Rock Anthems"));

    QVERIFY(PlaylistRepository::deletePlaylist(id));
    QVERIFY(PlaylistRepository::listPlaylists().isEmpty());
}

void PlaylistRepositoryTest::listPlaylistsExcludesQueue()
{
    PlaylistRepository::queueItems(); // lazily creates the is_queue=1 row
    PlaylistRepository::createPlaylist(QStringLiteral("Chill"));

    const auto playlists = PlaylistRepository::listPlaylists();
    QCOMPARE(playlists.size(), 1);
    QCOMPARE(playlists.first().name, QStringLiteral("Chill"));
}

void PlaylistRepositoryTest::deletePlaylistCascadesItsItems()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Party"));
    const qint64 trackId = insertTrack(QStringLiteral("/fake/party1.mp3"));
    QVERIFY(PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual")));
    QCOMPARE(PlaylistRepository::itemsForPlaylist(playlistId).size(), 1);

    QVERIFY(PlaylistRepository::deletePlaylist(playlistId));

    QSqlQuery remaining(Database::handle());
    remaining.prepare(QStringLiteral("SELECT COUNT(*) FROM playlist_items WHERE playlist_id = :id"));
    remaining.bindValue(QStringLiteral(":id"), playlistId);
    remaining.exec();
    QVERIFY(remaining.next());
    QCOMPARE(remaining.value(0).toInt(), 0);
}

void PlaylistRepositoryTest::deletePlaylistRefusesForQueue()
{
    PlaylistRepository::queueItems();

    QSqlQuery findQueue(Database::handle());
    findQueue.exec(QStringLiteral("SELECT id FROM playlists WHERE is_queue = 1"));
    QVERIFY(findQueue.next());
    const qint64 queueId = findQueue.value(0).toLongLong();

    QVERIFY(!PlaylistRepository::deletePlaylist(queueId));

    QSqlQuery stillThere(Database::handle());
    stillThere.prepare(QStringLiteral("SELECT COUNT(*) FROM playlists WHERE id = :id"));
    stillThere.bindValue(QStringLiteral(":id"), queueId);
    stillThere.exec();
    QVERIFY(stillThere.next());
    QCOMPARE(stillThere.value(0).toInt(), 1);
}

void PlaylistRepositoryTest::addTrackToPlaylistAssignsSequentialPosition()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Sequential"));
    const qint64 trackA = insertTrack(QStringLiteral("/fake/seq-a.mp3"));
    const qint64 trackB = insertTrack(QStringLiteral("/fake/seq-b.mp3"));

    QVERIFY(PlaylistRepository::addTrackToPlaylist(playlistId, trackA, QStringLiteral("manual")));
    QVERIFY(PlaylistRepository::addTrackToPlaylist(playlistId, trackB, QStringLiteral("manual")));

    const auto items = PlaylistRepository::itemsForPlaylist(playlistId);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).position, 0);
    QCOMPARE(items.at(1).position, 1);
    QCOMPARE(items.at(0).trackId, trackA);
    QCOMPARE(items.at(1).trackId, trackB);
}

void PlaylistRepositoryTest::setItemOrderRewritesPositions()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Reorder"));
    const qint64 trackA = insertTrack(QStringLiteral("/fake/reorder-a.mp3"));
    const qint64 trackB = insertTrack(QStringLiteral("/fake/reorder-b.mp3"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackA, QStringLiteral("manual"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackB, QStringLiteral("manual"));

    auto items = PlaylistRepository::itemsForPlaylist(playlistId);
    const qint64 itemA = items.at(0).playlistItemId;
    const qint64 itemB = items.at(1).playlistItemId;

    QVERIFY(PlaylistRepository::setItemOrder({ itemB, itemA }));

    items = PlaylistRepository::itemsForPlaylist(playlistId);
    QCOMPARE(items.at(0).trackId, trackB);
    QCOMPARE(items.at(1).trackId, trackA);
}

void PlaylistRepositoryTest::removeItemDeletesRow()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Removable"));
    const qint64 trackId = insertTrack(QStringLiteral("/fake/removable.mp3"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    const auto items = PlaylistRepository::itemsForPlaylist(playlistId);
    QCOMPARE(items.size(), 1);

    QVERIFY(PlaylistRepository::removeItem(items.first().playlistItemId));
    QVERIFY(PlaylistRepository::itemsForPlaylist(playlistId).isEmpty());
}

void PlaylistRepositoryTest::setPlaylistFolderAssignsAndClears()
{
    const qint64 folderId = PlaylistFolderRepository::createFolder(QStringLiteral("Shows"));
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Morning Show"));

    QVERIFY(PlaylistRepository::setPlaylistFolder(playlistId, folderId));
    QCOMPARE(PlaylistRepository::listPlaylists().first().folderId, folderId);

    QVERIFY(PlaylistRepository::setPlaylistFolder(playlistId, -1));
    QCOMPARE(PlaylistRepository::listPlaylists().first().folderId, qint64(-1));
}

void PlaylistRepositoryTest::queueMethodsStillWorkAfterRefactor()
{
    const qint64 trackId = insertTrack(QStringLiteral("/fake/queue-regression.mp3"));
    QVERIFY(PlaylistRepository::appendToQueue(trackId, QStringLiteral("manual")));

    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, trackId);
    QCOMPARE(items.first().source, QStringLiteral("manual"));

    // The queue itself must never surface via the generic playlist listing.
    QVERIFY(PlaylistRepository::listPlaylists().isEmpty());
}

void PlaylistRepositoryTest::createPlaylistFromTracksSnapshotsGivenTracksInOrder()
{
    const qint64 trackA = insertTrack(QStringLiteral("/fake/from-tracks-a.mp3"));
    const qint64 trackB = insertTrack(QStringLiteral("/fake/from-tracks-b.mp3"));

    const qint64 playlistId
        = PlaylistRepository::createPlaylistFromTracks(QStringLiteral("Snapshot"), { trackB, trackA });
    QVERIFY(playlistId >= 0);

    const auto playlists = PlaylistRepository::listPlaylists();
    QCOMPARE(playlists.size(), 1);
    QCOMPARE(playlists.first().name, QStringLiteral("Snapshot"));

    const auto items = PlaylistRepository::itemsForPlaylist(playlistId);
    QCOMPARE(items.size(), 2);
    QCOMPARE(items.at(0).trackId, trackB);
    QCOMPARE(items.at(1).trackId, trackA);
    QCOMPARE(items.at(0).source, QStringLiteral("import"));
}

QTEST_MAIN(PlaylistRepositoryTest)
#include "PlaylistRepositoryTest.moc"
