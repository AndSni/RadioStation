#pragma once

#include <QWidget>

namespace radio::ui {

// A hand-painted rotary knob -- the button-stack equivalent of a linear
// fader for a control that belongs beside the toggle buttons rather than
// in the main slider row (Mixer's deck Volume trim, Mic Gain, Ducking
// Amount -- see MixerPanelWidget's own construction for why those three
// specifically). Same house style as RoundButton/VuMeterWidget: a plain
// custom-painted QWidget, no QSS.
//
// Interaction is vertical-drag-to-change, the standard software-knob
// convention (a knob has no natural 2D drag gesture the way a slider's
// thumb does) -- drag up increases the value, drag down decreases it,
// dragging the full widget height sweeps the full range.
class RotaryKnob : public QWidget {
    Q_OBJECT

public:
    RotaryKnob(int minValue, int maxValue, int defaultValue, QWidget* parent = nullptr);

    int value() const { return m_value; }
    void setValue(int value);

    QSize sizeHint() const override;

signals:
    void valueChanged(int value);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    int m_min;
    int m_max;
    int m_value;
    bool m_hovered = false;
    bool m_dragging = false;
    int m_dragStartY = 0;
    int m_dragStartValue = 0;
};

} // namespace radio::ui
