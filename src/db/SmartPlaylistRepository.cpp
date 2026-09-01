#include "SmartPlaylistRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

namespace {
SmartPlaylistRecord fromQuery(const QSqlQuery& query)
{
    SmartPlaylistRecord record;
    record.id = query.value(QStringLiteral("id")).toLongLong();
    record.name = query.value(QStringLiteral("name")).toString();
    record.filterJson = query.value(QStringLiteral("filter_json")).toString();
    return record;
}
}

QVector<SmartPlaylistRecord> SmartPlaylistRepository::allSmartPlaylists()
{
    QVector<SmartPlaylistRecord> result;

    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM smart_playlists ORDER BY name COLLATE NOCASE"));
    while (query.next())
        result.append(fromQuery(query));
    return result;
}

SmartPlaylistRecord SmartPlaylistRepository::byId(qint64 id)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM smart_playlists WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return {};
    return fromQuery(query);
}

qint64 SmartPlaylistRepository::create(const QString& name, const QString& filterJson)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("INSERT INTO smart_playlists (name, filter_json) VALUES (:name, :filter)"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":filter"), filterJson);
    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to create smart playlist: %1").arg(query.lastError().text()));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool SmartPlaylistRepository::update(qint64 id, const QString& name, const QString& filterJson)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE smart_playlists SET name = :name, filter_json = :filter WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":filter"), filterJson);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to update smart playlist: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool SmartPlaylistRepository::remove(qint64 id)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery clearBlocks(db);
    clearBlocks.prepare(QStringLiteral("UPDATE schedule_blocks SET smart_playlist_id = NULL WHERE smart_playlist_id = :id"));
    clearBlocks.bindValue(QStringLiteral(":id"), id);
    if (!clearBlocks.exec()) {
        RS_LOG_ERROR("library.playlist",
            QStringLiteral("Failed to clear smart playlist assignment: %1").arg(clearBlocks.lastError().text()));
        return false;
    }

    QSqlQuery deleteRow(db);
    deleteRow.prepare(QStringLiteral("DELETE FROM smart_playlists WHERE id = :id"));
    deleteRow.bindValue(QStringLiteral(":id"), id);
    if (!deleteRow.exec()) {
        RS_LOG_ERROR("library.playlist", QStringLiteral("Failed to delete smart playlist: %1").arg(deleteRow.lastError().text()));
        return false;
    }
    return true;
}

} // namespace radio::db
