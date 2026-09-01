#pragma once

#include <QSet>
#include <QString>

namespace radio::db {

// All methods must run on the UI thread (see Database.h).
class PlayHistoryRepository {
public:
    static void recordPlay(qint64 trackId, const QString& deck);

    // Most-recently-played windowCount distinct track ids / artists, used by
    // the Auto-DJ scheduler's no-repeat window.
    static QSet<qint64> recentTrackIds(int windowCount);
    static QSet<QString> recentArtists(int windowCount);
};

} // namespace radio::db
