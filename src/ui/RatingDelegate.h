#pragma once

#include <QStyledItemDelegate>

namespace radio::ui {

// Inline 1-5 star rating editor for a TrackTableModel::RatingColumn cell.
// Paints filled/hollow star glyphs (no icon assets exist anywhere in this
// project - stays consistent) and handles a plain mouse-release directly in
// editorEvent() rather than opening a real editor widget, so it works with
// the view's NoEditTriggers setting exactly like Qt's own star-delegate
// pattern. Clicking the currently-set star again clears the rating to 0.
// Only updates the model in-memory (via setData) - the owning widget must
// connect to ratingEdited() to persist the change.
class RatingDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    explicit RatingDelegate(QObject* parent = nullptr);

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    bool editorEvent(
        QEvent* event, QAbstractItemModel* model, const QStyleOptionViewItem& option, const QModelIndex& index) override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

signals:
    void ratingEdited(qint64 trackId, int newRating);

private:
    static constexpr int kMaxStars = 5;
    static constexpr int kStarSize = 16;
};

} // namespace radio::ui
