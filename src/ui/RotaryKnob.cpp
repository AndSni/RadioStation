#include "RotaryKnob.h"
#include "ConsoleTheme.h"

#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>

namespace radio::ui {

namespace {
constexpr int kDiameter = 30; // matches RoundButton's own shrunk (1/3 smaller) diameter, see its own comment
constexpr int kBodyInset = 2;
constexpr int kSweepDegrees = 270; // -135deg (min) to +135deg (max), straight up = the range's midpoint
constexpr int kDragPixelsForFullSweep = 150; // dragging this many px vertically covers the whole min..max range
constexpr int kKnurlCount = 24; // engraved grip ticks around the skirt
}

RotaryKnob::RotaryKnob(int minValue, int maxValue, int defaultValue, QWidget* parent)
    : QWidget(parent)
    , m_min(minValue)
    , m_max(maxValue)
    , m_value(std::clamp(defaultValue, minValue, maxValue))
{
    setCursor(Qt::PointingHandCursor);
}

void RotaryKnob::setValue(int value)
{
    const int clamped = std::clamp(value, m_min, m_max);
    if (clamped == m_value)
        return;
    m_value = clamped;
    update();
    emit valueChanged(m_value);
}

QSize RotaryKnob::sizeHint() const
{
    return QSize(kDiameter, kDiameter);
}

void RotaryKnob::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton)
        return;
    m_dragging = true;
    m_dragStartY = event->position().toPoint().y();
    m_dragStartValue = m_value;
}

void RotaryKnob::mouseMoveEvent(QMouseEvent* event)
{
    if (!m_dragging)
        return;
    const int deltaY = m_dragStartY - event->position().toPoint().y(); // up = positive = increase
    const int span = m_max - m_min;
    const int deltaValue = static_cast<int>(std::round(static_cast<double>(deltaY) * span / kDragPixelsForFullSweep));
    setValue(m_dragStartValue + deltaValue);
}

void RotaryKnob::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        m_dragging = false;
}

void RotaryKnob::enterEvent(QEnterEvent* event)
{
    QWidget::enterEvent(event);
    m_hovered = true;
    update();
}

void RotaryKnob::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    m_hovered = false;
    update();
}

void RotaryKnob::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const int side = std::min(width(), height());
    const QRectF square((width() - side) / 2.0, (height() - side) / 2.0, side, side);
    const QRectF body = square.adjusted(kBodyInset, kBodyInset, -kBodyInset, -kBodyInset);
    const QPointF centre = body.center();
    const double radius = body.width() / 2.0;
    const bool active = m_hovered || m_dragging;

    // Drop shadow -- same low-alpha pass behind the shape the rest of the
    // console uses instead of a QGraphicsEffect.
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawEllipse(body.translated(0, 2));

    // Body: top-lit gradient, the same cap material as ConsoleFader's cap.
    QLinearGradient grad(body.topLeft(), body.bottomLeft());
    grad.setColorAt(0.0, active ? theme::kCapTop.lighter(108) : theme::kCapTop);
    grad.setColorAt(0.5, active ? theme::kCapHover : theme::kCapMid);
    grad.setColorAt(1.0, theme::kCapBottom);
    painter.setBrush(grad);
    painter.setPen(Qt::NoPen);
    painter.drawEllipse(body);

    // Engraved skirt knurl near the rim, on top of the body fill.
    painter.setPen(QPen(theme::kCapEdgeDark, 1.0));
    for (int i = 0; i < kKnurlCount; ++i) {
        painter.save();
        painter.translate(centre);
        painter.rotate(i * 360.0 / kKnurlCount);
        painter.drawLine(QPointF(0, -radius * 0.92), QPointF(0, -radius * 0.74));
        painter.restore();
    }

    // Rim outline + an upper-left radial sheen.
    painter.setPen(QPen(theme::kRim, 1.5));
    painter.setBrush(Qt::NoBrush);
    painter.drawEllipse(body);

    QRadialGradient sheen(centre - QPointF(radius * 0.3, radius * 0.35), radius * 1.15);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 34));
    sheen.setColorAt(0.6, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(sheen);
    painter.drawEllipse(body);

    // Pointer with a dot at its tip.
    const int span = m_max - m_min;
    const double normalized = span > 0 ? static_cast<double>(m_value - m_min) / span : 0.0;
    const double angleDegrees = -kSweepDegrees / 2.0 + normalized * kSweepDegrees;

    painter.save();
    painter.translate(centre);
    painter.rotate(angleDegrees);
    painter.setPen(QPen(theme::kCapText, 2.0, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(0, -radius * 0.22), QPointF(0, -radius * 0.64));
    painter.setPen(Qt::NoPen);
    painter.setBrush(theme::kCapText);
    painter.drawEllipse(QPointF(0, -radius * 0.64), 1.8, 1.8);
    painter.restore();
}

} // namespace radio::ui
