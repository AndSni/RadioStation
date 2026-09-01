#pragma once

#include <QMetaType>
#include <QString>

class QMimeData;

namespace radio::ui {

// A dragged Audio object carries only ids — a drop handler always re-reads
// current DB state via TrackRepository::trackById() rather than trusting
// stale drag-time display data (title/artist/color can all have changed, or
// the track could have been deleted, between drag start and drop).
struct AudioObjectDragPayload {
    qint64 trackId = -1;
    qint64 playlistItemId = -1; // -1 unless fromQueue
    // Marks the one destructive source/target pairing: Queue -> Deck
    // removes the item from the queue on drop. Every other pairing (Library
    // or Playlist as the source) is a plain reference add/load, nothing
    // removed from where it came from.
    bool fromQueue = false;
};

namespace AudioObjectMime {

extern const QString kMimeType;

// Sets kMimeType's payload on `mimeData` (already-allocated, e.g. one a
// QListWidget subclass's mimeData() override got from the base
// implementation and wants to layer this onto, alongside whatever Qt's own
// internal-move handling already put there).
void encode(QMimeData* mimeData, const AudioObjectDragPayload& payload);

// trackId == -1 in the result if `mimeData` is null or doesn't carry
// kMimeType (or its payload is malformed) — always check that before acting
// on the rest of the payload.
AudioObjectDragPayload decode(const QMimeData* mimeData);

} // namespace AudioObjectMime

} // namespace radio::ui

Q_DECLARE_METATYPE(radio::ui::AudioObjectDragPayload)
