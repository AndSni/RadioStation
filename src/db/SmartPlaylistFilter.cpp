#include "SmartPlaylistFilter.h"

#include <QJsonArray>
#include <QJsonValue>

namespace radio::db {

bool matchesSmartPlaylistFilter(const TrackRecord& track, const QJsonObject& filter)
{
    if (filter.contains(QStringLiteral("genre")) && !filter.value(QStringLiteral("genre")).toString().isEmpty()) {
        const QString genre = filter.value(QStringLiteral("genre")).toString();
        if (!track.genre.contains(genre, Qt::CaseInsensitive))
            return false;
    }

    if (filter.contains(QStringLiteral("categoryIds"))) {
        const QJsonArray categoryIds = filter.value(QStringLiteral("categoryIds")).toArray();
        if (!categoryIds.isEmpty()) {
            bool matched = false;
            for (const QJsonValue& value : categoryIds) {
                if (value.toVariant().toLongLong() == track.categoryId) {
                    matched = true;
                    break;
                }
            }
            if (!matched)
                return false;
        }
    }

    if (filter.contains(QStringLiteral("yearMin")) && track.year < filter.value(QStringLiteral("yearMin")).toInt())
        return false;
    if (filter.contains(QStringLiteral("yearMax")) && track.year > filter.value(QStringLiteral("yearMax")).toInt())
        return false;

    if (filter.contains(QStringLiteral("energyMin")) || filter.contains(QStringLiteral("energyMax"))) {
        if (track.energy < 0)
            return false;
        if (filter.contains(QStringLiteral("energyMin")) && track.energy < filter.value(QStringLiteral("energyMin")).toDouble())
            return false;
        if (filter.contains(QStringLiteral("energyMax")) && track.energy > filter.value(QStringLiteral("energyMax")).toDouble())
            return false;
    }

    if (filter.contains(QStringLiteral("bpmMin")) || filter.contains(QStringLiteral("bpmMax"))) {
        if (track.bpm < 0)
            return false;
        if (filter.contains(QStringLiteral("bpmMin")) && track.bpm < filter.value(QStringLiteral("bpmMin")).toDouble())
            return false;
        if (filter.contains(QStringLiteral("bpmMax")) && track.bpm > filter.value(QStringLiteral("bpmMax")).toDouble())
            return false;
    }

    return true;
}

} // namespace radio::db
