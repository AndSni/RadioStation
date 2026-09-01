#pragma once

#include <QString>

namespace radio::ui {

// The single "Artist — Title" convention used everywhere a track's artist
// and title are combined into one display string (deck, library, queue,
// playlist editor) -- falls back to bare title when artist is unknown.
inline QString formatTrackLabel(const QString& artist, const QString& title)
{
    return artist.isEmpty() ? title : QStringLiteral("%1 — %2").arg(artist, title);
}

} // namespace radio::ui
