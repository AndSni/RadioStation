#include "RotationCategoryRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

QVector<RotationCategoryRecord> RotationCategoryRepository::allCategories()
{
    QVector<RotationCategoryRecord> result;
    QSqlQuery query(Database::handle());
    query.exec(QStringLiteral("SELECT id, name, color, target_ratio FROM rotation_categories ORDER BY name"));
    while (query.next()) {
        RotationCategoryRecord record;
        record.id = query.value(0).toLongLong();
        record.name = query.value(1).toString();
        record.color = query.value(2).toString();
        record.targetRatio = query.value(3).toInt();
        result.append(record);
    }
    return result;
}

qint64 RotationCategoryRepository::addCategory(const QString& name, const QString& color, int targetRatio)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "INSERT INTO rotation_categories (name, color, target_ratio) VALUES (:name, :color, :ratio)"));
    query.bindValue(QStringLiteral(":name"), name);
    query.bindValue(QStringLiteral(":color"), color);
    query.bindValue(QStringLiteral(":ratio"), targetRatio);
    if (!query.exec()) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Add category failed: %1").arg(query.lastError().text()));
        return -1;
    }
    return query.lastInsertId().toLongLong();
}

} // namespace radio::db
