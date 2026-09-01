#pragma once

#include <QWidget>

namespace radio::ui {

// Inline 1-5 star rating control, standalone-widget counterpart to
// RatingDelegate (which does the same thing painted into an item-view
// cell) — used where a rating needs to sit in a plain widget layout instead
// of a table/list, e.g. DeckWidget's currently-loaded-track row. Same
// glyphs, same "click the current rating again to clear to 0" behavior.
// Purely a UI control: it only tracks/paints a rating and emits
// ratingChanged() on click — persisting the change (TrackRepository::
// setRating()) is the owning widget's job, same division of labor as
// RatingDelegate/LibraryBrowserWidget::onRatingEdited().
class StarRatingWidget : public QWidget {
    Q_OBJECT

public:
    explicit StarRatingWidget(QWidget* parent = nullptr);

    void setRating(int rating); // clamped 0-5, does not emit ratingChanged()
    int rating() const { return m_rating; }

    QSize sizeHint() const override;

signals:
    void ratingChanged(int newRating);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    static constexpr int kMaxStars = 5;
    static constexpr int kStarSize = 16;

    int m_rating = 0;
};

} // namespace radio::ui
