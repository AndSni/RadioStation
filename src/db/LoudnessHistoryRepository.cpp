#include "LoudnessHistoryRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

void LoudnessHistoryRepository::recordMeasurement(
    double integratedLufs, double momentaryLufs, double shortTermLufs, double truePeakDbfs)
{
    QSqlQuery insert(Database::handle());
    insert.prepare(QStringLiteral(
        "INSERT INTO loudness_history (measured_at, integrated_lufs, momentary_lufs, short_term_lufs, true_peak_dbfs) "
        "VALUES (:measured_at, :integrated, :momentary, :short_term, :true_peak)"));
    insert.bindValue(QStringLiteral(":measured_at"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    insert.bindValue(QStringLiteral(":integrated"), integratedLufs);
    insert.bindValue(QStringLiteral(":momentary"), momentaryLufs);
    insert.bindValue(QStringLiteral(":short_term"), shortTermLufs);
    insert.bindValue(QStringLiteral(":true_peak"), truePeakDbfs);
    if (!insert.exec())
        RS_LOG_ERROR("audio.pipeline", QStringLiteral("Failed to record loudness history: %1").arg(insert.lastError().text()));
}

} // namespace radio::db
