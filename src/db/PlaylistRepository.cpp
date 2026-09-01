#include "PlaylistRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace radio::db {

qint64 PlaylistRepository::ensureQueuePlaylistId()
{
    QSqlDatabase db = Database::handle();

    QSqlQuery find(db);
    find.exec(QStringLiteral("SELECT id FROM playlists WHERE is_queue = 1 LIMIT 1"));
    if (find.next())
        return find.value(0).toLongLong();

    QSqlQuery create(db);
    create.prepare(QStringLiteral("INSERT INTO playlists (name, is_queue) VALUES ('Queue', 1)"));
    if (!create.exec()) {
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Failed to create queue playlist: %1").arg(create.lastError().text()));
        return -1;
    }
    return create.lastInsertId().toLongLong();
}

QVector<QueueItemRecord> PlaylistRepository::queueItems()
{
    QVector<QueueItemRecord> result;
    const qint64 queueId = ensureQueuePlaylistId();
    if (queueId < 0)
        return result;

    const auto items = itemsForPlaylist(queueId);
    result.reserve(items.size());
    for (const auto& item : items) {
        QueueItemRecord record;
        record.playlistItemId = item.playlistItemId;
        record.trackId = item.trackId;
        record.filePath = item.filePath;
        record.title = item.title;
        record.artist = item.artist;
        record.position = item.position;
        record.source = item.source;
        record.durationMs = item.durationMs;
        record.missing = item.missing;
        result.append(record);
    }
    return result;
}

bool PlaylistRepository::appendToQueue(qint64 trackId, const QString& source)
{
    const qint64 queueId = ensureQueuePlaylistId();
    if (queueId < 0)
        return false;
    return addTrackToPlaylist(queueId, trackId, source);
}

bool PlaylistRepository::clearQueue()
{
    const qint64 queueId = ensureQueuePlaylistId();
    if (queueId < 0)
        return false;

    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :id"));
    query.bindValue(QStringLiteral(":id"), queueId);
    return query.exec();
}

QVector<PlaylistRecord> PlaylistRepository::listPlaylists()
{
    QVector<PlaylistRecord> result;

    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral(R"(
        SELECT p.id, p.name, p.folder_id, COUNT(pi.id) AS track_count
        FROM playlists p LEFT JOIN playlist_items pi ON pi.playlist_id = p.id
        WHERE p.is_queue = 0
        GROUP BY p.id
        ORDER BY p.name COLLATE NOCASE)"));

    while (query.next()) {
        PlaylistRecord record;
        record.id = query.value(0).toLongLong();
        record.name = query.value(1).toString();
        const QVariant folderId = query.value(2);
        record.folderId = folderId.isNull() ? -1 : folderId.toLongLong();
        record.trackCount = query.value(3).toInt();
        result.append(record);
    }
    return result;
}

qint64 PlaylistRepository::createPlaylist(const QString& name, qint64 folderId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("INSERT INTO playlists (name, is_queue, folder_id) VALUES (:name, 0, :folder)"));
    query.bindValue(QStringLiteral(":name"), name);
    if (folderId < 0)
        query.bindValue(QStringLiteral(":folder"), QVariant());
    else
        query.bindValue(QStringLiteral(":folder"), folderId);

    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to create playlist: %1").arg(query.lastError().text()));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

qint64 PlaylistRepository::createPlaylistFromTracks(
    const QString& name, const QVector<qint64>& trackIds, qint64 folderId)
{
    const qint64 playlistId = createPlaylist(name, folderId);
    if (playlistId < 0)
        return -1;

    for (const qint64 trackId : trackIds)
        addTrackToPlaylist(playlistId, trackId, QStringLiteral("import"));

    return playlistId;
}

bool PlaylistRepository::renamePlaylist(qint64 playlistId, const QString& newName)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE playlists SET name = :name WHERE id = :id AND is_queue = 0"));
    query.bindValue(QStringLiteral(":name"), newName);
    query.bindValue(QStringLiteral(":id"), playlistId);

    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to rename playlist: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool PlaylistRepository::deletePlaylist(qint64 playlistId)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery checkQueue(db);
    checkQueue.prepare(QStringLiteral("SELECT is_queue FROM playlists WHERE id = :id"));
    checkQueue.bindValue(QStringLiteral(":id"), playlistId);
    if (!checkQueue.exec() || !checkQueue.next())
        return false;
    if (checkQueue.value(0).toInt() != 0) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Refused to delete the queue playlist"));
        return false;
    }

    QSqlQuery checkBlocks(db);
    checkBlocks.prepare(QStringLiteral("SELECT COUNT(*) FROM schedule_blocks WHERE playlist_id = :id"));
    checkBlocks.bindValue(QStringLiteral(":id"), playlistId);
    if (checkBlocks.exec() && checkBlocks.next() && checkBlocks.value(0).toInt() > 0) {
        RS_LOG_ERROR("library.playlist",
            QStringLiteral("Refused to delete playlist %1: still referenced by a schedule block").arg(playlistId));
        return false;
    }

    QSqlQuery deleteItems(db);
    deleteItems.prepare(QStringLiteral("DELETE FROM playlist_items WHERE playlist_id = :id"));
    deleteItems.bindValue(QStringLiteral(":id"), playlistId);
    if (!deleteItems.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to delete playlist items: %1").arg(deleteItems.lastError().text()));
        return false;
    }

    QSqlQuery deletePlaylistRow(db);
    deletePlaylistRow.prepare(QStringLiteral("DELETE FROM playlists WHERE id = :id"));
    deletePlaylistRow.bindValue(QStringLiteral(":id"), playlistId);
    if (!deletePlaylistRow.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to delete playlist: %1").arg(deletePlaylistRow.lastError().text()));
        return false;
    }
    return true;
}

bool PlaylistRepository::setPlaylistFolder(qint64 playlistId, qint64 folderId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE playlists SET folder_id = :folder WHERE id = :id"));
    if (folderId < 0)
        query.bindValue(QStringLiteral(":folder"), QVariant());
    else
        query.bindValue(QStringLiteral(":folder"), folderId);
    query.bindValue(QStringLiteral(":id"), playlistId);

    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to set playlist folder: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

QVector<PlaylistItemRecord> PlaylistRepository::itemsForPlaylist(qint64 playlistId)
{
    QVector<PlaylistItemRecord> result;

    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(R"(
        SELECT pi.id, pi.track_id, t.file_path, t.title, t.artist, pi.position, pi.source, t.duration_ms, t.missing
        FROM playlist_items pi JOIN tracks t ON t.id = pi.track_id
        WHERE pi.playlist_id = :playlist_id
        ORDER BY pi.position ASC)"));
    query.bindValue(QStringLiteral(":playlist_id"), playlistId);
    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to load playlist items: %1").arg(query.lastError().text()));
        return result;
    }

    while (query.next()) {
        PlaylistItemRecord item;
        item.playlistItemId = query.value(0).toLongLong();
        item.trackId = query.value(1).toLongLong();
        item.filePath = query.value(2).toString();
        item.title = query.value(3).toString();
        item.artist = query.value(4).toString();
        item.position = query.value(5).toInt();
        item.source = query.value(6).toString();
        item.durationMs = query.value(7).toLongLong();
        item.missing = query.value(8).toBool();
        result.append(item);
    }
    return result;
}

bool PlaylistRepository::addTrackToPlaylist(qint64 playlistId, qint64 trackId, const QString& source)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery maxPos(db);
    maxPos.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) FROM playlist_items WHERE playlist_id = :id"));
    maxPos.bindValue(QStringLiteral(":id"), playlistId);
    maxPos.exec();
    const int nextPosition = maxPos.next() ? maxPos.value(0).toInt() + 1 : 0;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
        "INSERT INTO playlist_items (playlist_id, track_id, position, source) VALUES (:pid, :tid, :pos, :source)"));
    insert.bindValue(QStringLiteral(":pid"), playlistId);
    insert.bindValue(QStringLiteral(":tid"), trackId);
    insert.bindValue(QStringLiteral(":pos"), nextPosition);
    insert.bindValue(QStringLiteral(":source"), source);
    if (!insert.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to add track to playlist: %1").arg(insert.lastError().text()));
        return false;
    }
    return true;
}

bool PlaylistRepository::removeItem(qint64 playlistItemId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM playlist_items WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), playlistItemId);
    return query.exec();
}

bool PlaylistRepository::setItemOrder(const QVector<qint64>& playlistItemIdsInOrder)
{
    QSqlDatabase db = Database::handle();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE playlist_items SET position = :pos WHERE id = :id"));

    db.transaction();
    for (int i = 0; i < playlistItemIdsInOrder.size(); ++i) {
        query.bindValue(QStringLiteral(":pos"), i);
        query.bindValue(QStringLiteral(":id"), playlistItemIdsInOrder.at(i));
        if (!query.exec()) {
            RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to reorder playlist items: %1").arg(query.lastError().text()));
            db.rollback();
            return false;
        }
    }
    db.commit();
    return true;
}

} // namespace radio::db
