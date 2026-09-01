#pragma once

#include "TrackRecord.h"

#include <QSet>
#include <QVector>

namespace radio::db {

enum class SelectionMetric { None, Rating, PlayCount };
enum class SortDirection { Ascending, Descending };

// All methods must run on the UI thread (see Database.h).
class TrackRepository {
public:
    // Inserts a new track, or updates the scan-derived fields of an
    // existing one with the same file_path (title/artist/album/genre are
    // only overwritten if the existing values are empty, so user edits made
    // via TrackEditDialog survive a rescan). Returns the row id.
    static qint64 upsertScannedTrack(const TrackRecord& record);

    // Non-missing tracks only (see syncMissingForRoot below) — same
    // filtering as candidatesForCategory()/candidatesForPlaylist()/
    // candidatesForSmartPlaylist(), so the Library browser and Smart
    // Playlist editor don't keep showing stale rows left behind by a past
    // library reorganization as if they were still playable.
    static QVector<TrackRecord> allTracks();
    static QVector<TrackRecord> searchTracks(const QString& text);

    // Single-row lookup by id. Returns a default-constructed TrackRecord
    // (id == -1) if no such track exists, matching this codebase's existing
    // id == -1 "not found" convention (PlaylistRecord::folderId,
    // ScheduleBlockRecord::playlistId, etc.) rather than std::optional.
    static TrackRecord trackById(qint64 id);

    // User-editable fields only (TrackEditDialog).
    static bool updateEditableFields(
        qint64 id, const QString& title, const QString& artist, const QString& album, const QString& genre,
        qint64 categoryId);

    // Clamped to 0-5. Kept separate from updateEditableFields() since rating
    // is set inline via RatingDelegate's click handling, not through
    // TrackEditDialog's modal flow.
    static bool setRating(qint64 id, int rating);

    // Non-missing tracks with no known ReplayGain data yet (see
    // TrackRecord::hasReplayGain) — the candidate set for
    // ReplayGainAnalyzer/AnalyzeLoudnessDialog.
    static QVector<TrackRecord> tracksNeedingReplayGainAnalysis();

    // Records a freshly computed-and-written ReplayGain result
    // (ReplayGainAnalyzer) immediately, rather than waiting for a future
    // rescan's MetadataScanner pass to rediscover the tag it just wrote.
    static bool updateReplayGain(qint64 id, double gainDb, double peak);

    // Non-missing tracks with no known BPM yet (bpm < 0 -- see
    // TrackRecord::bpm) — the candidate set for BpmAnalyzer/AnalyzeBpmDialog,
    // mirroring tracksNeedingReplayGainAnalysis()'s shape exactly.
    static QVector<TrackRecord> tracksNeedingBpmAnalysis();

    // Records a freshly computed-and-written BPM result (BpmAnalyzer)
    // immediately, mirroring updateReplayGain()'s reasoning exactly.
    static bool updateBpm(qint64 id, double bpm);

    // Non-missing tracks with no energy score yet (see TrackRecord::energy)
    // — the candidate set for EnergyAnalyzer/AnalyzeEnergyDialog.
    static QVector<TrackRecord> tracksNeedingEnergyAnalysis();

    // Records a freshly computed energy score (EnergyAnalyzer).
    static bool updateEnergy(qint64 id, double energy);

    // Non-missing tracks in categoryId (or any category if categoryId < 0),
    // excluding the given track ids / artists, oldest-last_played_at first.
    // Used by the Auto-DJ scheduler; small-library-friendly (filters in
    // C++ after a single query rather than building dynamic SQL IN lists).
    static QVector<TrackRecord> candidatesForCategory(
        qint64 categoryId, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists);

    // Non-missing tracks belonging to playlistId, sorted by metric/direction
    // (ties broken by title, ascending — the "(A-Z)" naming in the schedule
    // block editor's UI), excluding the given track ids / artists. Same
    // small-library-friendly design as candidatesForCategory() (query
    // broad, filter in C++ rather than building a dynamic SQL IN list).
    // Used by AutoDjEngine's schedule-block picking path.
    static QVector<TrackRecord> candidatesForPlaylist(qint64 playlistId, SelectionMetric metric,
        SortDirection direction, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists);

    // Non-missing tracks matching smartPlaylistId's filter_json (see
    // SmartPlaylistFilter::matches()), excluding the given track ids /
    // artists. Same query-broad-filter-in-C++ design as
    // candidatesForCategory()/candidatesForPlaylist(). Used by
    // AutoDjEngine's schedule-block picking path when a block targets a
    // smart playlist instead of a static one.
    static QVector<TrackRecord> candidatesForSmartPlaylist(
        qint64 smartPlaylistId, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists);

    // Reconciles the DB against a fresh directory walk of rootPath (see
    // ImportDialog::runRescan): any currently-not-missing track whose
    // file_path falls under rootPath but wasn't in this walk's foundPaths
    // gets flagged missing=1 -- moved/deleted since the last rescan.
    // Rediscovered files are handled separately, by the normal
    // upsertScannedTrack() calls the same rescan makes for every file it
    // does find (which already reset missing=0). Returns how many tracks
    // were newly flagged.
    static int syncMissingForRoot(const QString& rootPath, const QSet<QString>& foundPaths);
};

} // namespace radio::db
