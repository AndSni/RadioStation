#pragma once

#include "RoundButton.h"

namespace radio::ui {

// A checkable RoundButton with a small LED in its own centered strip above
// the button -- dark/unlit while unchecked, glowing red while checked.
// Every real toggle in the Mixer panel (Loudness, High-Pass, Leveler, Auto
// dB Comp, Mic Enable, Ducking) uses this instead of a plain checkable
// QPushButton.
//
// The LED sits ABOVE the button, in a dedicated strip this widget is taller
// than RoundButton to make room for -- not carved out of the button's own
// circle. An earlier version shrank the circle into one corner to fit the
// LED inside the same square footprint as RoundButton, which meant this
// class's buttons rendered visibly SMALLER than plain RoundButtons (e.g.
// Mixer's "Flat") sitting right next to them in the same button column,
// even though both claimed the same QSize. bodyRect() below instead
// reuses RoundButton::kDiameter/kBodyInset directly, so the circle is
// pixel-identical in size regardless of whether a button has an LED --
// only the caller's column gets a little taller to fit the LED strip, the
// button itself never shrinks.
//
// The glow is painted directly (concentric semi-transparent circles around
// the LED dot) rather than via QGraphicsDropShadowEffect the way OnAirLabel's
// ON AIR indicator does -- that's a single instance app-wide; this panel
// puts one of these on every toggle simultaneously, and a QGraphicsEffect
// forces its own offscreen rasterized backing store per instance, a real,
// avoidable cost at that multiplicity. The glow is also deliberately
// steady, not pulsing -- a toggle a user sets and leaves, not a
// continuously-significant state like ON AIR -- so there was no animation
// to justify the extra machinery in the first place.
//
// Still a genuine QPushButton subclass throughout (via RoundButton) -- every
// existing setCheckable()/setChecked()/isChecked()/toggled call-site pattern
// works completely unchanged, and findChild<QPushButton*>("objectName")
// still resolves it (qobject_cast walks the metaobject chain), which is
// exactly what MixerPanelWidgetTest.cpp's persistence assertions rely on.
class IndicatorButton : public RoundButton {
    Q_OBJECT

public:
    explicit IndicatorButton(const QString& text = QString(), QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    QRect bodyRect() const override;
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onToggled(bool checked);
};

} // namespace radio::ui
