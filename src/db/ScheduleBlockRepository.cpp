#include "ScheduleBlockRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace radio::db {

namespace {
ScheduleBlockRecord fromQuery(const QSqlQuery& query)
{
    ScheduleBlockRecord record;
    record.id = query.value(QStringLiteral("id")).toLongLong();
    record.name = query.value(QStringLiteral("name")).toString();
    record.position = query.value(QStringLiteral("position")).toInt();
    record.enabled = query.value(QStringLiteral("enabled")).toBool();
    record.daysMask = query.value(QStringLiteral("days_mask")).toInt();
    record.startMinute = query.value(QStringLiteral("start_minute")).toInt();
    record.endMinute = query.value(QStringLiteral("end_minute")).toInt();
    record.playlistId = query.value(QStringLiteral("playlist_id")).toLongLong();
    record.selectionMetric = query.value(QStringLiteral("selection_metric")).toString();
    record.selectionDirection = query.value(QStringLiteral("selection_direction")).toString();
    record.selectionMode = query.value(QStringLiteral("selection_mode")).toString();
    record.cartFrequency = query.value(QStringLiteral("cart_frequency")).toInt();
    record.cartMode = query.value(QStringLiteral("cart_mode")).toString();
    record.cartColor = query.value(QStringLiteral("cart_color")).toString();
    record.cartOffsetSeconds = query.value(QStringLiteral("cart_offset_seconds")).toInt();
    record.cartPlaybackType = query.value(QStringLiteral("cart_playback_type")).toString();
    record.queueMode = query.value(QStringLiteral("queue_mode")).toString();
    record.queueSize = query.value(QStringLiteral("queue_size")).toInt();
    const QVariant smartPlaylistId = query.value(QStringLiteral("smart_playlist_id"));
    record.smartPlaylistId = smartPlaylistId.isNull() ? -1 : smartPlaylistId.toLongLong();
    const QVariant clockId = query.value(QStringLiteral("clock_id"));
    record.clockId = clockId.isNull() ? -1 : clockId.toLongLong();
    return record;
}
}

QVector<ScheduleBlockRecord> ScheduleBlockRepository::allBlocks()
{
    QVector<ScheduleBlockRecord> result;

    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM schedule_blocks ORDER BY position ASC"));
    while (query.next())
        result.append(fromQuery(query));
    return result;
}

qint64 ScheduleBlockRepository::createBlock(const ScheduleBlockRecord& block)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery maxPos(db);
    maxPos.exec(QStringLiteral("SELECT COALESCE(MAX(position), -1) FROM schedule_blocks"));
    const int nextPosition = maxPos.next() ? maxPos.value(0).toInt() + 1 : 0;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(R"(INSERT INTO schedule_blocks
        (name, position, enabled, days_mask, start_minute, end_minute, playlist_id,
         selection_metric, selection_direction, selection_mode,
         cart_frequency, cart_mode, cart_color, cart_offset_seconds, cart_playback_type,
         queue_mode, queue_size, smart_playlist_id, clock_id)
        VALUES (:name, :position, :enabled, :days_mask, :start_minute, :end_minute, :playlist_id,
                :selection_metric, :selection_direction, :selection_mode,
                :cart_frequency, :cart_mode, :cart_color, :cart_offset_seconds, :cart_playback_type,
                :queue_mode, :queue_size, :smart_playlist_id, :clock_id))"));
    insert.bindValue(QStringLiteral(":name"), block.name);
    insert.bindValue(QStringLiteral(":position"), nextPosition);
    insert.bindValue(QStringLiteral(":enabled"), block.enabled ? 1 : 0);
    insert.bindValue(QStringLiteral(":days_mask"), block.daysMask);
    insert.bindValue(QStringLiteral(":start_minute"), block.startMinute);
    insert.bindValue(QStringLiteral(":end_minute"), block.endMinute);
    insert.bindValue(QStringLiteral(":playlist_id"), block.playlistId);
    insert.bindValue(QStringLiteral(":selection_metric"), block.selectionMetric);
    insert.bindValue(QStringLiteral(":selection_direction"), block.selectionDirection);
    insert.bindValue(QStringLiteral(":selection_mode"), block.selectionMode);
    insert.bindValue(QStringLiteral(":cart_frequency"), block.cartFrequency);
    insert.bindValue(QStringLiteral(":cart_mode"), block.cartMode);
    insert.bindValue(QStringLiteral(":cart_color"), block.cartColor);
    insert.bindValue(QStringLiteral(":cart_offset_seconds"), block.cartOffsetSeconds);
    insert.bindValue(QStringLiteral(":cart_playback_type"), block.cartPlaybackType);
    insert.bindValue(QStringLiteral(":queue_mode"), block.queueMode);
    insert.bindValue(QStringLiteral(":queue_size"), block.queueSize);
    if (block.smartPlaylistId < 0)
        insert.bindValue(QStringLiteral(":smart_playlist_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":smart_playlist_id"), block.smartPlaylistId);
    if (block.clockId < 0)
        insert.bindValue(QStringLiteral(":clock_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":clock_id"), block.clockId);
    if (!insert.exec()) {
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Failed to create schedule block: %1").arg(insert.lastError().text()));
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

bool ScheduleBlockRepository::updateBlock(const ScheduleBlockRecord& block)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(R"(UPDATE schedule_blocks SET
        name = :name, enabled = :enabled, days_mask = :days_mask,
        start_minute = :start_minute, end_minute = :end_minute, playlist_id = :playlist_id,
        selection_metric = :selection_metric, selection_direction = :selection_direction,
        selection_mode = :selection_mode, cart_frequency = :cart_frequency, cart_mode = :cart_mode,
        cart_color = :cart_color, cart_offset_seconds = :cart_offset_seconds,
        cart_playback_type = :cart_playback_type, queue_mode = :queue_mode, queue_size = :queue_size,
        smart_playlist_id = :smart_playlist_id, clock_id = :clock_id
        WHERE id = :id)"));
    query.bindValue(QStringLiteral(":name"), block.name);
    query.bindValue(QStringLiteral(":enabled"), block.enabled ? 1 : 0);
    query.bindValue(QStringLiteral(":days_mask"), block.daysMask);
    query.bindValue(QStringLiteral(":start_minute"), block.startMinute);
    query.bindValue(QStringLiteral(":end_minute"), block.endMinute);
    query.bindValue(QStringLiteral(":playlist_id"), block.playlistId);
    query.bindValue(QStringLiteral(":selection_metric"), block.selectionMetric);
    query.bindValue(QStringLiteral(":selection_direction"), block.selectionDirection);
    query.bindValue(QStringLiteral(":selection_mode"), block.selectionMode);
    query.bindValue(QStringLiteral(":cart_frequency"), block.cartFrequency);
    query.bindValue(QStringLiteral(":cart_mode"), block.cartMode);
    query.bindValue(QStringLiteral(":cart_color"), block.cartColor);
    query.bindValue(QStringLiteral(":cart_offset_seconds"), block.cartOffsetSeconds);
    query.bindValue(QStringLiteral(":cart_playback_type"), block.cartPlaybackType);
    query.bindValue(QStringLiteral(":queue_mode"), block.queueMode);
    query.bindValue(QStringLiteral(":queue_size"), block.queueSize);
    if (block.smartPlaylistId < 0)
        query.bindValue(QStringLiteral(":smart_playlist_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":smart_playlist_id"), block.smartPlaylistId);
    if (block.clockId < 0)
        query.bindValue(QStringLiteral(":clock_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":clock_id"), block.clockId);
    query.bindValue(QStringLiteral(":id"), block.id);

    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Failed to update schedule block: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ScheduleBlockRepository::deleteBlock(qint64 blockId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM schedule_blocks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), blockId);
    return query.exec();
}

bool ScheduleBlockRepository::setBlockOrder(const QVector<qint64>& blockIdsInOrder)
{
    QSqlDatabase db = Database::handle();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE schedule_blocks SET position = :pos WHERE id = :id"));

    db.transaction();
    for (int i = 0; i < blockIdsInOrder.size(); ++i) {
        query.bindValue(QStringLiteral(":pos"), i);
        query.bindValue(QStringLiteral(":id"), blockIdsInOrder.at(i));
        if (!query.exec()) {
            RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Failed to reorder schedule blocks: %1").arg(query.lastError().text()));
            db.rollback();
            return false;
        }
    }
    db.commit();
    return true;
}

} // namespace radio::db
