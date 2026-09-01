#include "ClockRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

namespace radio::db {

namespace {
ClockRecord clockFromQuery(const QSqlQuery& query)
{
    ClockRecord record;
    record.id = query.value(QStringLiteral("id")).toLongLong();
    record.name = query.value(QStringLiteral("name")).toString();
    return record;
}

ClockElementRecord elementFromQuery(const QSqlQuery& query)
{
    ClockElementRecord record;
    record.id = query.value(QStringLiteral("id")).toLongLong();
    record.clockId = query.value(QStringLiteral("clock_id")).toLongLong();
    record.position = query.value(QStringLiteral("position")).toInt();
    record.elementType = query.value(QStringLiteral("element_type")).toString();
    record.label = query.value(QStringLiteral("label")).toString();
    record.itemCount = query.value(QStringLiteral("item_count")).toInt();
    const QVariant minuteOffset = query.value(QStringLiteral("minute_offset"));
    record.minuteOffset = minuteOffset.isNull() ? -1 : minuteOffset.toInt();
    record.timingMode = query.value(QStringLiteral("timing_mode")).toString();
    const QVariant playlistId = query.value(QStringLiteral("playlist_id"));
    record.playlistId = playlistId.isNull() ? -1 : playlistId.toLongLong();
    const QVariant smartPlaylistId = query.value(QStringLiteral("smart_playlist_id"));
    record.smartPlaylistId = smartPlaylistId.isNull() ? -1 : smartPlaylistId.toLongLong();
    record.selectionMetric = query.value(QStringLiteral("selection_metric")).toString();
    record.selectionDirection = query.value(QStringLiteral("selection_direction")).toString();
    record.selectionMode = query.value(QStringLiteral("selection_mode")).toString();
    record.cartMode = query.value(QStringLiteral("cart_mode")).toString();
    record.cartColor = query.value(QStringLiteral("cart_color")).toString();
    const QVariant cartClipId = query.value(QStringLiteral("cart_clip_id"));
    record.cartClipId = cartClipId.isNull() ? -1 : cartClipId.toLongLong();
    return record;
}

ClockWheelStateRecord wheelStateFromQuery(const QSqlQuery& query)
{
    ClockWheelStateRecord record;
    record.scheduleBlockId = query.value(QStringLiteral("schedule_block_id")).toLongLong();
    record.clockId = query.value(QStringLiteral("clock_id")).toLongLong();
    record.hourStartEpoch = query.value(QStringLiteral("hour_start_epoch")).toLongLong();
    record.position = query.value(QStringLiteral("position")).toInt();
    record.itemsDone = query.value(QStringLiteral("items_done")).toInt();
    return record;
}
}

QVector<ClockRecord> ClockRepository::allClocks()
{
    QVector<ClockRecord> result;
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT * FROM clocks ORDER BY name COLLATE NOCASE"));
    while (query.next())
        result.append(clockFromQuery(query));
    return result;
}

ClockRecord ClockRepository::clockById(qint64 id)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM clocks WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec() || !query.next())
        return {};
    return clockFromQuery(query);
}

ClockElementRecord ClockRepository::elementById(qint64 elementId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM clock_elements WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), elementId);
    if (!query.exec() || !query.next())
        return {};
    return elementFromQuery(query);
}

qint64 ClockRepository::createClock(const QString& name)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("INSERT INTO clocks (name) VALUES (:name)"));
    query.bindValue(QStringLiteral(":name"), name);
    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to create clock: %1").arg(query.lastError().text()));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool ClockRepository::renameClock(qint64 id, const QString& name)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("UPDATE clocks SET name = :name WHERE id = :id"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":id"), id);
    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to rename clock: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ClockRepository::deleteClock(qint64 id)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery clearBlocks(db);
    clearBlocks.prepare(QStringLiteral("UPDATE schedule_blocks SET clock_id = NULL WHERE clock_id = :id"));
    clearBlocks.bindValue(QStringLiteral(":id"), id);
    if (!clearBlocks.exec()) {
        RS_LOG_ERROR("scheduler.clock",
            QStringLiteral("Failed to clear clock assignment: %1").arg(clearBlocks.lastError().text()));
        return false;
    }

    QSqlQuery clearWheelState(db);
    clearWheelState.prepare(QStringLiteral("DELETE FROM clock_wheel_state WHERE clock_id = :id"));
    clearWheelState.bindValue(QStringLiteral(":id"), id);
    if (!clearWheelState.exec()) {
        RS_LOG_ERROR("scheduler.clock",
            QStringLiteral("Failed to clear clock wheel state: %1").arg(clearWheelState.lastError().text()));
        return false;
    }

    QSqlQuery deleteElements(db);
    deleteElements.prepare(QStringLiteral("DELETE FROM clock_elements WHERE clock_id = :id"));
    deleteElements.bindValue(QStringLiteral(":id"), id);
    if (!deleteElements.exec()) {
        RS_LOG_ERROR("scheduler.clock",
            QStringLiteral("Failed to delete clock elements: %1").arg(deleteElements.lastError().text()));
        return false;
    }

    QSqlQuery deleteRow(db);
    deleteRow.prepare(QStringLiteral("DELETE FROM clocks WHERE id = :id"));
    deleteRow.bindValue(QStringLiteral(":id"), id);
    if (!deleteRow.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to delete clock: %1").arg(deleteRow.lastError().text()));
        return false;
    }
    return true;
}

QVector<ClockElementRecord> ClockRepository::elementsForClock(qint64 clockId)
{
    QVector<ClockElementRecord> result;
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM clock_elements WHERE clock_id = :clock_id ORDER BY position ASC"));
    query.bindValue(QStringLiteral(":clock_id"), clockId);
    query.exec();
    while (query.next())
        result.append(elementFromQuery(query));
    return result;
}

qint64 ClockRepository::createElement(const ClockElementRecord& element)
{
    QSqlDatabase db = Database::handle();

    QSqlQuery maxPos(db);
    maxPos.prepare(QStringLiteral("SELECT COALESCE(MAX(position), -1) FROM clock_elements WHERE clock_id = :clock_id"));
    maxPos.bindValue(QStringLiteral(":clock_id"), element.clockId);
    maxPos.exec();
    const int nextPosition = maxPos.next() ? maxPos.value(0).toInt() + 1 : 0;

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(R"(INSERT INTO clock_elements
        (clock_id, position, element_type, label, item_count, minute_offset, timing_mode,
         playlist_id, smart_playlist_id, selection_metric, selection_direction, selection_mode,
         cart_mode, cart_color, cart_clip_id)
        VALUES (:clock_id, :position, :element_type, :label, :item_count, :minute_offset, :timing_mode,
                :playlist_id, :smart_playlist_id, :selection_metric, :selection_direction, :selection_mode,
                :cart_mode, :cart_color, :cart_clip_id))"));
    insert.bindValue(QStringLiteral(":clock_id"), element.clockId);
    insert.bindValue(QStringLiteral(":position"), nextPosition);
    insert.bindValue(QStringLiteral(":element_type"), element.elementType);
    insert.bindValue(QStringLiteral(":label"), element.label);
    insert.bindValue(QStringLiteral(":item_count"), element.itemCount);
    if (element.minuteOffset < 0)
        insert.bindValue(QStringLiteral(":minute_offset"), QVariant());
    else
        insert.bindValue(QStringLiteral(":minute_offset"), element.minuteOffset);
    insert.bindValue(QStringLiteral(":timing_mode"), element.timingMode);
    if (element.playlistId < 0)
        insert.bindValue(QStringLiteral(":playlist_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":playlist_id"), element.playlistId);
    if (element.smartPlaylistId < 0)
        insert.bindValue(QStringLiteral(":smart_playlist_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":smart_playlist_id"), element.smartPlaylistId);
    insert.bindValue(QStringLiteral(":selection_metric"), element.selectionMetric);
    insert.bindValue(QStringLiteral(":selection_direction"), element.selectionDirection);
    insert.bindValue(QStringLiteral(":selection_mode"), element.selectionMode);
    insert.bindValue(QStringLiteral(":cart_mode"), element.cartMode);
    insert.bindValue(QStringLiteral(":cart_color"), element.cartColor);
    if (element.cartClipId < 0)
        insert.bindValue(QStringLiteral(":cart_clip_id"), QVariant());
    else
        insert.bindValue(QStringLiteral(":cart_clip_id"), element.cartClipId);

    if (!insert.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to create clock element: %1").arg(insert.lastError().text()));
        return -1;
    }
    return insert.lastInsertId().toLongLong();
}

bool ClockRepository::updateElement(const ClockElementRecord& element)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(R"(UPDATE clock_elements SET
        element_type = :element_type, label = :label, item_count = :item_count,
        minute_offset = :minute_offset, timing_mode = :timing_mode,
        playlist_id = :playlist_id, smart_playlist_id = :smart_playlist_id,
        selection_metric = :selection_metric, selection_direction = :selection_direction,
        selection_mode = :selection_mode, cart_mode = :cart_mode, cart_color = :cart_color,
        cart_clip_id = :cart_clip_id
        WHERE id = :id)"));
    query.bindValue(QStringLiteral(":element_type"), element.elementType);
    query.bindValue(QStringLiteral(":label"), element.label);
    query.bindValue(QStringLiteral(":item_count"), element.itemCount);
    if (element.minuteOffset < 0)
        query.bindValue(QStringLiteral(":minute_offset"), QVariant());
    else
        query.bindValue(QStringLiteral(":minute_offset"), element.minuteOffset);
    query.bindValue(QStringLiteral(":timing_mode"), element.timingMode);
    if (element.playlistId < 0)
        query.bindValue(QStringLiteral(":playlist_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":playlist_id"), element.playlistId);
    if (element.smartPlaylistId < 0)
        query.bindValue(QStringLiteral(":smart_playlist_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":smart_playlist_id"), element.smartPlaylistId);
    query.bindValue(QStringLiteral(":selection_metric"), element.selectionMetric);
    query.bindValue(QStringLiteral(":selection_direction"), element.selectionDirection);
    query.bindValue(QStringLiteral(":selection_mode"), element.selectionMode);
    query.bindValue(QStringLiteral(":cart_mode"), element.cartMode);
    query.bindValue(QStringLiteral(":cart_color"), element.cartColor);
    if (element.cartClipId < 0)
        query.bindValue(QStringLiteral(":cart_clip_id"), QVariant());
    else
        query.bindValue(QStringLiteral(":cart_clip_id"), element.cartClipId);
    query.bindValue(QStringLiteral(":id"), element.id);

    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to update clock element: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ClockRepository::deleteElement(qint64 elementId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM clock_elements WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), elementId);
    return query.exec();
}

bool ClockRepository::setElementOrder(const QVector<qint64>& elementIdsInOrder)
{
    QSqlDatabase db = Database::handle();
    QSqlQuery query(db);
    query.prepare(QStringLiteral("UPDATE clock_elements SET position = :pos WHERE id = :id"));

    db.transaction();
    for (int i = 0; i < elementIdsInOrder.size(); ++i) {
        query.bindValue(QStringLiteral(":pos"), i);
        query.bindValue(QStringLiteral(":id"), elementIdsInOrder.at(i));
        if (!query.exec()) {
            RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to reorder clock elements: %1").arg(query.lastError().text()));
            db.rollback();
            return false;
        }
    }
    db.commit();
    return true;
}

ClockWheelStateRecord ClockRepository::wheelStateFor(qint64 scheduleBlockId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT * FROM clock_wheel_state WHERE schedule_block_id = :id"));
    query.bindValue(QStringLiteral(":id"), scheduleBlockId);
    if (!query.exec() || !query.next())
        return {};
    return wheelStateFromQuery(query);
}

bool ClockRepository::saveWheelState(const ClockWheelStateRecord& state)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(R"(INSERT OR REPLACE INTO clock_wheel_state
        (schedule_block_id, clock_id, hour_start_epoch, position, items_done, updated_at)
        VALUES (:schedule_block_id, :clock_id, :hour_start_epoch, :position, :items_done, :updated_at))"));
    query.bindValue(QStringLiteral(":schedule_block_id"), state.scheduleBlockId);
    query.bindValue(QStringLiteral(":clock_id"), state.clockId);
    query.bindValue(QStringLiteral(":hour_start_epoch"), state.hourStartEpoch);
    query.bindValue(QStringLiteral(":position"), state.position);
    query.bindValue(QStringLiteral(":items_done"), state.itemsDone);
    query.bindValue(QStringLiteral(":updated_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    if (!query.exec()) {
        RS_LOG_ERROR("scheduler.clock", QStringLiteral("Failed to save clock wheel state: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool ClockRepository::clearWheelState(qint64 scheduleBlockId)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM clock_wheel_state WHERE schedule_block_id = :id"));
    query.bindValue(QStringLiteral(":id"), scheduleBlockId);
    return query.exec();
}

} // namespace radio::db
