#pragma once

#include <QColor>
#include <QString>

class QPainter;
class QRect;

namespace radio::ui {

// Shared paint logic for an "Audio object" pill/capsule — a rounded-rect
// background with centered text, used identically by AudioPillWidget (a
// real QWidget, for DeckWidget's single "now loaded" display) and
// AudioPillDelegate (a QStyledItemDelegate, for per-row painting in Library/
// Queue/Playlist Editor's item views). Kept as free functions with no state,
// same spirit as UiStyleHelpers, so both call sites stay pixel-identical
// without duplicating drawing code.
namespace AudioPillPainter {

// The single colour every Audio-object pill is drawn in — Library, Queue,
// and Playlist Editor alike. (There used to be a per-queue-entry random
// colour carried onto the deck; that feature was removed.)
extern const QColor kNeutralColor;

// Picks black or white text so it stays readable against an arbitrary
// background, via a standard relative-luminance threshold.
QColor contrastingTextColor(const QColor& background);

// Draws the pill: rounded-rect fill in `background`, centered `text` in a
// readable contrasting color, elided if it doesn't fit `rect`.
void paint(QPainter& painter, const QRect& rect, const QString& text, const QColor& background);

// Linear per-channel mix of `base` and `tint`, weighted `tintRatio` (0 =
// pure base, 1 = pure tint, clamped in between) toward tint. Used for
// DeckWidget's ambient background tint (10% of the loaded track's pill
// color mixed into the panel's normal background), kept here since it's a
// generically useful pill-color operation, not DeckWidget-specific logic.
QColor blend(const QColor& base, const QColor& tint, double tintRatio);

} // namespace AudioPillPainter

} // namespace radio::ui
