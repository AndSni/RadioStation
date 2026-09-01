#include "CartRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

QVector<CartClipRecord> CartRepository::allClips()
{
    QVector<CartClipRecord> result;
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral(
        "SELECT id, file_path, label, color, slot_row, slot_col, hotkey, duration_ms, in_between_only FROM cart_clips "
        "ORDER BY slot_row, slot_col"));
    while (query.next()) {
        CartClipRecord clip;
        clip.id = query.value(0).toLongLong();
        clip.filePath = query.value(1).toString();
        clip.label = query.value(2).toString();
        clip.color = query.value(3).toString();
        clip.slotRow = query.value(4).toInt();
        clip.slotCol = query.value(5).toInt();
        clip.hotkey = query.value(6).toString();
        clip.durationMs = query.value(7).toLongLong();
        clip.inBetweenOnly = query.value(8).toBool();
        result.append(clip);
    }
    return result;
}

qint64 CartRepository::addClip(
    const QString& filePath, const QString& label, const QString& color, int row, int col, const QString& hotkey,
    qint64 durationMs, bool inBetweenOnly)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "INSERT INTO cart_clips (file_path, label, color, slot_row, slot_col, hotkey, duration_ms, in_between_only) "
        "VALUES (:path, :label, :color, :row, :col, :hotkey, :dur, :inbetween)"));
    query.bindValue(QStringLiteral(":path"), filePath);
    query.bindValue(QStringLiteral(":label"), label);
    query.bindValue(QStringLiteral(":color"), color);
    query.bindValue(QStringLiteral(":row"), row);
    query.bindValue(QStringLiteral(":col"), col);
    query.bindValue(QStringLiteral(":hotkey"), hotkey);
    query.bindValue(QStringLiteral(":dur"), durationMs);
    query.bindValue(QStringLiteral(":inbetween"), inBetweenOnly ? 1 : 0);
    if (!query.exec()) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Add cart clip failed: %1").arg(query.lastError().text()));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

bool CartRepository::updateClip(const CartClipRecord& clip)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "UPDATE cart_clips SET label = :label, color = :color, slot_row = :row, slot_col = :col, "
        "hotkey = :hotkey, in_between_only = :inbetween WHERE id = :id"));
    query.bindValue(QStringLiteral(":label"), clip.label);
    query.bindValue(QStringLiteral(":color"), clip.color);
    query.bindValue(QStringLiteral(":row"), clip.slotRow);
    query.bindValue(QStringLiteral(":col"), clip.slotCol);
    query.bindValue(QStringLiteral(":hotkey"), clip.hotkey);
    query.bindValue(QStringLiteral(":inbetween"), clip.inBetweenOnly ? 1 : 0);
    query.bindValue(QStringLiteral(":id"), clip.id);
    if (!query.exec()) {
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Update cart clip failed: %1").arg(query.lastError().text()));
        return false;
    }
    return true;
}

bool CartRepository::removeClip(qint64 id)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("DELETE FROM cart_clips WHERE id = :id"));
    query.bindValue(QStringLiteral(":id"), id);
    return query.exec();
}

} // namespace radio::db
