#include "db/Database.h"
#include "db/PlaylistFolderRepository.h"
#include "db/PlaylistRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class PlaylistFolderRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void createRenameDeleteFolder();
    void setFolderOrderRewritesPositions();
    void deleteFolderClearsMemberPlaylistsWithoutDeletingThem();
};

void PlaylistFolderRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationPlaylistFolderRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationPlaylistFolderRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void PlaylistFolderRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_folders"));
}

void PlaylistFolderRepositoryTest::createRenameDeleteFolder()
{
    const qint64 id = PlaylistFolderRepository::createFolder(QStringLiteral("Shows"));
    QVERIFY(id >= 0);

    auto folders = PlaylistFolderRepository::allFolders();
    QCOMPARE(folders.size(), 1);
    QCOMPARE(folders.first().name, QStringLiteral("Shows"));

    QVERIFY(PlaylistFolderRepository::renameFolder(id, QStringLiteral("Weekday Shows")));
    folders = PlaylistFolderRepository::allFolders();
    QCOMPARE(folders.first().name, QStringLiteral("Weekday Shows"));

    QVERIFY(PlaylistFolderRepository::deleteFolder(id));
    QVERIFY(PlaylistFolderRepository::allFolders().isEmpty());
}

void PlaylistFolderRepositoryTest::setFolderOrderRewritesPositions()
{
    const qint64 first = PlaylistFolderRepository::createFolder(QStringLiteral("First"));
    const qint64 second = PlaylistFolderRepository::createFolder(QStringLiteral("Second"));

    QVERIFY(PlaylistFolderRepository::setFolderOrder({ second, first }));

    const auto folders = PlaylistFolderRepository::allFolders();
    QCOMPARE(folders.at(0).id, second);
    QCOMPARE(folders.at(1).id, first);
}

void PlaylistFolderRepositoryTest::deleteFolderClearsMemberPlaylistsWithoutDeletingThem()
{
    const qint64 folderId = PlaylistFolderRepository::createFolder(QStringLiteral("Shows"));
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Morning Show"), folderId);
    QCOMPARE(PlaylistRepository::listPlaylists().first().folderId, folderId);

    QVERIFY(PlaylistFolderRepository::deleteFolder(folderId));

    const auto playlists = PlaylistRepository::listPlaylists();
    QCOMPARE(playlists.size(), 1);
    QCOMPARE(playlists.first().id, playlistId);
    QCOMPARE(playlists.first().folderId, qint64(-1));
}

QTEST_MAIN(PlaylistFolderRepositoryTest)
#include "PlaylistFolderRepositoryTest.moc"
