#include "QueueListWidget.h"
#include "AudioObjectMime.h"

#include <QDropEvent>
#include <QMimeData>

namespace radio::ui {

QueueListWidget::QueueListWidget(QWidget* parent)
    : QListWidget(parent)
{
}

QMimeData* QueueListWidget::mimeData(const QList<QListWidgetItem*>& items) const
{
    QMimeData* mime = QListWidget::mimeData(items);
    if (items.isEmpty() || !mime)
        return mime;

    // A multi-selection drag only carries the first item's identity in the
    // custom payload — Qt's own reorder handling still moves every selected
    // row correctly via its own payload; a foreign drop target (a Deck)
    // only ever wants a single track anyway.
    AudioObjectDragPayload payload;
    payload.trackId = items.first()->data(kTrackIdRole).toLongLong();
    payload.playlistItemId = items.first()->data(kPlaylistItemIdRole).toLongLong();
    payload.fromQueue = true;
    AudioObjectMime::encode(mime, payload);

    return mime;
}

void QueueListWidget::dropEvent(QDropEvent* event)
{
    if (event->source() == this) {
        QListWidget::dropEvent(event); // self-drop - Qt's own reorder handling
        return;
    }

    const AudioObjectDragPayload payload = AudioObjectMime::decode(event->mimeData());
    if (payload.trackId < 0) {
        event->ignore();
        return;
    }
    emit audioObjectDropped(payload);
    event->acceptProposedAction();
}

} // namespace radio::ui
