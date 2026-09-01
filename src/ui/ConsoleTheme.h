#pragma once

#include <QColor>
#include <QString>

class QPainter;
class QRectF;

// Single source of truth for the analog-console look shared by every
// hand-painted primitive in this app (RoundButton, IndicatorButton,
// RotaryKnob, ConsoleFader, LcdReadout, the LED meters) plus the QGroupBox
// bezel. Before this module the same RGB triples were re-declared as private
// `namespace {}` constants in each of those files and had already drifted
// apart in places -- everything now reads from here so the console reads as
// one consistent material.
//
// Colours are `inline const QColor` (C++17 inline variables -- one shared
// definition across every TU). QColor's constructor isn't constexpr, so
// these can't be `constexpr`, but `inline` is enough to keep the ODR happy.
namespace radio::ui::theme {

// --- Chassis / bezel (the QGroupBox frame around each channel strip) ------
inline const QColor kBezelTop(0x2a, 0x26, 0x20);
inline const QColor kBezelBottom(0x1c, 0x1a, 0x16);
inline const QColor kBezelBorder(0x4a, 0x45, 0x3c);
inline const QColor kTitleText(0xcf, 0xc6, 0xb4);

// --- Cap material (fader caps, round button caps, knob bodies) -----------
// kCapMid == the old RoundButton/RotaryKnob kBodyColor(52,48,43) -- the caps
// grade from a lighter top to a darker bottom around that midpoint so a flat
// button and a gradient fader cap still read as the same plastic.
inline const QColor kCapTop(74, 69, 62);
inline const QColor kCapMid(52, 48, 43);
inline const QColor kCapBottom(33, 30, 26);
inline const QColor kCapEdgeLight(0x6b, 0x63, 0x55); // top bevel highlight (~ old kRimColor)
inline const QColor kCapEdgeDark(18, 16, 14); // bottom bevel shadow
inline const QColor kCapHover(66, 61, 54); // old kHoverColor
inline const QColor kCapPressed(38, 35, 31); // old kPressedColor
inline const QColor kRim(110, 102, 88); // old kRimColor -- knob/button outline
inline const QColor kCapText(230, 224, 212); // old kTextColor / knob pointer

// --- Fader groove + etched scale ---------------------------------------
inline const QColor kGrooveDark(8, 7, 6);
inline const QColor kGrooveLight(28, 26, 22);
inline const QColor kGrooveShadow(0, 0, 0);
inline const QColor kGrooveHighlight(70, 65, 56);
inline const QColor kScaleTick(0x8a, 0x83, 0x77);
inline const QColor kScaleTickMajor(0xb8, 0xae, 0x9a);
inline const QColor kPointerLine(0xf2, 0xee, 0xe2); // the bright cap indicator line

// --- LEDs / LCD (indicator dots, LcdReadout screen) --------------------
inline const QColor kLedRed(0xff, 0x3b, 0x3b); // "engaged / live" -- also LcdReadout digits
inline const QColor kLedRedDim(0x5c, 0x1c, 0x1c);
inline const QColor kLedRim(20, 18, 16);
inline const QColor kLcdScreen(10, 9, 8);
inline const QColor kLcdRim(60, 55, 48);
inline const QColor kLcdCaption(150, 140, 125);

// --- LED ladder meters (VuMeterWidget / LevelBarWidget via LedMeterPainter)
inline const QColor kMeterGreen(34, 197, 94);
inline const QColor kMeterAmber(234, 179, 8);
inline const QColor kMeterRed(220, 38, 38);
inline const QColor kMeterBackground(18, 17, 15);
inline const QColor kMeterBorder(90, 90, 90);

// App-wide QSS applied once in main() -- gives every ordinary (non-flat)
// QPushButton the warm console cap look, plus a matching toolbar/status-bar
// tone, so plain dialog/panel buttons stop looking like stock Qt next to
// the hand-painted console controls. The `:!flat` guard keeps it off the
// custom-painted ConsoleButton / RoundButton / IndicatorButton (all
// setFlat(true)), which draw themselves.
QString appStyleSheet();

// QSS for a channel-strip QGroupBox (was MixerPanelWidget::applyConsoleBezelStyle()).
QString consoleBezelStyle();

// The same warm metal bezel as consoleBezelStyle() but for a plain QFrame
// (no title) -- for panels that aren't a QGroupBox, e.g. RadioStatisticsPanel.
// `objectName` must match the frame's setObjectName() so the rule doesn't
// bleed onto child frames.
QString consolePanelStyle(const QString& objectName);

// QSS for a dark "LCD screen" frame -- kLcdScreen fill, kLcdRim border,
// small radius -- the box every LcdReadout / DotMatrixDisplay sits in,
// reused for the Radio Statistics clock. `objectName` scoped, as above.
QString lcdScreenFrameStyle(const QString& objectName);

// QSS for the small numeric scale labels beside a fader (was
// MixerSliderHelpers' kScaleLabelStyle) -- "color: #8a8377; font-size: 10px;".
QString scaleLabelStyle();

// Paints a physical mixer cap -- the metal/plastic body only (gradient,
// top-light/bottom-dark bevel edges, a faint side sheen, a few engraved
// knurl grooves, a soft drop shadow). Draws entirely within `rect`. Callers
// that need a marking on top of the cap (ConsoleFader's bright position
// line, a ConsoleButton's text) draw it themselves afterwards. `horizontal`
// only rotates the knurl grooves; the light source stays at the top for
// both orientations.
void paintConsoleCap(QPainter& painter, const QRectF& rect, bool hovered, bool pressed, bool horizontal);

} // namespace radio::ui::theme
