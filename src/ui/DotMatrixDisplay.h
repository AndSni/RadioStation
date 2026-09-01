#pragma once

#include <QColor>
#include <QString>
#include <QWidget>

class QTimer;

namespace radio::ui {

// A hand-painted **character-LCD** panel -- the deck's "now playing"
// readout, modelled on a real HD44780-style 16x2 dot-matrix module (the
// Adafruit RGB negative LCD, product 399): a black screen of 5x7 character
// cells where every dot -- lit or not -- is a visible little square, so the
// whole thing reads as a pixel grid, not text.
//
// It does NOT use a display font. A font (even a "pixel" TTF) can't give the
// fixed-cell, visible-unlit-dot look. Instead each printable ASCII glyph is
// baked once into a 5x7 bit pattern (sampled from a bold monospace face --
// our stand-in "character ROM", see glyphFor()) and the two lines are drawn
// dot by dot onto a fixed grid. Overflowing text scrolls through the grid a
// whole dot-column at a time, the way a hardware sign does.
//
// Colour is a fixed LCD red. The two "dot size" metrics (pixels per grid
// dot, one per line) are read from QSettings
// (station_settings::kDeckLine1FontPx / kDeckLine2FontPx), editable from
// Station Settings > Deck Display, and applied live via
// reloadMetricsFromSettings().
//
// The whole panel is tinted by one "lamp" colour (setLampColor()) --
// everything (lit dots, bloom, unlit grid, screen) is derived from it, so
// DeckWidget can flip the display red (playing) / yellow (cued).
//
// setText()/text() mirror AudioPillWidget's API so this drops in for the
// deck's old pill title. Line 1 is the track ("Artist — Title"); line 2 is
// the technical strip ("320k / 128 BPM / -8.1 dB").
class DotMatrixDisplay : public QWidget {
    Q_OBJECT

public:
    explicit DotMatrixDisplay(QWidget* parent = nullptr);

    void setText(const QString& text); // line 1
    QString text() const { return m_text; }

    void setTagLine(const QString& tags); // line 2
    QString tagLine() const { return m_tags; }

    void setLampColor(const QColor& colour); // tints the whole panel
    QColor lampColor() const { return m_lampColor; }

    void reloadMetricsFromSettings();

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    void updateMarqueeState(); // starts/stops the scroll timer based on visibility + overflow
    int line1DotColumns() const; // width of line 1's text, in grid dot-columns

    QString m_text;
    QString m_tags;
    QColor m_lampColor { 0xff, 0x38, 0x24 }; // default LCD red; setLampColor() overrides

    int m_line1DotPx = 5; // pixels per grid dot, line 1 -- tunable via QSettings
    int m_line2DotPx = 3;

    int m_marqueeOffset = 0; // in dot-columns
    QTimer* m_marqueeTimer = nullptr;
};

} // namespace radio::ui
