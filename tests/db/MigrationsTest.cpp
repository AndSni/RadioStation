#include "db/Database.h"
#include "db/Migrations.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class MigrationsTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void freshDatabaseReachesLatestVersion();
    void reapplyingMigrationsIsIdempotent();
    void defaultRatingIsZero();
    void musicCategoryRenamedToSong();
    void v9ResetsStaleBpmAndReplayGainDataButNotEnergy();
};

void MigrationsTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationMigrationsTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationMigrationsTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void MigrationsTest::freshDatabaseReachesLatestVersion()
{
    QSqlDatabase db = Database::handle();

    QSqlQuery versionQuery(db);
    versionQuery.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations"));
    QVERIFY(versionQuery.next());
    QCOMPARE(versionQuery.value(0).toInt(), 14);

    QSqlQuery tracksInfo(db);
    tracksInfo.exec(QStringLiteral("PRAGMA table_info(tracks)"));
    bool hasRating = false;
    while (tracksInfo.next()) {
        if (tracksInfo.value(QStringLiteral("name")).toString() == QStringLiteral("rating"))
            hasRating = true;
    }
    QVERIFY(hasRating);

    QSqlQuery masterQuery(db);
    masterQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    masterQuery.bindValue(QStringLiteral(":name"), QStringLiteral("playlist_folders"));
    masterQuery.exec();
    QVERIFY(masterQuery.next());

    QSqlQuery scheduleBlocksQuery(db);
    scheduleBlocksQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    scheduleBlocksQuery.bindValue(QStringLiteral(":name"), QStringLiteral("schedule_blocks"));
    scheduleBlocksQuery.exec();
    QVERIFY(scheduleBlocksQuery.next());

    QSqlQuery scheduleBlocksInfo(db);
    scheduleBlocksInfo.exec(QStringLiteral("PRAGMA table_info(schedule_blocks)"));
    bool hasCartFrequency = false;
    bool hasCartPlaybackType = false;
    bool hasQueueMode = false;
    bool hasQueueSize = false;
    while (scheduleBlocksInfo.next()) {
        const QString columnName = scheduleBlocksInfo.value(QStringLiteral("name")).toString();
        if (columnName == QStringLiteral("cart_frequency"))
            hasCartFrequency = true;
        if (columnName == QStringLiteral("cart_playback_type"))
            hasCartPlaybackType = true;
        if (columnName == QStringLiteral("queue_mode"))
            hasQueueMode = true;
        if (columnName == QStringLiteral("queue_size"))
            hasQueueSize = true;
    }
    QVERIFY(hasCartFrequency);
    QVERIFY(hasCartPlaybackType);
    QVERIFY(hasQueueMode);
    QVERIFY(hasQueueSize);

    QSqlQuery playlistsInfo(db);
    playlistsInfo.exec(QStringLiteral("PRAGMA table_info(playlists)"));
    bool hasFolderId = false;
    while (playlistsInfo.next()) {
        if (playlistsInfo.value(QStringLiteral("name")).toString() == QStringLiteral("folder_id"))
            hasFolderId = true;
    }
    QVERIFY(hasFolderId);

    QSqlQuery tracksInfoV7(db);
    tracksInfoV7.exec(QStringLiteral("PRAGMA table_info(tracks)"));
    bool hasYear = false;
    bool hasEnergy = false;
    bool hasBpm = false;
    while (tracksInfoV7.next()) {
        const QString columnName = tracksInfoV7.value(QStringLiteral("name")).toString();
        if (columnName == QStringLiteral("year"))
            hasYear = true;
        if (columnName == QStringLiteral("energy"))
            hasEnergy = true;
        if (columnName == QStringLiteral("bpm"))
            hasBpm = true;
    }
    QVERIFY(hasYear);
    QVERIFY(hasEnergy);
    QVERIFY(hasBpm);

    QSqlQuery smartPlaylistsQuery(db);
    smartPlaylistsQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    smartPlaylistsQuery.bindValue(QStringLiteral(":name"), QStringLiteral("smart_playlists"));
    smartPlaylistsQuery.exec();
    QVERIFY(smartPlaylistsQuery.next());

    QSqlQuery scheduleBlocksInfoV7(db);
    scheduleBlocksInfoV7.exec(QStringLiteral("PRAGMA table_info(schedule_blocks)"));
    bool hasSmartPlaylistId = false;
    while (scheduleBlocksInfoV7.next()) {
        if (scheduleBlocksInfoV7.value(QStringLiteral("name")).toString() == QStringLiteral("smart_playlist_id"))
            hasSmartPlaylistId = true;
    }
    QVERIFY(hasSmartPlaylistId);

    QSqlQuery tracksInfoV8(db);
    tracksInfoV8.exec(QStringLiteral("PRAGMA table_info(tracks)"));
    bool hasHasReplayGain = false;
    bool hasReplayGainDb = false;
    bool hasReplayGainPeak = false;
    while (tracksInfoV8.next()) {
        const QString columnName = tracksInfoV8.value(QStringLiteral("name")).toString();
        if (columnName == QStringLiteral("has_replay_gain"))
            hasHasReplayGain = true;
        if (columnName == QStringLiteral("replay_gain_db"))
            hasReplayGainDb = true;
        if (columnName == QStringLiteral("replay_gain_peak"))
            hasReplayGainPeak = true;
    }
    QVERIFY(hasHasReplayGain);
    QVERIFY(hasReplayGainDb);
    QVERIFY(hasReplayGainPeak);

    QSqlQuery loudnessHistoryQuery(db);
    loudnessHistoryQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    loudnessHistoryQuery.bindValue(QStringLiteral(":name"), QStringLiteral("loudness_history"));
    loudnessHistoryQuery.exec();
    QVERIFY(loudnessHistoryQuery.next());

    for (const QString& tableName : { QStringLiteral("clocks"), QStringLiteral("clock_elements"), QStringLiteral("clock_wheel_state") }) {
        QSqlQuery tableQuery(db);
        tableQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
        tableQuery.bindValue(QStringLiteral(":name"), tableName);
        tableQuery.exec();
        QVERIFY2(tableQuery.next(), qPrintable(tableName));
    }

    QSqlQuery scheduleBlocksInfoV12(db);
    scheduleBlocksInfoV12.exec(QStringLiteral("PRAGMA table_info(schedule_blocks)"));
    bool hasClockId = false;
    while (scheduleBlocksInfoV12.next()) {
        if (scheduleBlocksInfoV12.value(QStringLiteral("name")).toString() == QStringLiteral("clock_id"))
            hasClockId = true;
    }
    QVERIFY(hasClockId);

    QSqlQuery ftsQuery(db);
    ftsQuery.prepare(QStringLiteral("SELECT name FROM sqlite_master WHERE type = 'table' AND name = :name"));
    ftsQuery.bindValue(QStringLiteral(":name"), QStringLiteral("tracks_fts"));
    ftsQuery.exec();
    QVERIFY(ftsQuery.next());
}

void MigrationsTest::musicCategoryRenamedToSong()
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT COUNT(*) FROM rotation_categories WHERE name = :name"));
    query.bindValue(QStringLiteral(":name"), QStringLiteral("Song"));
    QVERIFY(query.exec());
    QVERIFY(query.next());
    QCOMPARE(query.value(0).toInt(), 1);

    QSqlQuery musicQuery(Database::handle());
    musicQuery.prepare(QStringLiteral("SELECT COUNT(*) FROM rotation_categories WHERE name = :name"));
    musicQuery.bindValue(QStringLiteral(":name"), QStringLiteral("Music"));
    QVERIFY(musicQuery.exec());
    QVERIFY(musicQuery.next());
    QCOMPARE(musicQuery.value(0).toInt(), 0);
}

void MigrationsTest::reapplyingMigrationsIsIdempotent()
{
    QSqlDatabase db = Database::handle();
    // Without the columnExists() guards in applyV2(), this second call fails
    // with "duplicate column name" on the ALTER TABLE statements.
    QVERIFY(Migrations::run(db));
    QVERIFY(Migrations::run(db));
}

void MigrationsTest::v9ResetsStaleBpmAndReplayGainDataButNotEnergy()
{
    QSqlDatabase db = Database::handle();

    // initTestCase() already migrated this DB all the way to the latest
    // version. To actually exercise v9's reset behavior (as opposed to just
    // its schema, already covered above), rewind it to "pretend v8": the v9
    // migration is data-only (no columns added/removed), so the table
    // shape is identical either way -- only the recorded version and the
    // row's data need to change. Deletes every row >= 9, not just 9 itself
    // -- currentVersion() reads MAX(version), so leaving a later migration's
    // row (e.g. v10) in place would make MAX(version) still >= 9 and skip
    // re-running applyV9() below entirely.
    QVERIFY(QSqlQuery(db).exec(QStringLiteral("DELETE FROM schema_migrations WHERE version >= 9")));

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(R"(INSERT INTO tracks
        (file_path, title, bpm, energy, has_replay_gain, replay_gain_db, replay_gain_peak)
        VALUES (:path, :title, :bpm, :energy, 1, :gain_db, :peak))"));
    insert.bindValue(QStringLiteral(":path"), QStringLiteral("/fake/v9-reset-test.mp3"));
    insert.bindValue(QStringLiteral(":title"), QStringLiteral("V9 Reset Test"));
    insert.bindValue(QStringLiteral(":bpm"), 140.0);
    insert.bindValue(QStringLiteral(":energy"), 0.75);
    insert.bindValue(QStringLiteral(":gain_db"), -8.0);
    insert.bindValue(QStringLiteral(":peak"), 0.9);
    QVERIFY(insert.exec());
    const qint64 id = insert.lastInsertId().toLongLong();

    QVERIFY(Migrations::run(db));

    const TrackRecord reset = TrackRepository::trackById(id);
    QCOMPARE(reset.bpm, -1.0);
    QVERIFY(!reset.hasReplayGain);
    QCOMPARE(reset.replayGainDb, 0.0);
    QCOMPARE(reset.replayGainPeak, 0.0);
    // Energy is deliberately untouched by v9 -- it was never exposed to
    // the untrusted-embedded-tag problem in the first place (see
    // Migrations.cpp's applyV9() doc comment).
    QCOMPARE(reset.energy, 0.75);
}

void MigrationsTest::defaultRatingIsZero()
{
    QSqlQuery insert(Database::handle());
    insert.prepare(QStringLiteral("INSERT INTO tracks (file_path, title) VALUES (:path, :title)"));
    insert.bindValue(QStringLiteral(":path"), QStringLiteral("/fake/rating-default-test.mp3"));
    insert.bindValue(QStringLiteral(":title"), QStringLiteral("Rating Default Test"));
    QVERIFY(insert.exec());

    const auto tracks = TrackRepository::searchTracks(QStringLiteral("Rating Default Test"));
    QCOMPARE(tracks.size(), 1);
    QCOMPARE(tracks.first().rating, 0);
}

QTEST_MAIN(MigrationsTest)
#include "MigrationsTest.moc"
