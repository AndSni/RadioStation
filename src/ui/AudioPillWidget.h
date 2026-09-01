#pragma once

#include "AudioPillPainter.h"

#include <QWidget>

namespace radio::ui {

// A single Audio-object pill as a real widget — used by DeckWidget for its
// "now loaded" display, the one place a pill isn't painted into an item
// view (there's exactly one per deck, not a row in a list). Paints via the
// same AudioPillPainter routine AudioPillDelegate uses for Library/Queue/
// Playlist Editor, so all four panels stay visually identical.
class AudioPillWidget : public QWidget {
    Q_OBJECT

public:
    explicit AudioPillWidget(QWidget* parent = nullptr);

    void setText(const QString& text);
    void setColor(const QColor& color);
    QString text() const { return m_text; }
    QColor color() const { return m_color; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_text;
    QColor m_color = AudioPillPainter::kNeutralColor;
};

} // namespace radio::ui
