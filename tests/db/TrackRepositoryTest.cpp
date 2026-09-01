#include "db/Database.h"
#include "db/PlayHistoryRepository.h"
#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::db;

class TrackRepositoryTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void upsertInsertsNewTrack();
    void upsertPreservesUserEditedFieldsAcrossRescan();
    void setRatingRoundTrips();
    void setRatingClampsToValidRange();
    void trackByIdReturnsTheMatchingTrack();
    void trackByIdReturnsSentinelForUnknownId();

    void candidatesForPlaylistFiltersToPlaylistMembership();
    void candidatesForPlaylistSortsByRatingDescending();
    void candidatesForPlaylistSortsByPlayCountAscending();
    void candidatesForPlaylistExcludesGivenTrackIdsAndArtists();

    void upsertAlwaysOverwritesYear();
    void upsertPreservesEnergyAndBpmWhenRescanLeavesThemUnset();
    void upsertOnlySetsCategoryWhileUncategorized();
    void candidatesForSmartPlaylistFiltersByJson();
    void candidatesForSmartPlaylistExcludesGivenTrackIdsAndArtists();

    void syncMissingForRootDeletesFilesNotRediscovered();
    void syncMissingForRootIgnoresFilesOutsideRoot();
    void syncMissingForRootLeavesRediscoveredFilesAlone();
    void syncMissingForRootDeletesDependentPlaylistItemsAndHistory();

    void tracksNeedingReplayGainAnalysisExcludesAlreadyTaggedTracks();
    void updateReplayGainSetsHasReplayGainAndValues();

    void tracksNeedingBpmAnalysisExcludesAlreadyKnownTracks();
    void updateBpmSetsValue();

    void searchTracksMatchesByTitlePrefix();
    void searchTracksMatchesByArtistAlbumGenrePrefix();
    void searchTracksCombinesMultipleTermsAcrossColumns();
    void searchTracksExcludesMissingTracks();
    void searchTracksIndexStaysInSyncAfterRescanUpdate();

    void candidatesForCategoryExcludesGivenTrackIdsAndArtists();
    void candidatesForCategoryLimitsToTenWhenMoreCandidatesExist();
    void candidatesForPlaylistWithNoRepeatMetricReturnsEveryMemberNotJustTen();

private:
    static TrackRecord findByPath(const QString& path);
};

void TrackRepositoryTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationTrackRepositoryTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationTrackRepositoryTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void TrackRepositoryTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM rotation_categories"));
}

TrackRecord TrackRepositoryTest::findByPath(const QString& path)
{
    const auto tracks = TrackRepository::searchTracks(QString());
    for (const auto& track : tracks) {
        if (track.filePath == path)
            return track;
    }
    return {};
}

void TrackRepositoryTest::upsertInsertsNewTrack()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/upsert-new.mp3");
    record.title = QStringLiteral("New Track");
    record.artist = QStringLiteral("New Artist");
    record.durationMs = 123456;

    const qint64 id = TrackRepository::upsertScannedTrack(record);
    QVERIFY(id >= 0);

    const TrackRecord stored = findByPath(record.filePath);
    QCOMPARE(stored.title, QStringLiteral("New Track"));
    QCOMPARE(stored.artist, QStringLiteral("New Artist"));
    QCOMPARE(stored.durationMs, qint64(123456));
    QCOMPARE(stored.rating, 0);
}

void TrackRepositoryTest::upsertPreservesUserEditedFieldsAcrossRescan()
{
    TrackRecord initial;
    initial.filePath = QStringLiteral("/fake/upsert-rescan.mp3");
    initial.title = QStringLiteral("Scanned Title");
    initial.artist = QStringLiteral("Scanned Artist");
    const qint64 id = TrackRepository::upsertScannedTrack(initial);

    QVERIFY(TrackRepository::updateEditableFields(
        id, QStringLiteral("User Title"), QStringLiteral("User Artist"), QString(), QString(), -1));

    // A rescan with different scanned metadata must not clobber the user's edits.
    TrackRecord rescanned;
    rescanned.filePath = initial.filePath;
    rescanned.title = QStringLiteral("Different Scanned Title");
    rescanned.artist = QStringLiteral("Different Scanned Artist");
    rescanned.durationMs = 99999;
    TrackRepository::upsertScannedTrack(rescanned);

    const TrackRecord stored = findByPath(initial.filePath);
    QCOMPARE(stored.title, QStringLiteral("User Title"));
    QCOMPARE(stored.artist, QStringLiteral("User Artist"));
    QCOMPARE(stored.durationMs, qint64(99999)); // scan-derived fields DO update
}

void TrackRepositoryTest::setRatingRoundTrips()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/rating-roundtrip.mp3");
    record.title = QStringLiteral("Rating Track");
    const qint64 id = TrackRepository::upsertScannedTrack(record);

    QVERIFY(TrackRepository::setRating(id, 4));
    QCOMPARE(findByPath(record.filePath).rating, 4);

    QVERIFY(TrackRepository::setRating(id, 0));
    QCOMPARE(findByPath(record.filePath).rating, 0);
}

void TrackRepositoryTest::setRatingClampsToValidRange()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/rating-clamp.mp3");
    record.title = QStringLiteral("Clamp Track");
    const qint64 id = TrackRepository::upsertScannedTrack(record);

    QVERIFY(TrackRepository::setRating(id, 99));
    QCOMPARE(findByPath(record.filePath).rating, 5);

    QVERIFY(TrackRepository::setRating(id, -99));
    QCOMPARE(findByPath(record.filePath).rating, 0);
}

void TrackRepositoryTest::trackByIdReturnsTheMatchingTrack()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/track-by-id.mp3");
    record.title = QStringLiteral("Findable Track");
    record.artist = QStringLiteral("Findable Artist");
    const qint64 id = TrackRepository::upsertScannedTrack(record);
    TrackRepository::setRating(id, 4);

    const TrackRecord found = TrackRepository::trackById(id);
    QCOMPARE(found.id, id);
    QCOMPARE(found.title, QStringLiteral("Findable Track"));
    QCOMPARE(found.artist, QStringLiteral("Findable Artist"));
    QCOMPARE(found.rating, 4);
}

void TrackRepositoryTest::trackByIdReturnsSentinelForUnknownId()
{
    QCOMPARE(TrackRepository::trackById(987654).id, qint64(-1));
}

void TrackRepositoryTest::candidatesForPlaylistFiltersToPlaylistMembership()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Members Only"));

    TrackRecord inPlaylist;
    inPlaylist.filePath = QStringLiteral("/fake/in-playlist.mp3");
    inPlaylist.title = QStringLiteral("In Playlist");
    const qint64 inId = TrackRepository::upsertScannedTrack(inPlaylist);
    PlaylistRepository::addTrackToPlaylist(playlistId, inId, QStringLiteral("manual"));

    TrackRecord notInPlaylist;
    notInPlaylist.filePath = QStringLiteral("/fake/not-in-playlist.mp3");
    notInPlaylist.title = QStringLiteral("Not In Playlist");
    TrackRepository::upsertScannedTrack(notInPlaylist);

    const auto candidates
        = TrackRepository::candidatesForPlaylist(playlistId, SelectionMetric::None, SortDirection::Descending, {}, {});
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, inId);
}

void TrackRepositoryTest::candidatesForPlaylistSortsByRatingDescending()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Rated"));

    TrackRecord low;
    low.filePath = QStringLiteral("/fake/low-rated.mp3");
    low.title = QStringLiteral("Low");
    const qint64 lowId = TrackRepository::upsertScannedTrack(low);
    TrackRepository::setRating(lowId, 2);
    PlaylistRepository::addTrackToPlaylist(playlistId, lowId, QStringLiteral("manual"));

    TrackRecord high;
    high.filePath = QStringLiteral("/fake/high-rated.mp3");
    high.title = QStringLiteral("High");
    const qint64 highId = TrackRepository::upsertScannedTrack(high);
    TrackRepository::setRating(highId, 5);
    PlaylistRepository::addTrackToPlaylist(playlistId, highId, QStringLiteral("manual"));

    const auto candidates
        = TrackRepository::candidatesForPlaylist(playlistId, SelectionMetric::Rating, SortDirection::Descending, {}, {});
    QCOMPARE(candidates.size(), 2);
    QCOMPARE(candidates.first().id, highId);
    QCOMPARE(candidates.last().id, lowId);
}

void TrackRepositoryTest::candidatesForPlaylistSortsByPlayCountAscending()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("PlayCounts"));

    QSqlQuery insertA(Database::handle());
    insertA.prepare(QStringLiteral("INSERT INTO tracks (file_path, title, play_count) VALUES (:path, :title, :count)"));
    insertA.bindValue(QStringLiteral(":path"), QStringLiteral("/fake/played-often.mp3"));
    insertA.bindValue(QStringLiteral(":title"), QStringLiteral("Played Often"));
    insertA.bindValue(QStringLiteral(":count"), 50);
    insertA.exec();
    const qint64 oftenId = insertA.lastInsertId().toLongLong();
    PlaylistRepository::addTrackToPlaylist(playlistId, oftenId, QStringLiteral("manual"));

    QSqlQuery insertB(Database::handle());
    insertB.prepare(QStringLiteral("INSERT INTO tracks (file_path, title, play_count) VALUES (:path, :title, :count)"));
    insertB.bindValue(QStringLiteral(":path"), QStringLiteral("/fake/played-rarely.mp3"));
    insertB.bindValue(QStringLiteral(":title"), QStringLiteral("Played Rarely"));
    insertB.bindValue(QStringLiteral(":count"), 1);
    insertB.exec();
    const qint64 rarelyId = insertB.lastInsertId().toLongLong();
    PlaylistRepository::addTrackToPlaylist(playlistId, rarelyId, QStringLiteral("manual"));

    const auto candidates = TrackRepository::candidatesForPlaylist(
        playlistId, SelectionMetric::PlayCount, SortDirection::Ascending, {}, {});
    QCOMPARE(candidates.size(), 2);
    QCOMPARE(candidates.first().id, rarelyId);
    QCOMPARE(candidates.last().id, oftenId);
}

void TrackRepositoryTest::candidatesForPlaylistExcludesGivenTrackIdsAndArtists()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Excludable"));

    TrackRecord keep;
    keep.filePath = QStringLiteral("/fake/keep.mp3");
    keep.title = QStringLiteral("Keep");
    keep.artist = QStringLiteral("Keep Artist");
    const qint64 keepId = TrackRepository::upsertScannedTrack(keep);
    PlaylistRepository::addTrackToPlaylist(playlistId, keepId, QStringLiteral("manual"));

    TrackRecord excludedById;
    excludedById.filePath = QStringLiteral("/fake/excluded-by-id.mp3");
    excludedById.title = QStringLiteral("Excluded By Id");
    const qint64 excludedByIdId = TrackRepository::upsertScannedTrack(excludedById);
    PlaylistRepository::addTrackToPlaylist(playlistId, excludedByIdId, QStringLiteral("manual"));

    TrackRecord excludedByArtist;
    excludedByArtist.filePath = QStringLiteral("/fake/excluded-by-artist.mp3");
    excludedByArtist.title = QStringLiteral("Excluded By Artist");
    excludedByArtist.artist = QStringLiteral("Banned Artist");
    const qint64 excludedByArtistId = TrackRepository::upsertScannedTrack(excludedByArtist);
    PlaylistRepository::addTrackToPlaylist(playlistId, excludedByArtistId, QStringLiteral("manual"));

    const auto candidates = TrackRepository::candidatesForPlaylist(playlistId, SelectionMetric::None,
        SortDirection::Descending, { excludedByIdId }, { QStringLiteral("Banned Artist") });
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, keepId);
}

void TrackRepositoryTest::upsertAlwaysOverwritesYear()
{
    TrackRecord initial;
    initial.filePath = QStringLiteral("/fake/year-overwrite.mp3");
    initial.title = QStringLiteral("Year Track");
    initial.year = 1999;
    const qint64 id = TrackRepository::upsertScannedTrack(initial);
    QCOMPARE(TrackRepository::trackById(id).year, 1999);

    TrackRecord rescanned;
    rescanned.filePath = initial.filePath;
    rescanned.title = QStringLiteral("Year Track");
    rescanned.year = 2005;
    TrackRepository::upsertScannedTrack(rescanned);

    QCOMPARE(TrackRepository::trackById(id).year, 2005);
}

void TrackRepositoryTest::upsertPreservesEnergyAndBpmWhenRescanLeavesThemUnset()
{
    TrackRecord initial;
    initial.filePath = QStringLiteral("/fake/energy-bpm-preserve.mp3");
    initial.title = QStringLiteral("Energy Track");
    initial.energy = 0.6;
    initial.bpm = 128;
    const qint64 id = TrackRepository::upsertScannedTrack(initial);
    QCOMPARE(TrackRepository::trackById(id).energy, 0.6);
    QCOMPARE(TrackRepository::trackById(id).bpm, 128.0);

    // A plain rescan (energy/bpm left at their -1 "unset" default) must not
    // wipe the manifest-derived values.
    TrackRecord rescanned;
    rescanned.filePath = initial.filePath;
    rescanned.title = QStringLiteral("Energy Track");
    TrackRepository::upsertScannedTrack(rescanned);

    QCOMPARE(TrackRepository::trackById(id).energy, 0.6);
    QCOMPARE(TrackRepository::trackById(id).bpm, 128.0);

    // An explicit new value (e.g. a fresh manifest merge) does overwrite.
    TrackRecord reMerged;
    reMerged.filePath = initial.filePath;
    reMerged.title = QStringLiteral("Energy Track");
    reMerged.energy = 0.9;
    reMerged.bpm = 140;
    TrackRepository::upsertScannedTrack(reMerged);

    QCOMPARE(TrackRepository::trackById(id).energy, 0.9);
    QCOMPARE(TrackRepository::trackById(id).bpm, 140.0);
}

void TrackRepositoryTest::upsertOnlySetsCategoryWhileUncategorized()
{
    TrackRecord initial;
    initial.filePath = QStringLiteral("/fake/category-preserve.mp3");
    initial.title = QStringLiteral("Category Track");
    initial.categoryId = 5;
    const qint64 id = TrackRepository::upsertScannedTrack(initial);
    QCOMPARE(TrackRepository::trackById(id).categoryId, qint64(5));

    // A rescan proposing a different category must not clobber a track
    // that's already categorized (mirrors the title/artist preserve rule).
    TrackRecord rescanned;
    rescanned.filePath = initial.filePath;
    rescanned.title = QStringLiteral("Category Track");
    rescanned.categoryId = 9;
    TrackRepository::upsertScannedTrack(rescanned);
    QCOMPARE(TrackRepository::trackById(id).categoryId, qint64(5));

    // But an uncategorized track does get the proposed category filled in.
    TrackRecord uncategorized;
    uncategorized.filePath = QStringLiteral("/fake/category-fill-in.mp3");
    uncategorized.title = QStringLiteral("Uncategorized Track");
    const qint64 uncategorizedId = TrackRepository::upsertScannedTrack(uncategorized);
    QCOMPARE(TrackRepository::trackById(uncategorizedId).categoryId, qint64(-1));

    TrackRecord fillIn;
    fillIn.filePath = uncategorized.filePath;
    fillIn.title = QStringLiteral("Uncategorized Track");
    fillIn.categoryId = 3;
    TrackRepository::upsertScannedTrack(fillIn);
    QCOMPARE(TrackRepository::trackById(uncategorizedId).categoryId, qint64(3));
}

void TrackRepositoryTest::candidatesForSmartPlaylistFiltersByJson()
{
    const qint64 smartPlaylistId = SmartPlaylistRepository::create(
        QStringLiteral("High Energy"), QStringLiteral(R"({"energyMin": 0.7})"));

    TrackRecord highEnergy;
    highEnergy.filePath = QStringLiteral("/fake/high-energy.mp3");
    highEnergy.title = QStringLiteral("High");
    highEnergy.energy = 0.9;
    const qint64 highId = TrackRepository::upsertScannedTrack(highEnergy);

    TrackRecord lowEnergy;
    lowEnergy.filePath = QStringLiteral("/fake/low-energy.mp3");
    lowEnergy.title = QStringLiteral("Low");
    lowEnergy.energy = 0.2;
    TrackRepository::upsertScannedTrack(lowEnergy);

    const auto candidates = TrackRepository::candidatesForSmartPlaylist(smartPlaylistId, {}, {});
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, highId);
}

void TrackRepositoryTest::candidatesForSmartPlaylistExcludesGivenTrackIdsAndArtists()
{
    const qint64 smartPlaylistId = SmartPlaylistRepository::create(QStringLiteral("Everything"), QStringLiteral("{}"));

    TrackRecord keep;
    keep.filePath = QStringLiteral("/fake/smart-keep.mp3");
    keep.title = QStringLiteral("Keep");
    keep.artist = QStringLiteral("Keep Artist");
    const qint64 keepId = TrackRepository::upsertScannedTrack(keep);

    TrackRecord excludedById;
    excludedById.filePath = QStringLiteral("/fake/smart-excluded-by-id.mp3");
    excludedById.title = QStringLiteral("Excluded By Id");
    const qint64 excludedByIdId = TrackRepository::upsertScannedTrack(excludedById);

    TrackRecord excludedByArtist;
    excludedByArtist.filePath = QStringLiteral("/fake/smart-excluded-by-artist.mp3");
    excludedByArtist.title = QStringLiteral("Excluded By Artist");
    excludedByArtist.artist = QStringLiteral("Banned Artist");
    TrackRepository::upsertScannedTrack(excludedByArtist);

    const auto candidates = TrackRepository::candidatesForSmartPlaylist(
        smartPlaylistId, { excludedByIdId }, { QStringLiteral("Banned Artist") });
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, keepId);
}

void TrackRepositoryTest::syncMissingForRootDeletesFilesNotRediscovered()
{
    TrackRecord gone;
    gone.filePath = QStringLiteral("/fake/root/Artist/Gone Track.mp3");
    gone.title = QStringLiteral("Gone Track");
    const qint64 goneId = TrackRepository::upsertScannedTrack(gone);

    TrackRecord stillThere;
    stillThere.filePath = QStringLiteral("/fake/root/Artist/Still There.mp3");
    stillThere.title = QStringLiteral("Still There");
    const qint64 stillThereId = TrackRepository::upsertScannedTrack(stillThere);

    // A rescan is a full resync against what's actually on disk right now,
    // not a soft flag kept forever -- a track not rediscovered is removed
    // outright.
    const int removed = TrackRepository::syncMissingForRoot(
        QStringLiteral("/fake/root"), { stillThere.filePath });
    QCOMPARE(removed, 1);

    QCOMPARE(TrackRepository::trackById(goneId).id, -1); // gone -- no such row anymore
    QCOMPARE(TrackRepository::trackById(stillThereId).id, stillThereId);
}

void TrackRepositoryTest::syncMissingForRootIgnoresFilesOutsideRoot()
{
    TrackRecord outside;
    outside.filePath = QStringLiteral("/fake/other-root/Artist/Untouched.mp3");
    outside.title = QStringLiteral("Untouched");
    const qint64 outsideId = TrackRepository::upsertScannedTrack(outside);

    const int removed = TrackRepository::syncMissingForRoot(QStringLiteral("/fake/root"), {});
    QCOMPARE(removed, 0);
    QCOMPARE(TrackRepository::trackById(outsideId).id, outsideId);
}

void TrackRepositoryTest::syncMissingForRootLeavesRediscoveredFilesAlone()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/root/Artist/Rediscovered.mp3");
    track.title = QStringLiteral("Rediscovered");
    const qint64 id = TrackRepository::upsertScannedTrack(track);

    // Re-upserting it (as ImportDialog::runRescan does for every file it
    // finds, before syncMissingForRoot ever runs) must keep it out of the
    // not-rediscovered set entirely -- it's still there, so no deletion.
    TrackRepository::upsertScannedTrack(track);
    TrackRepository::syncMissingForRoot(QStringLiteral("/fake/root"), { track.filePath });
    QCOMPARE(TrackRepository::trackById(id).id, id);
}

void TrackRepositoryTest::syncMissingForRootDeletesDependentPlaylistItemsAndHistory()
{
    TrackRecord gone;
    gone.filePath = QStringLiteral("/fake/root/Artist/Gone With Deps.mp3");
    gone.title = QStringLiteral("Gone With Deps");
    const qint64 goneId = TrackRepository::upsertScannedTrack(gone);

    QVERIFY(PlaylistRepository::appendToQueue(goneId, QStringLiteral("library")));
    PlayHistoryRepository::recordPlay(goneId, QStringLiteral("A"));
    QVERIFY(PlayHistoryRepository::recentTrackIds(10).contains(goneId));

    const int removed = TrackRepository::syncMissingForRoot(QStringLiteral("/fake/root"), {});
    QCOMPARE(removed, 1);

    // Without SQLite's foreign-key enforcement turned on, the schema's
    // "ON DELETE CASCADE"/plain REFERENCES on these tables' track_id columns
    // is purely declarative -- syncMissingForRoot() must clean these up
    // itself or they'd be left dangling on a removed track id.
    for (const auto& item : PlaylistRepository::queueItems())
        QVERIFY(item.trackId != goneId);
    QVERIFY(!PlayHistoryRepository::recentTrackIds(10).contains(goneId));
}

void TrackRepositoryTest::tracksNeedingReplayGainAnalysisExcludesAlreadyTaggedTracks()
{
    TrackRecord untagged;
    untagged.filePath = QStringLiteral("/fake/replaygain-untagged.mp3");
    untagged.title = QStringLiteral("Untagged");
    const qint64 untaggedId = TrackRepository::upsertScannedTrack(untagged);

    TrackRecord tagged;
    tagged.filePath = QStringLiteral("/fake/replaygain-tagged.mp3");
    tagged.title = QStringLiteral("Tagged");
    tagged.hasReplayGain = true;
    tagged.replayGainDb = -6.5;
    tagged.replayGainPeak = 0.75;
    TrackRepository::upsertScannedTrack(tagged);

    const auto candidates = TrackRepository::tracksNeedingReplayGainAnalysis();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, untaggedId);
}

void TrackRepositoryTest::updateReplayGainSetsHasReplayGainAndValues()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/replaygain-update.mp3");
    record.title = QStringLiteral("Update Track");
    const qint64 id = TrackRepository::upsertScannedTrack(record);
    QVERIFY(!TrackRepository::trackById(id).hasReplayGain);

    QVERIFY(TrackRepository::updateReplayGain(id, -4.25, 0.891));

    const TrackRecord stored = TrackRepository::trackById(id);
    QVERIFY(stored.hasReplayGain);
    QCOMPARE(stored.replayGainDb, -4.25);
    QCOMPARE(stored.replayGainPeak, 0.891);
}

void TrackRepositoryTest::tracksNeedingBpmAnalysisExcludesAlreadyKnownTracks()
{
    TrackRecord unknown;
    unknown.filePath = QStringLiteral("/fake/bpm-unknown.mp3");
    unknown.title = QStringLiteral("Unknown Bpm");
    const qint64 unknownId = TrackRepository::upsertScannedTrack(unknown);

    TrackRecord known;
    known.filePath = QStringLiteral("/fake/bpm-known.mp3");
    known.title = QStringLiteral("Known Bpm");
    known.bpm = 128.0;
    TrackRepository::upsertScannedTrack(known);

    const auto candidates = TrackRepository::tracksNeedingBpmAnalysis();
    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, unknownId);
}

void TrackRepositoryTest::updateBpmSetsValue()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/bpm-update.mp3");
    record.title = QStringLiteral("Update Bpm Track");
    const qint64 id = TrackRepository::upsertScannedTrack(record);
    QCOMPARE(TrackRepository::trackById(id).bpm, -1.0);

    QVERIFY(TrackRepository::updateBpm(id, 96.0));

    QCOMPARE(TrackRepository::trackById(id).bpm, 96.0);
}

void TrackRepositoryTest::searchTracksMatchesByTitlePrefix()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/search-title.mp3");
    record.title = QStringLiteral("Bohemian Rhapsody");
    record.artist = QStringLiteral("Queen");
    TrackRepository::upsertScannedTrack(record);

    const auto matches = TrackRepository::searchTracks(QStringLiteral("bohe"));
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().title, QStringLiteral("Bohemian Rhapsody"));

    QVERIFY(TrackRepository::searchTracks(QStringLiteral("rhap")).size() == 1); // mid-title word, still a prefix match
    QVERIFY(TrackRepository::searchTracks(QStringLiteral("zzz-no-match")).isEmpty());
}

void TrackRepositoryTest::searchTracksMatchesByArtistAlbumGenrePrefix()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/search-artist.mp3");
    record.title = QStringLiteral("Some Song");
    record.artist = QStringLiteral("Queen");
    record.album = QStringLiteral("A Night at the Opera");
    record.genre = QStringLiteral("Rock");
    TrackRepository::upsertScannedTrack(record);

    QCOMPARE(TrackRepository::searchTracks(QStringLiteral("quee")).size(), 1);
    QCOMPARE(TrackRepository::searchTracks(QStringLiteral("oper")).size(), 1);
    QCOMPARE(TrackRepository::searchTracks(QStringLiteral("roc")).size(), 1);
}

void TrackRepositoryTest::searchTracksCombinesMultipleTermsAcrossColumns()
{
    TrackRecord match;
    match.filePath = QStringLiteral("/fake/search-multi-match.mp3");
    match.title = QStringLiteral("Bohemian Rhapsody");
    match.artist = QStringLiteral("Queen");
    TrackRepository::upsertScannedTrack(match);

    TrackRecord decoy;
    decoy.filePath = QStringLiteral("/fake/search-multi-decoy.mp3");
    decoy.title = QStringLiteral("Bohemian Like You");
    decoy.artist = QStringLiteral("Dandy Warhols");
    TrackRepository::upsertScannedTrack(decoy);

    // Both terms must be found (each anywhere across title/artist/album/genre)
    // -- "queen" alone would also match the decoy's artist... no, it
    // wouldn't (the decoy's artist is Dandy Warhols) -- this specifically
    // proves the AND-across-terms/across-columns behavior a single-column
    // LIKE query could never express.
    const auto matches = TrackRepository::searchTracks(QStringLiteral("bohemian queen"));
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.first().title, QStringLiteral("Bohemian Rhapsody"));
}

void TrackRepositoryTest::searchTracksExcludesMissingTracks()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/search-missing.mp3");
    record.title = QStringLiteral("Ghost Track");
    TrackRepository::upsertScannedTrack(record);
    TrackRepository::syncMissingForRoot(QStringLiteral("/fake"), {}); // flags it missing

    QVERIFY(TrackRepository::searchTracks(QStringLiteral("ghost")).isEmpty());
}

void TrackRepositoryTest::searchTracksIndexStaysInSyncAfterRescanUpdate()
{
    TrackRecord record;
    record.filePath = QStringLiteral("/fake/search-resync.mp3");
    record.title = QStringLiteral("Original Title");
    const qint64 id = TrackRepository::upsertScannedTrack(record);
    QCOMPARE(TrackRepository::searchTracks(QStringLiteral("original")).size(), 1);

    // Direct UPDATE (not upsertScannedTrack -- its rescan path deliberately
    // preserves an already-non-empty title, so it can never exercise this)
    // to prove the tracks_fts_au AFTER UPDATE trigger keeps the index
    // current for ANY update to the row, not just tracks_fts_ai on first
    // insert.
    QSqlQuery update(Database::handle());
    update.prepare(QStringLiteral("UPDATE tracks SET title = :title WHERE id = :id"));
    update.bindValue(QStringLiteral(":title"), QStringLiteral("Rescanned Title"));
    update.bindValue(QStringLiteral(":id"), id);
    QVERIFY(update.exec());

    QVERIFY(TrackRepository::searchTracks(QStringLiteral("rescan")).size() == 1);
    QVERIFY(TrackRepository::searchTracks(QStringLiteral("original")).isEmpty());
}

void TrackRepositoryTest::candidatesForCategoryExcludesGivenTrackIdsAndArtists()
{
    const qint64 categoryId = RotationCategoryRepository::addCategory(QStringLiteral("News"), QString(), 1);

    TrackRecord kept;
    kept.filePath = QStringLiteral("/fake/category-kept.mp3");
    kept.artist = QStringLiteral("Kept Artist");
    kept.categoryId = categoryId;
    const qint64 keptId = TrackRepository::upsertScannedTrack(kept);

    TrackRecord excludedById;
    excludedById.filePath = QStringLiteral("/fake/category-excluded-id.mp3");
    excludedById.artist = QStringLiteral("Some Other Artist");
    excludedById.categoryId = categoryId;
    const qint64 excludedByIdId = TrackRepository::upsertScannedTrack(excludedById);

    TrackRecord excludedByArtist;
    excludedByArtist.filePath = QStringLiteral("/fake/category-excluded-artist.mp3");
    excludedByArtist.artist = QStringLiteral("Excluded Artist");
    excludedByArtist.categoryId = categoryId;
    TrackRepository::upsertScannedTrack(excludedByArtist);

    const auto candidates = TrackRepository::candidatesForCategory(
        categoryId, { excludedByIdId }, { QStringLiteral("Excluded Artist") });

    QCOMPARE(candidates.size(), 1);
    QCOMPARE(candidates.first().id, keptId);
}

void TrackRepositoryTest::candidatesForCategoryLimitsToTenWhenMoreCandidatesExist()
{
    const qint64 categoryId = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);
    for (int i = 0; i < 15; ++i) {
        TrackRecord track;
        track.filePath = QStringLiteral("/fake/category-limit-%1.mp3").arg(i);
        track.categoryId = categoryId;
        TrackRepository::upsertScannedTrack(track);
    }

    // The caller (AutoDjEngine::generateNext()) never looks past index 9 of
    // the returned vector -- LIMIT 10 must actually be applied by the SQL,
    // not just "happen to return everything" for a small test library.
    const auto candidates = TrackRepository::candidatesForCategory(categoryId, {}, {});
    QCOMPARE(candidates.size(), 10);
}

void TrackRepositoryTest::candidatesForPlaylistWithNoRepeatMetricReturnsEveryMemberNotJustTen()
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Big Playlist"));
    for (int i = 0; i < 15; ++i) {
        TrackRecord track;
        track.filePath = QStringLiteral("/fake/playlist-limit-%1.mp3").arg(i);
        const qint64 trackId = TrackRepository::upsertScannedTrack(track);
        PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));
    }

    // Unlike candidatesForCategory(), this must NOT be capped at 10: with
    // SelectionMetric::None ("Random"), the caller picks uniformly across
    // the WHOLE candidate list (no sort metric to bias a "top" pool
    // against) -- see candidatesForPlaylist()'s own comment.
    const auto candidates
        = TrackRepository::candidatesForPlaylist(playlistId, SelectionMetric::None, SortDirection::Descending, {}, {});
    QCOMPARE(candidates.size(), 15);
}

QTEST_MAIN(TrackRepositoryTest)
#include "TrackRepositoryTest.moc"
