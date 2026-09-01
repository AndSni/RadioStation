#pragma once

#include <QString>

class QWidget;
class QSlider;

namespace radio::ui {

class LcdReadout;

// Shared fixed width for EVERY column in a Mixer channel strip -- every
// slider column, the VU meter column, and every button column are all
// pinned to exactly this width (see each helper below), so the whole strip
// reads as one disciplined grid instead of each control floating at
// whatever width its own content happens to need.
constexpr int kMixerColumnWidth = 64;

// Shared helpers so every fader/EQ slider in MixerPanelWidget looks and
// behaves consistently.

// A vertical slider with a fixed (narrow) width but an EXPANDING height
// size policy, so it grows to fill whatever vertical space its containing
// panel has ("sliders should fill all available space") rather than
// sitting at one fixed pixel height. zeroCenteredTicks places a tick mark
// at the slider's min/0/max — a native, correctly-positioned "where's the
// middle" reference (see makeScaledColumn() for the text labels that
// complement it).
QSlider* makeExpandingSlider(int minValue, int maxValue, int defaultValue, QWidget* parent, bool zeroCenteredTicks);

// Wraps a row-1 control (slider or VU meter) as one column: a small label
// showing the control's maximum value above it, the control itself
// (expanding to fill available height), a label showing its minimum value
// below it, and a name label under that. This approximates the printed
// numeric scale beside a real mixer's faders (e.g. a graphic EQ or channel
// strip) without needing pixel-perfect custom painting — the two value
// labels sit outside the control's own rect, so there's no alignment
// guesswork, and the middle ("0") reference comes from the slider's own
// native tick mark instead of a third floating label.
QWidget* makeScaledColumn(
    QWidget* control, const QString& maxLabel, const QString& minLabel, const QString& name, QWidget* parent);

// The pair returned by makeScaledColumnWithReadout() below -- the caller
// needs both the assembled container (to add to a layout) and the readout
// itself (to wire a slider's valueChanged into, via setValue()).
struct ScaledColumn {
    QWidget* container;
    LcdReadout* readout;
};

// Same shape as makeScaledColumn() above, but the static name caption at
// the bottom is replaced by a live LcdReadout — set once via
// LcdReadout::setCaption() (the frequency/name, e.g. "60Hz"/"Vol") and then
// updated by the caller as the control's value changes (LcdReadout::setValue()).
// Used for every slider that has a live value worth showing (i.e. every
// slider in the analog-console rework except the VU meters, which keep
// using the plain makeScaledColumn() above — a meter bar isn't a value
// needing digits).
ScaledColumn makeScaledColumnWithReadout(
    QWidget* control, const QString& maxLabel, const QString& minLabel, const QString& caption, QWidget* parent);

// Stacks `control` above a small static caption label beneath it — the
// button-row equivalent of makeScaledColumn()'s bottom name label, giving
// every RoundButton/IndicatorButton toggle in the Mixer panel the same
// "control, then its printed label" grammar sliders already have (real
// console hardware silkscreens a switch's function on the panel beside it
// rather than putting text on the switch cap itself — see RoundButton's own
// doc comment).
QWidget* wrapWithCaption(QWidget* control, const QString& caption, QWidget* parent);

} // namespace radio::ui
