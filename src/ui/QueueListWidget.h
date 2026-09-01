#pragma once

#include "AudioObjectMime.h"

#include <QListWidget>

namespace radio::ui {

// QListWidget::InternalMove (self-reorder only) doesn't allow dragging an
// item out to a different widget; plain QListWidget::DragDrop allows
// drag-out but doesn't, on its own, carry the payload a foreign drop target
// (DeckWidget) needs to identify what was dragged. This subclass bridges
// the two: mimeData() starts from the base class's own implementation
// (whatever standard payload Qt's internal reorder handling already relies
// on for a self-drop) and layers a custom AudioObjectMime payload on top of
// the SAME QMimeData object, so a self-drop still reorders normally while
// an external drop target can read the custom payload instead. See
// QueueWidget, which sets DragDropMode::DragDrop + this class together;
// AudioObjectMime.h for the payload format; DeckWidget::dropEvent() for the
// drop-target side of the Queue -> Deck pairing.
//
// dropEvent() handles the OTHER direction — Library/Playlist -> Queue: a
// self-drop (event->source() == this) is a reorder, handed to the base
// class's own handling unchanged; a foreign drop emits audioObjectDropped()
// instead of letting the base class try (and likely fail, or mis-handle) to
// interpret a payload it doesn't understand.
class QueueListWidget : public QListWidget {
    Q_OBJECT

public:
    // Item-data roles QueueWidget::refresh() populates and mimeData() (and
    // DeckWidget's drop handler, via the decoded AudioObjectMime payload)
    // reads back. Shared here rather than file-local constants so both
    // sides can't silently drift apart.
    static constexpr int kTrackIdRole = Qt::UserRole;
    static constexpr int kPlaylistItemIdRole = Qt::UserRole + 1;
    static constexpr int kColorRole = Qt::UserRole + 2; // vestigial -- the per-entry random pill colour feature was removed

    explicit QueueListWidget(QWidget* parent = nullptr);

signals:
    void audioObjectDropped(const AudioObjectDragPayload& payload);

protected:
    QMimeData* mimeData(const QList<QListWidgetItem*>& items) const override;
    void dropEvent(QDropEvent* event) override;
};

} // namespace radio::ui
