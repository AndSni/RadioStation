#pragma once

class QPainter;
class QRect;

namespace radio::ui {

// Width of a single meter channel -- VuMeterWidget's own L/R split (its
// 28px sizeHint minus a 4px inter-channel gap, halved) -- shared so a
// single-value meter like LevelBarWidget renders at the same width as one
// channel of a stereo VU meter, not an independently-guessed size.
constexpr int kMeterChannelWidth = 12;

// Shared rendering for every bar-style meter in the Mixer panel
// (VuMeterWidget's stereo peak bars, LevelBarWidget's mono dB bars) --
// discrete LED segments (dark background, small gaps between lit/unlit
// blocks, a soft glow behind each lit one) rather than one continuous
// filled rect, matching a real hardware meter's look. A free function, not
// a widget of its own -- both callers already own their widget's rect/
// decay/value-tracking logic and just need "paint the bar" factored out so
// the two don't duplicate this segment/color/glow logic independently.
//
// normalizedLevel: 0..1 (values are clamped by each caller before this is
// reached). clipped: true forces every segment lit red (VuMeterWidget's own
// clip-threshold latch; LevelBarWidget never passes true, it has no
// separate clip concept beyond simply reaching the top of its own range).
void paintLedMeterBar(QPainter& painter, const QRect& rect, float normalizedLevel, bool clipped);

} // namespace radio::ui
