#include "TrackRepository.h"
#include "Database.h"
#include "SmartPlaylistFilter.h"
#include "SmartPlaylistRepository.h"

#include "core/Logging.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <algorithm>

namespace radio::db {

namespace {

// Positional, not string-keyed (query.value("colname") is a linear,
// case-insensitive scan per call -- ~24 of those per row adds up across a
// large library) -- every call site that feeds this function queries
// `SELECT * FROM tracks ...` (grepped), so these indices are exactly the
// tracks table's own physical column order: the base CREATE TABLE's column
// list, then every subsequent `ALTER TABLE tracks ADD COLUMN` in migration
// order (SQLite always appends). A migration that inserts a new tracks
// column anywhere but the end would silently break this -- keep new
// columns append-only, matching every migration to date.
TrackRecord fromQuery(const QSqlQuery& query)
{
    TrackRecord record;
    record.id = query.value(0).toLongLong();
    record.filePath = query.value(1).toString();
    record.title = query.value(2).toString();
    record.artist = query.value(3).toString();
    record.album = query.value(4).toString();
    record.genre = query.value(5).toString();
    record.durationMs = query.value(6).toLongLong();
    record.codec = query.value(7).toString();
    record.sampleRate = query.value(8).toInt();
    record.channels = query.value(9).toInt();
    record.bitrate = query.value(10).toInt();
    const QVariant categoryId = query.value(11);
    record.categoryId = categoryId.isNull() ? -1 : categoryId.toLongLong();
    record.addedAt = QDateTime::fromString(query.value(12).toString(), Qt::ISODate);
    record.lastPlayedAt = QDateTime::fromString(query.value(13).toString(), Qt::ISODate);
    record.playCount = query.value(14).toInt();
    record.fileMtime = query.value(15).toLongLong();
    record.missing = query.value(16).toBool();
    record.rating = query.value(17).toInt();
    record.year = query.value(18).toInt();
    record.energy = query.value(19).toDouble();
    record.bpm = query.value(20).toDouble();
    record.hasReplayGain = query.value(21).toBool();
    record.replayGainDb = query.value(22).toDouble();
    record.replayGainPeak = query.value(23).toDouble();
    return record;
}

QVector<TrackRecord> runSelect(QSqlQuery& query)
{
    QVector<TrackRecord> result;
    while (query.next())
        result.append(fromQuery(query));
    return result;
}

// Appends " AND id NOT IN (?,?,...)" / " AND (artist IS NULL OR artist NOT
// IN (?,?,...))" to `sql` and the corresponding values (in the same order
// the placeholders appear) to `bindValues`, or does nothing if the
// respective set is empty. `columnPrefix` lets callers that qualify column
// names (e.g. "tracks.") reuse this. The artist clause's "IS NULL OR"
// mirrors the C++ filtering this replaces -- `!record.artist.isEmpty() &&
// excludeArtists.contains(...)` -- which never excludes a track with no
// artist at all; a NULL column compared via a bare `NOT IN` is neither true
// nor false in SQL's three-valued logic and would otherwise silently drop
// those rows instead of keeping them.
void appendExclusionClauses(QString& sql, QVariantList& bindValues, const QString& columnPrefix,
    const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    if (!excludeTrackIds.isEmpty()) {
        QStringList placeholders;
        placeholders.reserve(excludeTrackIds.size());
        for (qint64 id : excludeTrackIds) {
            placeholders << QStringLiteral("?");
            bindValues << id;
        }
        sql += QStringLiteral(" AND %1id NOT IN (%2)").arg(columnPrefix, placeholders.join(QLatin1Char(',')));
    }
    if (!excludeArtists.isEmpty()) {
        QStringList placeholders;
        placeholders.reserve(excludeArtists.size());
        for (const QString& artist : excludeArtists) {
            placeholders << QStringLiteral("?");
            bindValues << artist;
        }
        sql += QStringLiteral(" AND (%1artist IS NULL OR %1artist NOT IN (%2))")
                   .arg(columnPrefix, placeholders.join(QLatin1Char(',')));
    }
}

QVector<TrackRecord> runSelectWithBindValues(const QString& sql, const QVariantList& bindValues)
{
    QSqlQuery query(Database::handle());
    query.prepare(sql);
    for (const QVariant& value : bindValues)
        query.addBindValue(value);
    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Candidate query failed: %1").arg(query.lastError().text()));
        return {};
    }
    return runSelect(query);
}

} // namespace

qint64 TrackRepository::upsertScannedTrack(const TrackRecord& record)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery existing(db);
    existing.prepare(QStringLiteral(
        "SELECT id, title, artist, album, genre, category_id, energy, bpm FROM tracks WHERE file_path = :path"));
    existing.bindValue(QStringLiteral(":path"), record.filePath);
    if (!existing.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Track lookup failed: %1").arg(existing.lastError().text()));
        return -1;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    if (existing.next()) {
        const qint64 id = existing.value(0).toLongLong();
        // Preserve user-entered title/artist/album/genre across rescans;
        // only fill in fields the user hasn't touched (still empty).
        const QString keepTitle = existing.value(1).toString().isEmpty() ? record.title : existing.value(1).toString();
        const QString keepArtist = existing.value(2).toString().isEmpty() ? record.artist : existing.value(2).toString();
        const QString keepAlbum = existing.value(3).toString().isEmpty() ? record.album : existing.value(3).toString();
        const QString keepGenre = existing.value(4).toString().isEmpty() ? record.genre : existing.value(4).toString();
        // A rescan shouldn't clobber a hand-reassigned category — only fill
        // it in while the track is still uncategorized.
        const QVariant existingCategory = existing.value(5);
        const bool wasUncategorized = existingCategory.isNull() || existingCategory.toLongLong() < 0;
        // energy/bpm come from the manifest merge (ImportDialog), not this
        // scan pass itself — a plain rescan (record.energy/bpm still unset)
        // must not wipe a previously-merged value.
        const double keepEnergy = record.energy >= 0 ? record.energy : existing.value(6).toDouble();
        const double keepBpm = record.bpm >= 0 ? record.bpm : existing.value(7).toDouble();

        QSqlQuery update(db);
        update.prepare(QStringLiteral(R"(UPDATE tracks SET
            title = :title, artist = :artist, album = :album, genre = :genre,
            duration_ms = :duration_ms, codec = :codec, sample_rate = :sample_rate,
            channels = :channels, bitrate = :bitrate, file_mtime = :file_mtime, missing = 0,
            year = :year, energy = :energy, bpm = :bpm,
            has_replay_gain = :has_replay_gain, replay_gain_db = :replay_gain_db, replay_gain_peak = :replay_gain_peak
            WHERE id = :id)"));
        update.bindValue(QStringLiteral(":title"), keepTitle);
        update.bindValue(QStringLiteral(":artist"), keepArtist);
        update.bindValue(QStringLiteral(":album"), keepAlbum);
        update.bindValue(QStringLiteral(":genre"), keepGenre);
        update.bindValue(QStringLiteral(":duration_ms"), record.durationMs);
        update.bindValue(QStringLiteral(":codec"), record.codec);
        update.bindValue(QStringLiteral(":sample_rate"), record.sampleRate);
        update.bindValue(QStringLiteral(":channels"), record.channels);
        update.bindValue(QStringLiteral(":bitrate"), record.bitrate);
        update.bindValue(QStringLiteral(":file_mtime"), record.fileMtime);
        update.bindValue(QStringLiteral(":year"), record.year);
        update.bindValue(QStringLiteral(":energy"), keepEnergy);
        update.bindValue(QStringLiteral(":bpm"), keepBpm);
        update.bindValue(QStringLiteral(":has_replay_gain"), record.hasReplayGain);
        update.bindValue(QStringLiteral(":replay_gain_db"), record.replayGainDb);
        update.bindValue(QStringLiteral(":replay_gain_peak"), record.replayGainPeak);
        update.bindValue(QStringLiteral(":id"), id);
        if (!update.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Track update failed: %1").arg(update.lastError().text()));
            return id;
        }

        if (wasUncategorized && record.categoryId >= 0) {
            QSqlQuery updateCategory(db);
            updateCategory.prepare(QStringLiteral("UPDATE tracks SET category_id = :category_id WHERE id = :id"));
            updateCategory.bindValue(QStringLiteral(":category_id"), record.categoryId);
            updateCategory.bindValue(QStringLiteral(":id"), id);
            if (!updateCategory.exec())
                RS_LOG_ERROR("library.scan", QStringLiteral("Track category update failed: %1").arg(updateCategory.lastError().text()));
        }
        return id;
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(R"(INSERT INTO tracks
        (file_path, title, artist, album, genre, duration_ms, codec, sample_rate, channels, bitrate,
         added_at, play_count, file_mtime, missing, category_id, year, energy, bpm,
         has_replay_gain, replay_gain_db, replay_gain_peak)
        VALUES (:path, :title, :artist, :album, :genre, :duration_ms, :codec, :sample_rate, :channels, :bitrate,
                :added_at, 0, :file_mtime, 0, :category_id, :year, :energy, :bpm,
                :has_replay_gain, :replay_gain_db, :replay_gain_peak))"));
    insert.bindValue(QStringLiteral(":path"), record.filePath);
    insert.bindValue(QStringLiteral(":title"), record.title);
    insert.bindValue(QStringLiteral(":artist"), record.artist);
    insert.bindValue(QStringLiteral(":album"), record.album);
    insert.bindValue(QStringLiteral(":genre"), record.genre);
    insert.bindValue(QStringLiteral(":duration_ms"), record.durationMs);
    insert.bindValue(QStringLiteral(":codec"), record.codec);
    insert.bindValue(QStringLiteral(":sample_rate"), record.sampleRate);
    insert.bindValue(QStringLiteral(":channels"), record.channels);
    insert.bindValue(QStringLiteral(":bitrate"), record.bitrate);
    insert.bindValue(QStringLiteral(":added_at"), now);
    insert.bindValue(QStringLiteral(":file_mtime"), record.fileMtime);
    if (record.categoryId < 0)
        insert.bindValue(QStringLiteral(":category_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":category_id"), record.categoryId);
    insert.bindValue(QStringLiteral(":year"), record.year);
    insert.bindValue(QStringLiteral(":energy"), record.energy);
    insert.bindValue(QStringLiteral(":bpm"), record.bpm);
    insert.bindValue(QStringLiteral(":has_replay_gain"), record.hasReplayGain);
    insert.bindValue(QStringLiteral(":replay_gain_db"), record.replayGainDb);
    insert.bindValue(QStringLiteral(":replay_gain_peak"), record.replayGainPeak);
    if (!insert.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Track insert failed: %1").arg(insert.lastError().text()));
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

QVector<TrackRecord> TrackRepository::allTracks()
{
    // missing = 0: a rescan (ImportDialog::runRescan -> syncMissingForRoot)
    // flags rows whose file has been moved/deleted/renamed since the last
    // scan rather than deleting them outright (so a temporarily-unmounted
    // drive or a since-reverted rename doesn't lose play history/rating/
    // category), but nothing should keep showing them as if they were
    // playable — same filter candidatesForCategory()/candidatesForPlaylist()/
    // candidatesForSmartPlaylist() already apply for Auto-DJ's own picking.
    // Without it, every past library reorganization's stale paths pile up
    // here forever, indistinguishable from the real file.
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM tracks WHERE missing = 0 ORDER BY artist, title"));
    return runSelect(query);
}

QVector<TrackRecord> TrackRepository::searchTracks(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty())
        return allTracks();

    // Backed by the tracks_fts FTS5 index (see Migrations.cpp's applyV13())
    // rather than a `LIKE '%text%'` scan -- unindexable in SQLite regardless
    // of what B-tree indexes exist, so it used to be a full table scan on
    // every call.
    //
    // Query text is split into whitespace-separated terms, each wrapped in
    // double quotes (FTS5's literal-phrase syntax, which also protects
    // against the input being interpreted as FTS5 query syntax --
    // AND/OR/NOT/-/parentheses -- rather than literal text) with a trailing
    // '*' prefix wildcard, so e.g. "sea" matches "Search" while typing
    // rather than requiring a complete word. Juxtaposing multiple quoted
    // terms combines them with FTS5's implicit AND, so "queen bohemian"
    // matches a row whose artist starts with "queen" and title contains a
    // word starting with "bohemian" -- across columns, not just one, unlike
    // the old single-substring LIKE query.
    const QStringList words = trimmed.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QStringList terms;
    terms.reserve(words.size());
    for (const QString& word : words) {
        QString escaped = word;
        escaped.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        terms << QStringLiteral("\"%1\"*").arg(escaped);
    }

    // See allTracks()'s comment on the missing = 0 filter. `tracks.*`, not a
    // bare `*` -- this joins against tracks_fts, whose own columns must not
    // leak into the result set fromQuery() reads positionally.
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT tracks.* FROM tracks JOIN tracks_fts ON tracks.id = tracks_fts.rowid "
                                  "WHERE tracks.missing = 0 AND tracks_fts MATCH :q ORDER BY tracks.artist, tracks.title"));
    query.bindValue(QStringLiteral(":q"), terms.join(QLatin1Char(' ')));
    query.exec();
    return runSelect(query);
}

TrackRecord TrackRepository::trackById(qint64 id)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM tracks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return {};
    return fromQuery(query);
}

QVector<TrackRecord> TrackRepository::candidatesForCategory(
    qint64 categoryId, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    // Exclusion pushed into SQL (rather than fetching every non-missing
    // track and filtering in C++) -- every caller (AutoDjEngine::
    // generateNext()) only ever looks at the first 10 of the returned
    // vector (`poolSize = min(candidates.size(), 10)`), so LIMIT 10 here
    // returns exactly the same rows that vector's first 10 elements always
    // were, just without materializing the rest of the (possibly large)
    // library as TrackRecords only to discard them.
    QString sql = categoryId >= 0
        // Tracks with no category assigned (category_id IS NULL — never
        // categorized via the import manifest or track editor) belong to no
        // rotation bucket of their own, so without this OR they'd never be
        // selected while ANY real category still has tracks in it: the
        // weighted picker would just keep cycling the categorized tracks
        // and uncategorized library additions would sit invisible forever.
        // Secondary ORDER BY RANDOM() breaks ties (most tracks in a mostly-
        // unplayed library share last_played_at = NULL) with a fresh shuffle
        // every call, instead of SQLite's default tie order, which is stable
        // row order -- i.e. the same handful of "top 10" candidates every
        // single pick, usually clustered by import/folder order (same artist
        // back to back), which is exactly what made rotation feel stuck.
        ? QStringLiteral("SELECT * FROM tracks WHERE missing = 0 AND (category_id = ? OR category_id IS NULL)")
        : QStringLiteral("SELECT * FROM tracks WHERE missing = 0");

    QVariantList bindValues;
    if (categoryId >= 0)
        bindValues << categoryId;
    appendExclusionClauses(sql, bindValues, QString(), excludeTrackIds, excludeArtists);
    sql += QStringLiteral(" ORDER BY last_played_at ASC, RANDOM() LIMIT 10");

    return runSelectWithBindValues(sql, bindValues);
}

QVector<TrackRecord> TrackRepository::candidatesForPlaylist(qint64 playlistId, SelectionMetric metric,
    SortDirection direction, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    // Never interpolated from external input — metric/direction are a
    // fixed enum, not user-supplied text, so this switch-selected literal
    // is safe by construction.
    QString orderBy;
    switch (metric) {
    case SelectionMetric::Rating:
        orderBy = direction == SortDirection::Descending ? QStringLiteral("rating DESC, title COLLATE NOCASE ASC")
                                                           : QStringLiteral("rating ASC, title COLLATE NOCASE ASC");
        break;
    case SelectionMetric::PlayCount:
        orderBy = direction == SortDirection::Descending
            ? QStringLiteral("play_count DESC, title COLLATE NOCASE ASC")
            : QStringLiteral("play_count ASC, title COLLATE NOCASE ASC");
        break;
    case SelectionMetric::None:
    default:
        orderBy = QStringLiteral("id ASC");
        break;
    }

    // Membership pushed into the query itself (an IN-subquery against
    // playlist_items, which SQLite can satisfy via its track_id index)
    // instead of a separate membership SELECT plus a full non-missing-
    // library scan filtered in C++ down to just this playlist's tracks --
    // the previous shape's real cost: this runs on every single Auto-DJ
    // pick. No LIMIT here, unlike candidatesForCategory() above -- when
    // metric == None ("Random"), the caller (AutoDjEngine::
    // generateNextFromBlock()) picks uniformly across the ENTIRE candidate
    // list, not just a top-N pool (see its own comment: "there's no
    // meaningful 'top' to bias toward without a sort metric"), so
    // truncating here would skew that toward whatever LIMIT rows happen to
    // sort first -- a real behavior change, not just an optimization.
    QString sql = QStringLiteral("SELECT * FROM tracks WHERE missing = 0 "
                                  "AND id IN (SELECT track_id FROM playlist_items WHERE playlist_id = ?)");
    QVariantList bindValues;
    bindValues << playlistId;
    appendExclusionClauses(sql, bindValues, QString(), excludeTrackIds, excludeArtists);
    sql += QStringLiteral(" ORDER BY ") + orderBy;

    return runSelectWithBindValues(sql, bindValues);
}

QVector<TrackRecord> TrackRepository::candidatesForSmartPlaylist(
    qint64 smartPlaylistId, const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    const SmartPlaylistRecord smartPlaylist = SmartPlaylistRepository::byId(smartPlaylistId);
    if (smartPlaylist.id < 0)
        return {};

    const QJsonObject filter = QJsonDocument::fromJson(smartPlaylist.filterJson.toUtf8()).object();

    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM tracks WHERE missing = 0"));

    QVector<TrackRecord> candidates;
    while (query.next()) {
        TrackRecord record = fromQuery(query);
        if (excludeTrackIds.contains(record.id))
            continue;
        if (!record.artist.isEmpty() && excludeArtists.contains(record.artist))
            continue;
        if (!matchesSmartPlaylistFilter(record, filter))
            continue;
        candidates.append(record);
    }
    return candidates;
}

int TrackRepository::syncMissingForRoot(const QString& rootPath, const QSet<QString>& foundPaths)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery select(db);
    select.prepare(QStringLiteral("SELECT id, file_path FROM tracks WHERE file_path LIKE :prefix"));
    select.bindValue(QStringLiteral(":prefix"), rootPath + QStringLiteral("/%"));
    if (!select.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Missing-file sync query failed: %1").arg(select.lastError().text()));
        return 0;
    }

    QVector<qint64> goneIds;
    while (select.next()) {
        const QString path = select.value(1).toString();
        if (!foundPaths.contains(path))
            goneIds.append(select.value(0).toLongLong());
    }

    if (goneIds.isEmpty())
        return 0;

    // A rescan is a full resync against what's actually on disk under
    // rootPath right now, not a soft "hide it and remember forever" flag --
    // a library that gets reorganized/renamed on disk regularly (common on
    // a shared NTFS drive edited from elsewhere) would otherwise accumulate
    // stale rows without bound, exactly as observed: 6500+ of ~14000 tracks
    // sitting around permanently referencing paths that no longer exist.
    // Delete outright, including anything that only makes sense pointing at
    // a real track: play_history.track_id has no ON DELETE behavior
    // enforced (this codebase never turns on SQLite's "PRAGMA foreign_keys",
    // so the "ON DELETE CASCADE" declared on playlist_items.track_id in the
    // schema is inert too) -- clean up both explicitly rather than leaving
    // dangling track_ids behind. cart_clips is untouched: it stores its own
    // file_path directly, not a tracks.id reference.
    db.transaction();
    QSqlQuery deleteHistory(db);
    deleteHistory.prepare(QStringLiteral("DELETE FROM play_history WHERE track_id = :id"));
    QSqlQuery deletePlaylistItems(db);
    deletePlaylistItems.prepare(QStringLiteral("DELETE FROM playlist_items WHERE track_id = :id"));
    QSqlQuery deleteTrack(db);
    deleteTrack.prepare(QStringLiteral("DELETE FROM tracks WHERE id = :id"));

    int removed = 0;
    for (const qint64 id : goneIds) {
        deleteHistory.bindValue(QStringLiteral(":id"), id);
        deletePlaylistItems.bindValue(QStringLiteral(":id"), id);
        deleteTrack.bindValue(QStringLiteral(":id"), id);
        if (!deleteHistory.exec() || !deletePlaylistItems.exec() || !deleteTrack.exec()) {
            RS_LOG_ERROR("library.scan",
                QStringLiteral("Failed to remove track %1 during rescan sync: %2").arg(id).arg(deleteTrack.lastError().text()));
            continue;
        }
        ++removed;
    }
    db.commit();

    if (removed > 0)
        RS_LOG_INFO("library.scan", QStringLiteral("Rescan removed %1 track(s) no longer found under %2").arg(removed).arg(rootPath));

    return removed;
}

bool TrackRepository::updateEditableFields(
    qint64 id, const QString& title, const QString& artist, const QString& album, const QString& genre,
    qint64 categoryId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "UPDATE tracks SET title = :title, artist = :artist, album = :album, genre = :genre, "
        "category_id = :category_id WHERE id = :id"));
    query.bindValue(QStringLiteral(":title"), title);
    query.bindValue(QStringLiteral(":artist"), artist);
    query.bindValue(QStringLiteral(":album"), album);
    query.bindValue(QStringLiteral(":genre"), genre);
    if (categoryId < 0)
        query.bindValue(QStringLiteral(":category_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":category_id"), categoryId);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Track field update failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<TrackRecord> TrackRepository::tracksNeedingReplayGainAnalysis()
{
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM tracks WHERE missing = 0 AND has_replay_gain = 0 ORDER BY artist, title"));
    return runSelect(query);
}

bool TrackRepository::updateReplayGain(qint64 id, double gainDb, double peak)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "UPDATE tracks SET has_replay_gain = 1, replay_gain_db = :gain_db, replay_gain_peak = :peak WHERE id = :id"));
    query.bindValue(QStringLiteral(":gain_db"), gainDb);
    query.bindValue(QStringLiteral(":peak"), peak);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("ReplayGain update failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<TrackRecord> TrackRepository::tracksNeedingEnergyAnalysis()
{
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM tracks WHERE missing = 0 AND energy < 0 ORDER BY artist, title"));
    return runSelect(query);
}

bool TrackRepository::updateEnergy(qint64 id, double energy)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE tracks SET energy = :energy WHERE id = :id"));
    query.bindValue(QStringLiteral(":energy"), energy);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Energy update failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<TrackRecord> TrackRepository::tracksNeedingBpmAnalysis()
{
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM tracks WHERE missing = 0 AND bpm < 0 ORDER BY artist, title"));
    return runSelect(query);
}

bool TrackRepository::updateBpm(qint64 id, double bpm)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE tracks SET bpm = :bpm WHERE id = :id"));
    query.bindValue(QStringLiteral(":bpm"), bpm);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("BPM update failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool TrackRepository::setRating(qint64 id, int rating)
{
    rating = std::clamp(rating, 0, 5);

    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE tracks SET rating = :rating WHERE id = :id"));
    query.bindValue(QStringLiteral(":rating"), rating);
    query.bindValue(QStringLiteral(":id"), id);

    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Track rating update failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

} // namespace radio::db
