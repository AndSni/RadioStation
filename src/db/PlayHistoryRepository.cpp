#include "PlayHistoryRepository.h"
#include "Database.h"

#include "core/Logging.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

void PlayHistoryRepository::recordPlay(qint64 trackId, const QString& deck)
{
    QSqlDatabase db = Database::handle();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral("INSERT INTO play_history (track_id, played_at, deck) VALUES (:tid, :played_at, :deck)"));
    insert.bindValue(QStringLiteral(":tid"), trackId);
    insert.bindValue(QStringLiteral(":played_at"), now);
    insert.bindValue(QStringLiteral(":deck"), deck);
    if (!insert.exec())
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Failed to record play history: %1").arg(insert.lastError().text()));

    QSqlQuery update(db);
    update.prepare(
        QStringLiteral("UPDATE tracks SET last_played_at = :played_at, play_count = play_count + 1 WHERE id = :tid"));
    update.bindValue(QStringLiteral(":played_at"), now);
    update.bindValue(QStringLiteral(":tid"), trackId);
    update.exec();
}

QSet<qint64> PlayHistoryRepository::recentTrackIds(int windowCount)
{
    QSet<qint64> result;
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral("SELECT track_id FROM play_history ORDER BY played_at DESC LIMIT :n"));
    query.bindValue(QStringLiteral(":n"), windowCount);
    if (query.exec()) {
        while (query.next())
            result.insert(query.value(0).toLongLong());
    }
    return result;
}

QSet<QString> PlayHistoryRepository::recentArtists(int windowCount)
{
    QSet<QString> result;
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(R"(
        SELECT t.artist FROM play_history ph JOIN tracks t ON t.id = ph.track_id
        ORDER BY ph.played_at DESC LIMIT :n)"));
    query.bindValue(QStringLiteral(":n"), windowCount);
    if (query.exec()) {
        while (query.next()) {
            const QString artist = query.value(0).toString();
            if (!artist.isEmpty())
                result.insert(artist);
        }
    }
    return result;
}

} // namespace radio::db
