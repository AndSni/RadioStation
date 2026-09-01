#pragma once

#include <QSlider>

namespace radio::ui {

// A hand-painted mixing-console fader. Subclasses QSlider (not a bare
// QWidget like RotaryKnob) on purpose: it keeps QSlider's whole
// range/value/valueChanged API, its keyboard and wheel handling, and every
// existing `findChild<QSlider*>(...)` test lookup in MixerPanelWidgetTest --
// only the painting and the pointer hit-testing are replaced.
//
// Same house style as RoundButton / RotaryKnob / VuMeterWidget: everything
// is drawn in paintEvent() from ConsoleTheme colours, no QSS (the app sets
// no QApplication::setStyle(), so a styled groove/handle would render
// differently under each platform style -- see RoundButton.h).
//
// The look: a recessed dark groove with a faint carved edge, a short etched
// scale down one side (minor ticks from tickInterval(), majors at
// min/mid/max, a heavier detent tick at the midpoint when setCenterDetent()
// is on), and a chunky knurled cap (ConsoleTheme::paintConsoleCap()) with a
// single bright indicator line across it -- the part the eye tracks against
// the scale, exactly as on the reference hardware.
//
// Mouse: click anywhere in the groove jumps the cap there, then drag
// follows the pointer -- the default QSlider behaviour routes through
// QStyle sub-control rects that would not match this custom geometry, so
// press/move are reimplemented the same way RotaryKnob does it.
class ConsoleFader : public QSlider {
    Q_OBJECT

public:
    explicit ConsoleFader(Qt::Orientation orientation, QWidget* parent = nullptr);

    // Draws a heavier, wider tick at the range midpoint -- for the
    // zero-centred faders (EQ bands, master Bass/Treble) where "flat" is the
    // middle, not an end. Off by default.
    void setCenterDetent(bool enabled);
    bool centerDetent() const { return m_centerDetent; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

    // Geometry, exposed for tests (cap position must move monotonically with
    // value(); a groove click must land near the expected fraction).
    QRect grooveRect() const;
    QRect capRect() const;

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    // Maps a pixel position along the travel axis to a value and applies it.
    void setValueFromPos(const QPoint& pos);
    // 0..1 fraction of the way from the min end to the max end for `value`,
    // accounting for orientation and invertedAppearance().
    double valueFraction(int value) const;

    bool m_centerDetent = false;
    bool m_hovered = false;
};

} // namespace radio::ui
