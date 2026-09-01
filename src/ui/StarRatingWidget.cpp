#include "StarRatingWidget.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace radio::ui {

StarRatingWidget::StarRatingWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(kMaxStars * kStarSize, kStarSize);
}

void StarRatingWidget::setRating(int rating)
{
    rating = std::clamp(rating, 0, kMaxStars);
    if (rating == m_rating)
        return;
    m_rating = rating;
    update();
}

QSize StarRatingWidget::sizeHint() const
{
    return QSize(kMaxStars * kStarSize, kStarSize);
}

void StarRatingWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    QFont font = painter.font();
    font.setPointSize(font.pointSize() + 1);
    painter.setFont(font);
    // Disabled (no known track to rate) reads as dim, not just non-reactive.
    painter.setPen(isEnabled() ? palette().color(QPalette::WindowText) : palette().color(QPalette::Disabled, QPalette::WindowText));

    for (int star = 0; star < kMaxStars; ++star) {
        const QRect starRect(star * kStarSize, 0, kStarSize, height());
        painter.drawText(starRect, Qt::AlignCenter, star < m_rating ? QStringLiteral("★") : QStringLiteral("☆"));
    }
}

void StarRatingWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (!isEnabled()) {
        event->ignore();
        return;
    }

    const int xOffset = static_cast<int>(event->position().x());
    const int clickedStar = std::clamp(xOffset / kStarSize, 0, kMaxStars - 1);
    const int newRating = (clickedStar + 1 == m_rating) ? 0 : clickedStar + 1;

    setRating(newRating);
    emit ratingChanged(newRating);
}

} // namespace radio::ui
