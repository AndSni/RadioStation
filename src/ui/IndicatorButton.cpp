#include "IndicatorButton.h"
#include "ConsoleTheme.h"

#include <QPainter>

namespace radio::ui {

namespace {
constexpr int kLedDiameter = 8; // scaled down alongside RoundButton::kDiameter's own 1/3 shrink
constexpr int kLedAreaHeight = 14; // strip above the button the LED sits in, including its own top/bottom margin

// Same on/off red pair OnAirLabel uses (0xff3b3b lit / 0x5c1c1c dim) -- this
// app's existing "red = engaged/live" language, reused rather than invented
// fresh.
const QColor kLedOnColor = theme::kLedRed;
const QColor kLedOffColor = theme::kLedRedDim;
const QColor kLedRimColor = theme::kLedRim;
}

IndicatorButton::IndicatorButton(const QString& text, QWidget* parent)
    : RoundButton(text, parent)
{
    setCheckable(true);
    connect(this, &QAbstractButton::toggled, this, &IndicatorButton::onToggled);
}

void IndicatorButton::onToggled(bool)
{
    update();
}

QSize IndicatorButton::sizeHint() const
{
    return QSize(kDiameter, kDiameter + kLedAreaHeight);
}

QRect IndicatorButton::bodyRect() const
{
    // Same diameter/inset RoundButton itself uses (see this class's own
    // doc comment for why that match matters) -- anchored to the bottom of
    // this widget's own (taller) rect, leaving the LED strip's room above
    // it untouched rather than carved out of the circle.
    const int side = kDiameter - 2 * kBodyInset;
    const int left = (width() - side) / 2;
    const int top = height() - side - kBodyInset;
    return QRect(left, top, side, side);
}

void IndicatorButton::paintEvent(QPaintEvent* event)
{
    RoundButton::paintEvent(event); // paints the circular body via bodyRect()'s override above

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect ledRect((width() - kLedDiameter) / 2, (kLedAreaHeight - kLedDiameter) / 2, kLedDiameter, kLedDiameter);
    const bool on = isChecked();

    if (on) {
        // Concentric glow: a few decreasing-opacity rings around the dot,
        // cheapest possible stand-in for a real glow with no extra widgets
        // or QGraphicsEffect involved (see this class's own doc comment).
        for (int ring = 3; ring >= 1; --ring) {
            QColor glow = kLedOnColor;
            glow.setAlpha(45 / ring);
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            const int pad = ring * 3;
            painter.drawEllipse(ledRect.adjusted(-pad, -pad, pad, pad));
        }
    }

    painter.setPen(QPen(kLedRimColor, 1));
    painter.setBrush(on ? kLedOnColor : kLedOffColor);
    painter.drawEllipse(ledRect);
}

} // namespace radio::ui
