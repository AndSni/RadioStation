#pragma once

#include <QString>

namespace radio::ui {

// Style for a checkable QPushButton that needs an unmistakable "this is ON"
// state — a real mixer's MUTE/PFL buttons light up distinctly when engaged,
// they don't rely on a subtle sunken-look alone (see feedback that led to
// this: active buttons need a green border, not just their default checked
// look). Shared so every such button (Mixer's Auto dB Comp/Loudness, the
// Auto DJ master switch, ...) looks and feels consistent.
QString checkableActiveButtonStyle();

// Family name of the embedded LCD-style TrueType font (resources/fonts/
// lcd_att_phone_time_date.ttf, registered via resources.qrc), or an empty
// string if the resource somehow failed to load, in which case callers fall
// back to whatever font they already had. Loaded and cached exactly once
// per process (QFontDatabase::addApplicationFont() is safe to call
// repeatedly but pointless to) — shared so every "real LCD screen" surface
// in this app (RadioStatisticsPanel's studio clock, LcdReadout's meter
// readouts) uses the exact same registered font instead of each loading its
// own copy.
QString lcdFontFamily();


} // namespace radio::ui
