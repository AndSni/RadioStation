#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// Organizational grouping for playlists (e.g. a "Weekday Shows" folder
// containing several playlists) — purely a display/organization concept,
// unrelated to rotation_categories. All methods must run on the UI thread
// (see Database.h).
class PlaylistFolderRepository {
public:
    static QVector<PlaylistFolderRecord> allFolders(); // ORDER BY position ASC
    static qint64 createFolder(const QString& name);
    static bool renameFolder(qint64 folderId, const QString& newName);

    // Clears folder_id on member playlists (SQLite FK enforcement is off,
    // see Migrations.cpp — ON DELETE SET NULL is documentation, not live
    // behavior) — does NOT delete the playlists themselves.
    static bool deleteFolder(qint64 folderId);

    static bool setFolderOrder(const QVector<qint64>& folderIdsInOrder);
};

} // namespace radio::db
