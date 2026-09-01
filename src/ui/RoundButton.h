#pragma once

#include <QPushButton>

namespace radio::ui {

// A circular pushbutton, hand-painted rather than styled via QSS
// border-radius — this app sets no QApplication::setStyle(), so it renders
// under whatever native platform style is present locally (a different
// renderer than CI's offscreen/Fusion fallback), and a styled QPushButton's
// square backing store/hit-region risks leftover corner pixels depending on
// how that native style handles a stylesheet corner radius. Painting the
// circle directly (same house style as VuMeterWidget) sidesteps that
// entirely and is the only way to guarantee the same look everywhere.
//
// Used directly for a momentary/action button (no on/off state of its own,
// e.g. Mixer's "Flat" reset) — see IndicatorButton for the checkable,
// LED-lit variant built on top of this one. Deliberately painted with NO
// text inside the cap by convention (real console switches are usually
// unlabeled captionless caps, with the function printed on the panel beside
// them) — see MixerSliderHelpers::wrapWithCaption() for that label. The
// text-painting code stays here (harmless, and this class doesn't forbid a
// caller passing real button text) purely so a future caller isn't blocked
// from it.
class RoundButton : public QPushButton {
    Q_OBJECT

public:
    explicit RoundButton(const QString& text = QString(), QWidget* parent = nullptr);

    QSize sizeHint() const override;

protected:
    // The circle's diameter and its inset from the enclosing square --
    // exposed so IndicatorButton's own (taller, LED-above-button) bodyRect()
    // override can compute a circle that's pixel-identical in size to this
    // class's own, rather than an independently-guessed value silently
    // drifting out of sync (see IndicatorButton's own doc comment for why
    // that actually happened once already: its circle used to be smaller
    // than this class's, shrunk to fit the LED in its own corner instead of
    // giving the LED separate room).
    static constexpr int kDiameter = 30; // 1/3 smaller than the original 44 -- also RotaryKnob's own diameter, for a consistent button-stack rhythm
    static constexpr int kBodyInset = 2;

    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

    // The square region the circular body is painted within, inset from
    // this widget's own rect. IndicatorButton overrides this to anchor the
    // same-size circle to the bottom of its own (taller) rect instead —
    // every other part of the painting (body fill, rim, hover/pressed/
    // disabled state, text) is shared as-is via paintEvent()'s virtual
    // dispatch to this method.
    virtual QRect bodyRect() const;

private:
    bool m_hovered = false;
};

} // namespace radio::ui
