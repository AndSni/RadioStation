#pragma once

#include <QLabel>

namespace radio::ui {

// A single-line text label that NEVER grows the layout to fit its text:
// its sizeHint width is capped, and when the text is wider than the space
// it actually gets, the overflow is painted fading out under the right edge
// (toward the console bezel colour) instead of pushing the panel wider or
// showing an ellipsis. Used for the Deck metadata block, where an
// arbitrarily long title/artist must not be able to stretch the deck.
class FadingLabel : public QLabel {
    Q_OBJECT

public:
    explicit FadingLabel(QWidget* parent = nullptr);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
};

} // namespace radio::ui
