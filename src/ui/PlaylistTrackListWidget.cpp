#include "PlaylistTrackListWidget.h"

#include <QDropEvent>
#include <QMimeData>

namespace radio::ui {

PlaylistTrackListWidget::PlaylistTrackListWidget(QWidget* parent)
    : QListWidget(parent)
{
}

QMimeData* PlaylistTrackListWidget::mimeData(const QList<QListWidgetItem*>& items) const
{
    QMimeData* mime = QListWidget::mimeData(items);
    if (items.isEmpty() || !mime)
        return mime;

    AudioObjectDragPayload payload;
    payload.trackId = items.first()->data(kTrackIdRole).toLongLong();
    payload.fromQueue = false;
    AudioObjectMime::encode(mime, payload);

    return mime;
}

void PlaylistTrackListWidget::dropEvent(QDropEvent* event)
{
    const AudioObjectDragPayload payload = AudioObjectMime::decode(event->mimeData());
    if (payload.trackId < 0) {
        event->ignore();
        return;
    }
    emit audioObjectDropped(payload);
    event->acceptProposedAction();
}

} // namespace radio::ui
