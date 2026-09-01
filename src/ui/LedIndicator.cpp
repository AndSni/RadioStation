#include "LedIndicator.h"
#include "ConsoleTheme.h"

#include <QPainter>
#include <algorithm>

namespace radio::ui {

namespace {
constexpr int kDotDiameter = 11;
constexpr int kBox = 18; // widget side -- leaves room for the glow rings
} // namespace

LedIndicator::LedIndicator(QWidget* parent)
    : QWidget(parent)
    , m_colour(theme::kLedRed)
{
    // A real minimum so a tight vertical layout can't collapse the lamp to
    // nothing (which is exactly what happened in the Crossfader dock).
    setMinimumSize(kBox, kBox);
}

void LedIndicator::setOn(bool on)
{
    if (m_on == on)
        return;
    m_on = on;
    update();
}

void LedIndicator::setColor(const QColor& colour)
{
    if (!colour.isValid() || m_colour == colour)
        return;
    m_colour = colour;
    update();
}

QSize LedIndicator::sizeHint() const
{
    return QSize(kBox, kBox);
}

void LedIndicator::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int side = std::min({ width(), height(), kBox });
    const qreal d = std::min(kDotDiameter, side);
    const QRectF dot((width() - d) / 2.0, (height() - d) / 2.0, d, d);

    if (m_on) {
        // Concentric glow -- same technique as IndicatorButton's LED.
        for (int ring = 3; ring >= 1; --ring) {
            QColor glow = m_colour;
            glow.setAlpha(55 / ring);
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            const int pad = ring * 3;
            painter.drawEllipse(dot.adjusted(-pad, -pad, pad, pad));
        }
    }

    // The lamp body -- bright when on, a visible dark-glass dome when off
    // (a real unlit LED is still clearly a physical component, not nothing).
    painter.setPen(QPen(theme::kLedRim, 1));
    painter.setBrush(m_on ? m_colour : m_colour.darker(240));
    painter.drawEllipse(dot);

    // Small specular highlight, upper-left.
    QColor spec(255, 255, 255, m_on ? 150 : 60);
    painter.setPen(Qt::NoPen);
    painter.setBrush(spec);
    const qreal sr = d * 0.18;
    painter.drawEllipse(QPointF(dot.center().x() - d * 0.18, dot.center().y() - d * 0.18), sr, sr);
}

} // namespace radio::ui
