#include "scheduler/AutoDjEngine.h"
#include "scheduler/ClockEngine.h"

#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlayHistoryRepository.h"
#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/SmartPlaylistRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::scheduler;
using namespace radio::db;

class AutoDjEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void topsUpQueueRespectingWatermark();
    void singleFillPassDoesNotStackSameArtistBackToBack();
    void uncategorizedTracksJoinCategoryRotationRatherThanLooping();
    void relaxesNoRepeatWindowRatherThanStarving();
    void emptyLibraryEmitsStarved();
    void activeBlockOverridesCategoryRotation();
    void exhaustedBlockFallsBackToCategoryRotation();
    void activeBlockTargetingSmartPlaylistOverridesCategoryRotation();
    void exhaustedSmartPlaylistBlockFallsBackToCategoryRotation();
    void resetQueueClearsAndRefills();
    void resetQueueForBlockTargetsGivenBlockRegardlessOfWallClock();
    void resetQueueForBlockMinusOneFallsBackToCategoryRotation();
    void remainingModeQueuesEnoughToCoverBlocksTotalDuration();
    void countModeUsesBlocksOwnQueueSizeOverGlobalWatermark();
    void countModeWithZeroQueueSizeInheritsGlobalWatermark();

    void clockDrivenBlockQueuesFromItsClockElements();
    void clockDrivenBlockWithNoClockEngineFallsBackToCategoryRotation();
    void nonClockBlockBehaviorUnchangedWithClockEngineAttached();

private:
    static qint64 insertCategory(const QString& name, int targetRatio);
    static qint64 insertTrack(const QString& path, const QString& artist, qint64 categoryId, qint64 durationMs = 180000);
};

void AutoDjEngineTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationSchedulerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationSchedulerTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void AutoDjEngineTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM play_history"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM rotation_categories"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
}

qint64 AutoDjEngineTest::insertCategory(const QString& name, int targetRatio)
{
    return RotationCategoryRepository::addCategory(name, QStringLiteral("#000000"), targetRatio);
}

qint64 AutoDjEngineTest::insertTrack(const QString& path, const QString& artist, qint64 categoryId, qint64 durationMs)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "INSERT INTO tracks (file_path, title, artist, category_id, duration_ms, missing, play_count) "
        "VALUES (:path, :path, :artist, :cat, :duration_ms, 0, 0)"));
    query.bindValue(QStringLiteral(":path"), path);
    query.bindValue(QStringLiteral(":artist"), artist);
    // Mirror TrackRepository::upsertScannedTrack: categoryId < 0 means
    // "uncategorized", which production code stores as SQL NULL, not the
    // literal sentinel value -- callers matching on "category_id IS NULL"
    // need that to hold in tests too.
    query.bindValue(QStringLiteral(":cat"), categoryId < 0 ? QVariant() : QVariant(categoryId));
    query.bindValue(QStringLiteral(":duration_ms"), durationMs);
    query.exec();
    return query.lastInsertId().toLongLong();
}

void AutoDjEngineTest::topsUpQueueRespectingWatermark()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    for (int i = 0; i < 10; ++i)
        insertTrack(QStringLiteral("/fake/track%1.mp3").arg(i), QStringLiteral("Artist%1").arg(i), category);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(5);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    const int added = engine.topUpQueue();
    QCOMPARE(added, 5);
    QCOMPARE(PlaylistRepository::queueItems().size(), 5);

    // Already at watermark — a second pass should add nothing more.
    QCOMPARE(engine.topUpQueue(), 0);
    QCOMPARE(PlaylistRepository::queueItems().size(), 5);
}

void AutoDjEngineTest::singleFillPassDoesNotStackSameArtistBackToBack()
{
    // Reproduces the real-world bug report: filling a fresh queue from a
    // library that's mostly never been played picked the same artist
    // several times in a row. That's because "least recently played" ties
    // on NULL for every unplayed track, and recentArtists() only excludes
    // artists from PLAY HISTORY -- it has no idea what this very fill pass
    // already put in the queue. A prolific never-played artist can
    // therefore dominate every single pick until the queue's full.
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    for (int i = 0; i < 15; ++i)
        insertTrack(QStringLiteral("/fake/prolific%1.mp3").arg(i), QStringLiteral("Prolific Artist"), category);

    // Distinct-artist tracks get a real (non-NULL) last_played_at so they
    // deterministically sort after the never-played "Prolific Artist" batch
    // -- this is what makes the test deterministic rather than relying on
    // RANDOM() tie-break luck: without the artist-exclusion fix, Prolific
    // Artist's NULL rows always win every single pick.
    for (int i = 0; i < 10; ++i) {
        const qint64 id = insertTrack(
            QStringLiteral("/fake/distinct%1.mp3").arg(i), QStringLiteral("Distinct Artist%1").arg(i), category);
        QSqlQuery backdate(Database::handle());
        backdate.prepare(QStringLiteral("UPDATE tracks SET last_played_at = :ts WHERE id = :id"));
        backdate.bindValue(QStringLiteral(":ts"), QStringLiteral("2020-01-01T00:00:00"));
        backdate.bindValue(QStringLiteral(":id"), id);
        QVERIFY(backdate.exec());
    }

    AutoDjEngine engine;
    engine.setQueueLowWatermark(5);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 5);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 5);

    QSet<QString> artistsSeen;
    for (const auto& item : items) {
        QVERIFY2(!artistsSeen.contains(item.artist), qPrintable(QStringLiteral("artist repeated: %1").arg(item.artist)));
        artistsSeen.insert(item.artist);
    }
    QCOMPARE(PlaylistRepository::queueItems().size(), 5);
}

void AutoDjEngineTest::uncategorizedTracksJoinCategoryRotationRatherThanLooping()
{
    // Reproduces the real-world bug report: a small batch of tracks already
    // has a category assigned (e.g. from an old manifest.csv import), while
    // newer library additions were scanned without ever going through that
    // manifest and so sit with category_id NULL. Category-rotation picks
    // must be able to reach those uncategorized tracks too, not just loop
    // forever within the originally-categorized batch.
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    QVector<qint64> categorizedIds;
    for (int i = 0; i < 3; ++i)
        categorizedIds.append(
            insertTrack(QStringLiteral("/fake/categorized%1.mp3").arg(i), QStringLiteral("Old Artist%1").arg(i), category));

    QVector<qint64> uncategorizedIds;
    for (int i = 0; i < 5; ++i)
        uncategorizedIds.append(
            insertTrack(QStringLiteral("/fake/new%1.mp3").arg(i), QStringLiteral("New Artist%1").arg(i), -1));

    AutoDjEngine engine;
    engine.setQueueLowWatermark(8); // exactly the whole library, categorized + uncategorized
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 8);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 8);

    QSet<qint64> queuedIds;
    for (const auto& item : items)
        queuedIds.insert(item.trackId);
    for (qint64 id : std::as_const(uncategorizedIds))
        QVERIFY(queuedIds.contains(id));
    for (qint64 id : std::as_const(categorizedIds))
        QVERIFY(queuedIds.contains(id));
}

void AutoDjEngineTest::relaxesNoRepeatWindowRatherThanStarving()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    QVector<qint64> trackIds;
    for (int i = 0; i < 3; ++i)
        trackIds.append(insertTrack(QStringLiteral("/fake/recent%1.mp3").arg(i), QStringLiteral("SameArtist"), category));

    // Simulate all 3 tracks having just been played, so a naive no-repeat
    // filter would exclude every track in the library.
    for (qint64 id : std::as_const(trackIds))
        PlayHistoryRepository::recordPlay(id, QStringLiteral("A"));

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(10);
    engine.setNoRepeatArtistWindow(10);

    QSignalSpy starvedSpy(&engine, &AutoDjEngine::starved);

    const int added = engine.topUpQueue();
    QCOMPARE(added, 1);
    QCOMPARE(PlaylistRepository::queueItems().size(), 1);
    QCOMPARE(starvedSpy.count(), 0);
}

void AutoDjEngineTest::emptyLibraryEmitsStarved()
{
    AutoDjEngine engine;
    engine.setQueueLowWatermark(3);

    QSignalSpy starvedSpy(&engine, &AutoDjEngine::starved);
    QCOMPARE(engine.topUpQueue(), 0);
    QCOMPARE(starvedSpy.count(), 1);
    QCOMPARE(PlaylistRepository::queueItems().size(), 0);
}

void AutoDjEngineTest::activeBlockOverridesCategoryRotation()
{
    // A category with its own track exists (so the engine COULD fall back
    // to category-rotation if the block path failed) - the point of this
    // test is confirming it doesn't, while a matching block is active.
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    insertTrack(QStringLiteral("/fake/category-track.mp3"), QStringLiteral("Category Artist"), category);

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Block Playlist"));
    const qint64 playlistTrackId
        = insertTrack(QStringLiteral("/fake/playlist-track.mp3"), QStringLiteral("Playlist Artist"), -1);
    PlaylistRepository::addTrackToPlaylist(playlistId, playlistTrackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On");
    block.daysMask = 0b1111111; // every day
    block.startMinute = 0;
    block.endMinute = 1440; // All Day
    block.playlistId = playlistId;
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, playlistTrackId);
}

void AutoDjEngineTest::exhaustedBlockFallsBackToCategoryRotation()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    const qint64 categoryTrackId
        = insertTrack(QStringLiteral("/fake/fallback-track.mp3"), QStringLiteral("Fallback Artist"), category);

    // A block is active all day every day, but its playlist has no tracks
    // at all - generateNextFromBlock() must return -1 even after relaxing,
    // and generateNext() must fall through to category-rotation rather
    // than leaving the queue starved.
    const qint64 emptyPlaylistId = PlaylistRepository::createPlaylist(QStringLiteral("Empty Playlist"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On But Empty");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.playlistId = emptyPlaylistId;
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, categoryTrackId);
}

void AutoDjEngineTest::activeBlockTargetingSmartPlaylistOverridesCategoryRotation()
{
    // A category with its own track exists (so the engine COULD fall back
    // to category-rotation if the smart-playlist path failed) - the point
    // of this test is confirming it doesn't, while a matching block is active.
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    insertTrack(QStringLiteral("/fake/smart-category-track.mp3"), QStringLiteral("Category Artist"), category);

    const qint64 smartTrackId
        = insertTrack(QStringLiteral("/fake/smart-track.mp3"), QStringLiteral("Smart Artist"), -1);
    QSqlQuery updateGenre(Database::handle());
    updateGenre.prepare(QStringLiteral("UPDATE tracks SET genre = :genre WHERE id = :id"));
    updateGenre.bindValue(QStringLiteral(":genre"), QStringLiteral("Ambient"));
    updateGenre.bindValue(QStringLiteral(":id"), smartTrackId);
    QVERIFY(updateGenre.exec());

    const qint64 smartPlaylistId
        = SmartPlaylistRepository::create(QStringLiteral("Ambient Only"), QStringLiteral(R"({"genre": "Ambient"})"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Smart Playlist Block");
    block.daysMask = 0b1111111; // every day
    block.startMinute = 0;
    block.endMinute = 1440; // All Day
    block.smartPlaylistId = smartPlaylistId;
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, smartTrackId);
}

void AutoDjEngineTest::exhaustedSmartPlaylistBlockFallsBackToCategoryRotation()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    const qint64 categoryTrackId
        = insertTrack(QStringLiteral("/fake/smart-fallback-track.mp3"), QStringLiteral("Fallback Artist"), category);

    // A smart playlist whose filter matches nothing in the library.
    const qint64 smartPlaylistId
        = SmartPlaylistRepository::create(QStringLiteral("Nothing Matches"), QStringLiteral(R"({"genre": "Nonexistent"})"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On But Empty Smart Playlist");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.smartPlaylistId = smartPlaylistId;
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, categoryTrackId);
}

void AutoDjEngineTest::resetQueueClearsAndRefills()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    for (int i = 0; i < 15; ++i)
        insertTrack(QStringLiteral("/fake/reset%1.mp3").arg(i), QStringLiteral("Artist%1").arg(i), category);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(5);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 5);
    const auto beforeReset = PlaylistRepository::queueItems();
    QCOMPARE(beforeReset.size(), 5);

    // Manually stuff in one extra queue entry (simulating something queued
    // before/during a manual interruption) to prove resetQueue() actually
    // clears first, rather than just topping up on top of what's there.
    PlaylistRepository::appendToQueue(beforeReset.first().trackId, QStringLiteral("manual"));
    QCOMPARE(PlaylistRepository::queueItems().size(), 6);

    engine.resetQueue();

    QCOMPARE(PlaylistRepository::queueItems().size(), 5);
}

void AutoDjEngineTest::resetQueueForBlockTargetsGivenBlockRegardlessOfWallClock()
{
    // A category with its own track exists, so a normal wall-clock-resolved
    // topUpQueue() would fall back to it (the block below never matches
    // "now" - wrong day entirely).
    // Higher target_ratio than the "other" category below guarantees Music
    // wins the very first weighted-rotation pick deterministically (ties are
    // broken by allCategories()'s alphabetical order, not id, so relying on
    // insertion order alone would be fragile here).
    const qint64 category = insertCategory(QStringLiteral("Music"), 2);
    const qint64 categoryTrackId
        = insertTrack(QStringLiteral("/fake/category-track.mp3"), QStringLiteral("Category Artist"), category);

    // Give the playlist-only track a distinct category (rather than leaving
    // it uncategorized) so it's cleanly excluded from Music's rotation pool
    // below -- an uncategorized track would now legitimately be eligible
    // there too (that's the whole point of the category-rotation fix), which
    // would make this sanity check's "only categoryTrackId gets picked"
    // assumption nondeterministic.
    const qint64 otherCategory = insertCategory(QStringLiteral("Jingles"), 1);
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Next Block Playlist"));
    const qint64 playlistTrackId
        = insertTrack(QStringLiteral("/fake/next-block-track.mp3"), QStringLiteral("Next Block Artist"), otherCategory);
    PlaylistRepository::addTrackToPlaylist(playlistId, playlistTrackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Never Active Right Now");
    block.daysMask = 0; // matches no day at all - resolveActiveBlockId(now) can never return this block's id
    block.startMinute = 0;
    block.endMinute = 1440;
    block.playlistId = playlistId;
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    // Sanity check: a normal top-up ignores this block (it's never "active")
    // and falls back to category-rotation instead.
    QCOMPARE(engine.topUpQueue(), 1);
    QCOMPARE(PlaylistRepository::queueItems().first().trackId, categoryTrackId);
    PlaylistRepository::clearQueue();

    engine.resetQueueForBlock(blockId);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, playlistTrackId);
}

void AutoDjEngineTest::resetQueueForBlockMinusOneFallsBackToCategoryRotation()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    const qint64 categoryTrackId
        = insertTrack(QStringLiteral("/fake/rotation-track.mp3"), QStringLiteral("Rotation Artist"), category);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    engine.resetQueueForBlock(-1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, categoryTrackId);
}

void AutoDjEngineTest::remainingModeQueuesEnoughToCoverBlocksTotalDuration()
{
    // 8 one-minute tracks available - a 5-minute block should queue exactly
    // 5 of them (5 x 60000ms = 300000ms), not all 8.
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Remaining Mode Playlist"));
    for (int i = 0; i < 8; ++i) {
        const qint64 trackId = insertTrack(
            QStringLiteral("/fake/remaining%1.mp3").arg(i), QStringLiteral("Artist%1").arg(i), -1, 60000);
        PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));
    }

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Remaining Mode Block");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 5; // 5-minute total duration
    block.playlistId = playlistId;
    block.queueMode = QStringLiteral("remaining");
    const qint64 blockId = ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    // resetQueueForBlock() targets a specific block directly - this test's
    // whole point (does the duration math work) doesn't need to depend on
    // real wall-clock "now" the way the periodic topUpQueue() path would.
    engine.resetQueueForBlock(blockId);

    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 5);
    qint64 totalDuration = 0;
    for (const auto& item : items)
        totalDuration += item.durationMs;
    QCOMPARE(totalDuration, qint64(300000));
}

void AutoDjEngineTest::countModeUsesBlocksOwnQueueSizeOverGlobalWatermark()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Count Override Playlist"));
    for (int i = 0; i < 10; ++i) {
        const qint64 trackId
            = insertTrack(QStringLiteral("/fake/countoverride%1.mp3").arg(i), QStringLiteral("Artist%1").arg(i), -1);
        PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));
    }

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Count Override Block");
    block.daysMask = 0b1111111; // every day
    block.startMinute = 0;
    block.endMinute = 1440; // All Day - matches regardless of when this test runs
    block.playlistId = playlistId;
    block.queueSize = 3; // deliberately different from the global watermark below
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(10); // must be ignored - the block's own queueSize wins
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 3);
    QCOMPARE(PlaylistRepository::queueItems().size(), 3);
}

void AutoDjEngineTest::countModeWithZeroQueueSizeInheritsGlobalWatermark()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Inherit Playlist"));
    for (int i = 0; i < 10; ++i) {
        const qint64 trackId
            = insertTrack(QStringLiteral("/fake/inherit%1.mp3").arg(i), QStringLiteral("Artist%1").arg(i), -1);
        PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));
    }

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Inherit Block");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.playlistId = playlistId;
    // queueSize left at its default (0) - deliberately not set, proving the
    // sentinel keeps existing setQueueLowWatermark()-driven tests intact.
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine;
    engine.setQueueLowWatermark(4);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 4);
}

void AutoDjEngineTest::clockDrivenBlockQueuesFromItsClockElements()
{
    // A category with its own track exists (so the engine COULD fall back
    // to category-rotation if the clock path failed) -- the point of this
    // test is confirming it doesn't, while a clock-driven block is active.
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    insertTrack(QStringLiteral("/fake/category-track.mp3"), QStringLiteral("Category Artist"), category);

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Clock Playlist"));
    const qint64 playlistTrackId
        = insertTrack(QStringLiteral("/fake/clock-track.mp3"), QStringLiteral("Clock Artist"), -1);
    PlaylistRepository::addTrackToPlaylist(playlistId, playlistTrackId, QStringLiteral("manual"));

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.playlistId = playlistId;
    element.itemCount = 1;
    ClockRepository::createElement(element);

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Clock Block");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);

    ClockEngine clockEngine;
    AutoDjEngine engine;
    engine.setClockEngine(&clockEngine);
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, playlistTrackId);
    QVERIFY(items.first().source.startsWith(QStringLiteral("clock:")));
}

void AutoDjEngineTest::clockDrivenBlockWithNoClockEngineFallsBackToCategoryRotation()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    const qint64 categoryTrackId
        = insertTrack(QStringLiteral("/fake/fallback-track.mp3"), QStringLiteral("Fallback Artist"), category);

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    // Deliberately no elements, and no setClockEngine() call below either --
    // block.clockId >= 0 but AutoDjEngine has no ClockEngine to consult.

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Clock Block");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.clockId = clockId;
    ScheduleBlockRepository::createBlock(block);

    AutoDjEngine engine; // m_clockEngine stays nullptr
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, categoryTrackId);
}

void AutoDjEngineTest::nonClockBlockBehaviorUnchangedWithClockEngineAttached()
{
    const qint64 category = insertCategory(QStringLiteral("Music"), 1);
    insertTrack(QStringLiteral("/fake/category-track.mp3"), QStringLiteral("Category Artist"), category);

    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Block Playlist"));
    const qint64 playlistTrackId
        = insertTrack(QStringLiteral("/fake/playlist-track.mp3"), QStringLiteral("Playlist Artist"), -1);
    PlaylistRepository::addTrackToPlaylist(playlistId, playlistTrackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On");
    block.daysMask = 0b1111111;
    block.startMinute = 0;
    block.endMinute = 1440;
    block.playlistId = playlistId; // NOT clock-driven (clockId stays -1)
    ScheduleBlockRepository::createBlock(block);

    ClockEngine clockEngine;
    AutoDjEngine engine;
    engine.setClockEngine(&clockEngine); // attached, but must never be consulted for a non-clock block
    engine.setQueueLowWatermark(1);
    engine.setNoRepeatTrackWindow(0);
    engine.setNoRepeatArtistWindow(0);

    QCOMPARE(engine.topUpQueue(), 1);
    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().trackId, playlistTrackId);
}

QTEST_MAIN(AutoDjEngineTest)
#include "AutoDjEngineTest.moc"
