#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// Manages the single "Queue" playlist (playlists.is_queue = 1) — the
// upcoming-tracks queue that feeds the decks — plus a general multi-
// playlist system (playlists.is_queue = 0): a track can belong to many
// playlists at once, independent of its single rotation_categories
// membership (an unrelated, flat classification used only for the
// category-rotation picker). All methods must run on the UI thread (see
// Database.h).
class PlaylistRepository {
public:
    // --- Queue-specific surface, used by QueueWidget/AutoDjEngine/
    // CrossfadeController. Internally thin wrappers around the generic
    // item operations below, scoped to ensureQueuePlaylistId(). ---
    static QVector<QueueItemRecord> queueItems();

    // source is "manual" (added from the library browser) or "autodj".
    static bool appendToQueue(qint64 trackId, const QString& source);

    // Removes every pending queue entry (does not touch whatever's already
    // loaded on a deck — loading a track already removes it from the queue,
    // see CrossfadeController::maybeLoadNextFromQueue()). Used when Auto DJ
    // fully resumes after a manual interruption, so a stale queue doesn't
    // linger — see AutoDjEngine::resetQueue().
    static bool clearQueue();

    // --- Generic playlist CRUD. Excludes the is_queue=1 row entirely. ---
    static QVector<PlaylistRecord> listPlaylists();
    static qint64 createPlaylist(const QString& name, qint64 folderId = -1);
    static bool renamePlaylist(qint64 playlistId, const QString& newName);

    // Snapshots a set of tracks (e.g. the current smart-playlist filter
    // results) into a brand-new static playlist, in the given order --
    // "Create Playlist from these results", the one-shot counterpart to
    // saving the filter itself as a re-evaluated smart playlist. Items are
    // recorded with source "import" (see PlaylistItemRecord::source).
    static qint64 createPlaylistFromTracks(
        const QString& name, const QVector<qint64>& trackIds, qint64 folderId = -1);

    // Manually cascades playlist_items (SQLite FK enforcement is off, see
    // Migrations.cpp — ON DELETE CASCADE in the schema is documentation,
    // not live behavior). No-op returning false for the queue playlist.
    static bool deletePlaylist(qint64 playlistId);

    // folderId == -1 clears the assignment (playlist becomes ungrouped).
    static bool setPlaylistFolder(qint64 playlistId, qint64 folderId);

    // --- Generic playlist-item operations, valid for any playlist
    // (including the queue — queueItems()/appendToQueue() above are just
    // fixed-playlist-id convenience wrappers over these). ---
    static QVector<PlaylistItemRecord> itemsForPlaylist(qint64 playlistId);
    static bool addTrackToPlaylist(qint64 playlistId, qint64 trackId, const QString& source);

    // item ids (playlist_items.id) are globally unique, so no playlistId
    // scoping is needed for either of these.
    static bool removeItem(qint64 playlistItemId);
    static bool setItemOrder(const QVector<qint64>& playlistItemIdsInOrder);

private:
    static qint64 ensureQueuePlaylistId();
};

} // namespace radio::db
