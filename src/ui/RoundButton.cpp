#include "RoundButton.h"
#include "ConsoleTheme.h"

#include <QPainter>
#include <algorithm>

namespace radio::ui {

namespace {
const QColor kBodyColor = theme::kCapMid;
const QColor kHoverColor = theme::kCapHover;
const QColor kPressedColor = theme::kCapPressed;
const QColor kRimColor = theme::kRim;
const QColor kTextColor = theme::kCapText;
}

RoundButton::RoundButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
{
    setCursor(Qt::PointingHandCursor);
    // The circle IS the whole clickable target -- no native chrome to
    // fight, so there's nothing default focus/flat styling would add here.
    setFlat(true);
}

QSize RoundButton::sizeHint() const
{
    return QSize(kDiameter, kDiameter);
}

QRect RoundButton::bodyRect() const
{
    const int side = std::min(width(), height());
    const QRect square((width() - side) / 2, (height() - side) / 2, side, side);
    return square.adjusted(kBodyInset, kBodyInset, -kBodyInset, -kBodyInset);
}

void RoundButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    m_hovered = true;
    update();
}

void RoundButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    m_hovered = false;
    update();
}

void RoundButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect rect = bodyRect();
    QColor body = isDown() ? kPressedColor : (m_hovered ? kHoverColor : kBodyColor);
    if (!isEnabled())
        body = body.darker(150);

    painter.setPen(QPen(kRimColor, 1.5));
    painter.setBrush(body);
    painter.drawEllipse(rect);

    if (!text().isEmpty()) {
        painter.setPen(isEnabled() ? kTextColor : kTextColor.darker(150));
        painter.drawText(rect, Qt::AlignCenter | Qt::TextWordWrap, text());
    }
}

} // namespace radio::ui
