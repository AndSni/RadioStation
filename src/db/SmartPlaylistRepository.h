#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// CRUD for saved Type/Genre/Era/Energy/BPM smart-playlist queries. All
// methods must run on the UI thread (see Database.h).
class SmartPlaylistRepository {
public:
    static QVector<SmartPlaylistRecord> allSmartPlaylists(); // ORDER BY name

    // Default-constructed record (id == -1) if no such smart playlist exists,
    // matching this codebase's existing id == -1 "not found" convention.
    static SmartPlaylistRecord byId(qint64 id);

    static qint64 create(const QString& name, const QString& filterJson);
    static bool update(qint64 id, const QString& name, const QString& filterJson);

    // Clears smart_playlist_id on any schedule block targeting this smart
    // playlist (SQLite FK enforcement is off, see Migrations.cpp — ON
    // DELETE SET NULL in the schema is documentation, not live behavior).
    static bool remove(qint64 id);
};

} // namespace radio::db
