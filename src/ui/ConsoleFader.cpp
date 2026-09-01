#include "ConsoleFader.h"
#include "ConsoleTheme.h"

#include <QEnterEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <algorithm>
#include <cmath>

namespace radio::ui {

namespace {
constexpr int kCapThickness = 20; // cap extent ALONG the travel axis
constexpr int kCapAcrossMax = 34; // cap extent ACROSS the travel axis
constexpr int kGrooveAcross = 10; // slot width
constexpr int kScaleStrip = 12; // reserved strip beside the groove for the etched scale -- the cap never covers it
constexpr int kScaleGap = 2; // gap from the strip edge to the start of a tick
} // namespace

ConsoleFader::ConsoleFader(Qt::Orientation orientation, QWidget* parent)
    : QSlider(orientation, parent)
{
    setCursor(Qt::PointingHandCursor);
}

void ConsoleFader::setCenterDetent(bool enabled)
{
    if (m_centerDetent == enabled)
        return;
    m_centerDetent = enabled;
    update();
}

QSize ConsoleFader::sizeHint() const
{
    return orientation() == Qt::Vertical ? QSize(44, 160) : QSize(160, 40);
}

QSize ConsoleFader::minimumSizeHint() const
{
    return orientation() == Qt::Vertical ? QSize(44, 80) : QSize(80, 40);
}

QRect ConsoleFader::grooveRect() const
{
    if (orientation() == Qt::Vertical) {
        const int usable = width() - kScaleStrip; // groove + cap live here; scale sits in the strip to the right
        const int x = (usable - kGrooveAcross) / 2;
        return QRect(x, kCapThickness / 2, kGrooveAcross, std::max(1, height() - kCapThickness));
    }
    const int usable = height() - kScaleStrip; // scale strip runs along the bottom
    const int y = (usable - kGrooveAcross) / 2;
    return QRect(kCapThickness / 2, y, std::max(1, width() - kCapThickness), kGrooveAcross);
}

double ConsoleFader::valueFraction(int value) const
{
    const int span = maximum() - minimum();
    if (span <= 0)
        return 0.0;
    return static_cast<double>(std::clamp(value, minimum(), maximum()) - minimum()) / span;
}

QRect ConsoleFader::capRect() const
{
    const bool vertical = orientation() == Qt::Vertical;
    const QRect groove = grooveRect();
    const double f = valueFraction(value());

    if (vertical) {
        const int top = kCapThickness / 2;
        const int bottom = height() - kCapThickness / 2;
        const double travel = bottom - top;
        const double centre = invertedAppearance() ? top + f * travel : bottom - f * travel;
        const int across = std::max(12, std::min(kCapAcrossMax, width() - kScaleStrip - 2));
        return QRect(groove.center().x() - across / 2, static_cast<int>(std::lround(centre)) - kCapThickness / 2,
            across, kCapThickness);
    }
    const int left = kCapThickness / 2;
    const int right = width() - kCapThickness / 2;
    const double travel = right - left;
    const double centre = invertedAppearance() ? right - f * travel : left + f * travel;
    const int across = std::max(12, std::min(kCapAcrossMax, height() - kScaleStrip - 2));
    return QRect(static_cast<int>(std::lround(centre)) - kCapThickness / 2, groove.center().y() - across / 2,
        kCapThickness, across);
}

void ConsoleFader::setValueFromPos(const QPoint& pos)
{
    double f = 0.0;
    if (orientation() == Qt::Vertical) {
        const double top = kCapThickness / 2.0;
        const double bottom = height() - kCapThickness / 2.0;
        const double travel = bottom - top;
        if (travel <= 0.0)
            return;
        const double y = std::clamp(static_cast<double>(pos.y()), top, bottom);
        f = invertedAppearance() ? (y - top) / travel : (bottom - y) / travel;
    } else {
        const double left = kCapThickness / 2.0;
        const double right = width() - kCapThickness / 2.0;
        const double travel = right - left;
        if (travel <= 0.0)
            return;
        const double x = std::clamp(static_cast<double>(pos.x()), left, right);
        f = invertedAppearance() ? (right - x) / travel : (x - left) / travel;
    }
    setValue(minimum() + static_cast<int>(std::lround(f * (maximum() - minimum()))));
}

void ConsoleFader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QSlider::mousePressEvent(event);
        return;
    }
    setSliderDown(true);
    setValueFromPos(event->position().toPoint());
    event->accept();
}

void ConsoleFader::mouseMoveEvent(QMouseEvent* event)
{
    if (!isSliderDown()) {
        QSlider::mouseMoveEvent(event);
        return;
    }
    setValueFromPos(event->position().toPoint());
    event->accept();
}

void ConsoleFader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && isSliderDown()) {
        setSliderDown(false);
        event->accept();
        return;
    }
    QSlider::mouseReleaseEvent(event);
}

void ConsoleFader::enterEvent(QEnterEvent* event)
{
    QSlider::enterEvent(event);
    m_hovered = true;
    update();
}

void ConsoleFader::leaveEvent(QEvent* event)
{
    QSlider::leaveEvent(event);
    m_hovered = false;
    update();
}

void ConsoleFader::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const bool vertical = orientation() == Qt::Vertical;
    if (!isEnabled())
        painter.setOpacity(0.55);

    // --- Groove: a recessed channel ---------------------------------------
    const QRect groove = grooveRect();
    QLinearGradient grooveGrad(groove.topLeft(), vertical ? groove.topRight() : groove.bottomLeft());
    grooveGrad.setColorAt(0.0, theme::kGrooveLight);
    grooveGrad.setColorAt(0.5, theme::kGrooveDark);
    grooveGrad.setColorAt(1.0, theme::kGrooveLight);
    painter.setPen(Qt::NoPen);
    painter.setBrush(grooveGrad);
    painter.drawRoundedRect(groove, 3, 3);
    // Carved edge: dark on the top/left inner rim, faint highlight bottom/right.
    QColor highlight = theme::kGrooveHighlight;
    highlight.setAlpha(110);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(QColor(0, 0, 0, 130), 1));
    painter.drawLine(groove.topLeft() + QPoint(1, 1),
        vertical ? groove.bottomLeft() + QPoint(1, -1) : groove.topRight() + QPoint(-1, 1));
    painter.setPen(QPen(highlight, 1));
    painter.drawLine(vertical ? groove.topRight() + QPoint(-1, 1) : groove.bottomLeft() + QPoint(1, -1),
        groove.bottomRight() + QPoint(-1, -1));

    // --- Etched scale, in the reserved strip the cap never reaches -------
    const auto axisPos = [&](double f) -> int {
        if (vertical) {
            const double top = kCapThickness / 2.0;
            const double bottom = height() - kCapThickness / 2.0;
            const double travel = bottom - top;
            return static_cast<int>(std::lround(invertedAppearance() ? top + f * travel : bottom - f * travel));
        }
        const double left = kCapThickness / 2.0;
        const double right = width() - kCapThickness / 2.0;
        const double travel = right - left;
        return static_cast<int>(std::lround(invertedAppearance() ? right - f * travel : left + f * travel));
    };
    const int stripStart = (vertical ? width() : height()) - kScaleStrip + kScaleGap;
    const auto drawTick = [&](double f, int len, const QColor& colour, int weight) {
        painter.setPen(QPen(colour, weight));
        if (vertical) {
            const int y = axisPos(f);
            painter.drawLine(stripStart, y, stripStart + len, y);
        } else {
            const int x = axisPos(f);
            painter.drawLine(x, stripStart, x, stripStart + len);
        }
    };

    const int span = maximum() - minimum();
    const int step = tickInterval();
    if (step > 0 && span > 0) {
        for (int v = minimum(); v <= maximum(); v += step)
            drawTick(valueFraction(v), 4, theme::kScaleTick, 1);
    }
    drawTick(0.0, 8, theme::kScaleTickMajor, 1);
    drawTick(1.0, 8, theme::kScaleTickMajor, 1);
    drawTick(0.5, m_centerDetent ? 9 : 5, m_centerDetent ? theme::kScaleTickMajor : theme::kScaleTick,
        m_centerDetent ? 2 : 1);

    // --- Cap + its bright indicator line -------------------------------
    const QRectF cap = capRect();
    theme::paintConsoleCap(painter, cap, m_hovered, isSliderDown(), !vertical);

    if (vertical) {
        const double y = cap.center().y();
        painter.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
        painter.drawLine(QPointF(cap.left() + 3, y + 1.5), QPointF(cap.right() - 3, y + 1.5));
        painter.setPen(QPen(theme::kPointerLine, 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(cap.left() + 3, y), QPointF(cap.right() - 3, y));
    } else {
        const double x = cap.center().x();
        painter.setPen(QPen(QColor(0, 0, 0, 110), 1.0));
        painter.drawLine(QPointF(x + 1.5, cap.top() + 3), QPointF(x + 1.5, cap.bottom() - 3));
        painter.setPen(QPen(theme::kPointerLine, 2.4, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(x, cap.top() + 3), QPointF(x, cap.bottom() - 3));
    }
}

} // namespace radio::ui
