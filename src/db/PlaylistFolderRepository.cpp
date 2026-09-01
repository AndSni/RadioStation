#include "PlaylistFolderRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

QVector<PlaylistFolderRecord> PlaylistFolderRepository::allFolders()
{
    QVector<PlaylistFolderRecord> result;

    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT id, name, position FROM playlist_folders ORDER BY position ASC"));
    while (query.next()) {
        PlaylistFolderRecord record;
        record.id = query.value(0).toLongLong();
        record.name = query.value(1).toString();
        record.position = query.value(2).toInt();
        result.append(record);
    }
    return result;
}

qint64 PlaylistFolderRepository::createFolder(const QString& name)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery maxPos(db);
    maxPos.exec(QStringLiteral("SELECT COALESCE(MAX(position), -1) FROM playlist_folders"));
    const int nextPosition = maxPos.next() ? maxPos.value(0).toInt() + 1 : 0;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO playlist_folders (name, position) VALUES (:name, :pos)"));
    insert.bindValue(QStringLiteral(":name"), name);
    insert.bindValue(QStringLiteral(":pos"), nextPosition);
    if (!insert.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to create playlist folder: %1").arg(insert.lastError().text()));
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

bool PlaylistFolderRepository::renameFolder(qint64 folderId, const QString& newName)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE playlist_folders SET name = :name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), newName);
    query.bindValue(QStringLiteral(":id"), folderId);

    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to rename playlist folder: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool PlaylistFolderRepository::deleteFolder(qint64 folderId)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery clearMembers(db);
    clearMembers.prepare(QStringLiteral("UPDATE playlists SET folder_id = NULL WHERE folder_id = :id"));
    clearMembers.bindValue(QStringLiteral(":id"), folderId);
    if (!clearMembers.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to clear playlist folder assignment: %1").arg(clearMembers.lastError().text()));
        return false;
    }

    QSqlQuery deleteFolderRow(db);
    deleteFolderRow.prepare(QStringLiteral("DELETE FROM playlist_folders WHERE id = :id"));
    deleteFolderRow.bindValue(QStringLiteral(":id"), folderId);
    if (!deleteFolderRow.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to delete playlist folder: %1").arg(deleteFolderRow.lastError().text()));
        return false;
    }
    return true;
}

bool PlaylistFolderRepository::setFolderOrder(const QVector<qint64>& folderIdsInOrder)
{
    QSqlDatabase db = Database::handle();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE playlist_folders SET position = :pos WHERE id = :id"));

    db.transaction();
    for (int i = 0; i < folderIdsInOrder.size(); ++i) {
        query.bindValue(QStringLiteral(":pos"), i);
        query.bindValue(QStringLiteral(":id"), folderIdsInOrder.at(i));
        if (!query.exec()) {
            RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to reorder playlist folders: %1").arg(query.lastError().text()));
            db.rollback();
            return false;
        }
    }
    db.commit();
    return true;
}

} // namespace radio::db
