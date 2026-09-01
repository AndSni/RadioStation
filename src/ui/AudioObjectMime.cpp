#include "AudioObjectMime.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QMimeData>

namespace radio::ui::AudioObjectMime {

const QString kMimeType = QStringLiteral("application/x-radiostation-audio-object");

void encode(QMimeData* mimeData, const AudioObjectDragPayload& payload)
{
    QByteArray bytes;
    QDataStream stream(&bytes, QIODevice::WriteOnly);
    stream << payload.trackId << payload.playlistItemId << payload.fromQueue;
    mimeData->setData(kMimeType, bytes);
}

AudioObjectDragPayload decode(const QMimeData* mimeData)
{
    AudioObjectDragPayload payload;
    if (!mimeData || !mimeData->hasFormat(kMimeType))
        return payload;

    QByteArray bytes = mimeData->data(kMimeType);
    QDataStream stream(&bytes, QIODevice::ReadOnly);
    stream >> payload.trackId >> payload.playlistItemId >> payload.fromQueue;
    if (stream.status() != QDataStream::Ok)
        return {};
    return payload;
}

} // namespace radio::ui::AudioObjectMime
