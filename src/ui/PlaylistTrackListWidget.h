#pragma once

#include "AudioObjectMime.h"

#include <QListWidget>

namespace radio::ui {

// PlaylistEditorWidget's track list, as both a drag source (Playlist -> Deck
// or Playlist -> Queue, reference-only — never destructive, unlike
// Queue -> Deck) and a drop target (Library -> Playlist, adds to whichever
// playlist is currently selected). Unlike QueueListWidget, this list has no
// internal-reorder concept to preserve, so dropEvent() doesn't need to
// distinguish a self-drop from a foreign one — every drop is handled the
// same way, via the audioObjectDropped() signal.
class PlaylistTrackListWidget : public QListWidget {
    Q_OBJECT

public:
    static constexpr int kTrackIdRole = Qt::UserRole;
    static constexpr int kPlaylistItemIdRole = Qt::UserRole + 1;

    explicit PlaylistTrackListWidget(QWidget* parent = nullptr);

signals:
    void audioObjectDropped(const AudioObjectDragPayload& payload);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    void dropEvent(QDropEvent* event) override;
};

} // namespace radio::ui
