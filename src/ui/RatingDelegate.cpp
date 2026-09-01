#include "RatingDelegate.h"

#include "db/TrackRecord.h"

#include <QMouseEvent>
#include <QPainter>
#include <algorithm>

namespace radio::ui {

using radio::db::TrackRecord;

RatingDelegate::RatingDelegate(QObject* parent)
    : QStyledItemDelegate(parent)
{
}

void RatingDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const int rating = index.data(Qt::DisplayRole).toInt();

    painter->save();
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    QFont font = painter->font();
    font.setPointSize(font.pointSize() + 1);
    painter->setFont(font);

    for (int star = 0; star < kMaxStars; ++star) {
        const QRect starRect(option.rect.left() + star * kStarSize, option.rect.top(), kStarSize, option.rect.height());
        painter->drawText(starRect, Qt::AlignCenter, star < rating ? QStringLiteral("★") : QStringLiteral("☆"));
    }
    painter->restore();
}

bool RatingDelegate::editorEvent(
    QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index)
{
    if (event->type() != QEvent::MouseButtonRelease)
        return false;

    auto* mouseEvent = static_cast<QMouseEvent*>(event);
    const int xOffset = static_cast<int>(mouseEvent->position().x()) - option.rect.left();
    const int clickedStar = std::clamp(xOffset / kStarSize, 0, kMaxStars - 1);
    const int currentRating = index.data(Qt::DisplayRole).toInt();
    const int newRating = (clickedStar + 1 == currentRating) ? 0 : clickedStar + 1;

    model->setData(index, newRating, Qt::EditRole);

    const qint64 trackId = index.data(Qt::UserRole).value<TrackRecord>().id;
    emit ratingEdited(trackId, newRating);
    return true;
}

QSize RatingDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    Q_UNUSED(index);
    return QSize(kMaxStars * kStarSize, option.rect.height());
}

} // namespace radio::ui
