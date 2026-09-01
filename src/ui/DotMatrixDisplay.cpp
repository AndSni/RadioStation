#include "DotMatrixDisplay.h"
#include "ConsoleTheme.h"
#include "StationSettings.h"

#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QSettings>
#include <QTimer>

#include <algorithm>
#include <cstdint>

namespace radio::ui {

namespace {
constexpr int kGlyphCols = 5; // an HD44780 character cell is 5 dots wide...
constexpr int kGlyphRows = 7; // ...and 7 tall (the 8th row is the cursor line)
constexpr int kCellCols = kGlyphCols + 1; // + 1 blank dot-column between characters
constexpr int kMarqueeGapCols = kCellCols * 2; // blank run before the text wraps around
constexpr int kMarqueeIntervalMs = 85; // one dot-column per tick
constexpr int kPadX = 10;
constexpr int kLineGapPx = 6;

// Matched to the Adafruit 399 "RGB negative" LCD: a near-black screen faintly
// tinted with the lamp colour, intense lit segments with a soft bloom, and a
// dim-but-visible unlit dot grid. Everything is derived from one "lamp"
// colour so the whole panel can be tinted (red = playing, yellow = cued).
QColor screenFor(const QColor& lamp)
{
    return QColor(lamp.red() / 15 + 6, lamp.green() / 15 + 4, lamp.blue() / 15 + 3);
}
QColor haloFor(const QColor& lamp)
{
    QColor c = lamp;
    c.setAlpha(70);
    return c;
}
QColor offFor(const QColor& lamp)
{
    QColor c = lamp;
    c.setAlpha(40); // over the dark screen this reads as the faint grid
    return c;
}

// The classic 5x7 "glcdfont" character ROM for ASCII 0x20..0x7F -- the same
// bitmap font a real HD44780 module ships. Column-major: 5 bytes per glyph,
// bit 0 = the top row. This is what makes it look like a hardware LCD
// rather than a shrunk typeface (sampling an arbitrary font into 5x7 was
// tried and looked mushy -- these glyphs are *designed* for the grid).
constexpr std::uint8_t kFont5x7[96][kGlyphCols] = {
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, // 0x20 space
    { 0x00, 0x00, 0x5F, 0x00, 0x00 }, // !
    { 0x00, 0x07, 0x00, 0x07, 0x00 }, // "
    { 0x14, 0x7F, 0x14, 0x7F, 0x14 }, // #
    { 0x24, 0x2A, 0x7F, 0x2A, 0x12 }, // $
    { 0x23, 0x13, 0x08, 0x64, 0x62 }, // %
    { 0x36, 0x49, 0x55, 0x22, 0x50 }, // &
    { 0x00, 0x05, 0x03, 0x00, 0x00 }, // '
    { 0x00, 0x1C, 0x22, 0x41, 0x00 }, // (
    { 0x00, 0x41, 0x22, 0x1C, 0x00 }, // )
    { 0x14, 0x08, 0x3E, 0x08, 0x14 }, // *
    { 0x08, 0x08, 0x3E, 0x08, 0x08 }, // +
    { 0x00, 0x50, 0x30, 0x00, 0x00 }, // ,
    { 0x08, 0x08, 0x08, 0x08, 0x08 }, // -
    { 0x00, 0x60, 0x60, 0x00, 0x00 }, // .
    { 0x20, 0x10, 0x08, 0x04, 0x02 }, // /
    { 0x3E, 0x51, 0x49, 0x45, 0x3E }, // 0
    { 0x00, 0x42, 0x7F, 0x40, 0x00 }, // 1
    { 0x42, 0x61, 0x51, 0x49, 0x46 }, // 2
    { 0x21, 0x41, 0x45, 0x4B, 0x31 }, // 3
    { 0x18, 0x14, 0x12, 0x7F, 0x10 }, // 4
    { 0x27, 0x45, 0x45, 0x45, 0x39 }, // 5
    { 0x3C, 0x4A, 0x49, 0x49, 0x30 }, // 6
    { 0x01, 0x71, 0x09, 0x05, 0x03 }, // 7
    { 0x36, 0x49, 0x49, 0x49, 0x36 }, // 8
    { 0x06, 0x49, 0x49, 0x29, 0x1E }, // 9
    { 0x00, 0x36, 0x36, 0x00, 0x00 }, // :
    { 0x00, 0x56, 0x36, 0x00, 0x00 }, // ;
    { 0x08, 0x14, 0x22, 0x41, 0x00 }, // <
    { 0x14, 0x14, 0x14, 0x14, 0x14 }, // =
    { 0x00, 0x41, 0x22, 0x14, 0x08 }, // >
    { 0x02, 0x01, 0x51, 0x09, 0x06 }, // ?
    { 0x32, 0x49, 0x79, 0x41, 0x3E }, // @
    { 0x7E, 0x11, 0x11, 0x11, 0x7E }, // A
    { 0x7F, 0x49, 0x49, 0x49, 0x36 }, // B
    { 0x3E, 0x41, 0x41, 0x41, 0x22 }, // C
    { 0x7F, 0x41, 0x41, 0x22, 0x1C }, // D
    { 0x7F, 0x49, 0x49, 0x49, 0x41 }, // E
    { 0x7F, 0x09, 0x09, 0x09, 0x01 }, // F
    { 0x3E, 0x41, 0x49, 0x49, 0x7A }, // G
    { 0x7F, 0x08, 0x08, 0x08, 0x7F }, // H
    { 0x00, 0x41, 0x7F, 0x41, 0x00 }, // I
    { 0x20, 0x40, 0x41, 0x3F, 0x01 }, // J
    { 0x7F, 0x08, 0x14, 0x22, 0x41 }, // K
    { 0x7F, 0x40, 0x40, 0x40, 0x40 }, // L
    { 0x7F, 0x02, 0x0C, 0x02, 0x7F }, // M
    { 0x7F, 0x04, 0x08, 0x10, 0x7F }, // N
    { 0x3E, 0x41, 0x41, 0x41, 0x3E }, // O
    { 0x7F, 0x09, 0x09, 0x09, 0x06 }, // P
    { 0x3E, 0x41, 0x51, 0x21, 0x5E }, // Q
    { 0x7F, 0x09, 0x19, 0x29, 0x46 }, // R
    { 0x46, 0x49, 0x49, 0x49, 0x31 }, // S
    { 0x01, 0x01, 0x7F, 0x01, 0x01 }, // T
    { 0x3F, 0x40, 0x40, 0x40, 0x3F }, // U
    { 0x1F, 0x20, 0x40, 0x20, 0x1F }, // V
    { 0x3F, 0x40, 0x38, 0x40, 0x3F }, // W
    { 0x63, 0x14, 0x08, 0x14, 0x63 }, // X
    { 0x07, 0x08, 0x70, 0x08, 0x07 }, // Y
    { 0x61, 0x51, 0x49, 0x45, 0x43 }, // Z
    { 0x00, 0x7F, 0x41, 0x41, 0x00 }, // [
    { 0x02, 0x04, 0x08, 0x10, 0x20 }, // backslash
    { 0x00, 0x41, 0x41, 0x7F, 0x00 }, // ]
    { 0x04, 0x02, 0x01, 0x02, 0x04 }, // ^
    { 0x40, 0x40, 0x40, 0x40, 0x40 }, // _
    { 0x00, 0x01, 0x02, 0x04, 0x00 }, // `
    { 0x20, 0x54, 0x54, 0x54, 0x78 }, // a
    { 0x7F, 0x48, 0x44, 0x44, 0x38 }, // b
    { 0x38, 0x44, 0x44, 0x44, 0x20 }, // c
    { 0x38, 0x44, 0x44, 0x48, 0x7F }, // d
    { 0x38, 0x54, 0x54, 0x54, 0x18 }, // e
    { 0x08, 0x7E, 0x09, 0x01, 0x02 }, // f
    { 0x0C, 0x52, 0x52, 0x52, 0x3E }, // g
    { 0x7F, 0x08, 0x04, 0x04, 0x78 }, // h
    { 0x00, 0x44, 0x7D, 0x40, 0x00 }, // i
    { 0x20, 0x40, 0x44, 0x3D, 0x00 }, // j
    { 0x7F, 0x10, 0x28, 0x44, 0x00 }, // k
    { 0x00, 0x41, 0x7F, 0x40, 0x00 }, // l
    { 0x7C, 0x04, 0x18, 0x04, 0x78 }, // m
    { 0x7C, 0x08, 0x04, 0x04, 0x78 }, // n
    { 0x38, 0x44, 0x44, 0x44, 0x38 }, // o
    { 0x7C, 0x14, 0x14, 0x14, 0x08 }, // p
    { 0x08, 0x14, 0x14, 0x18, 0x7C }, // q
    { 0x7C, 0x08, 0x04, 0x04, 0x08 }, // r
    { 0x48, 0x54, 0x54, 0x54, 0x20 }, // s
    { 0x04, 0x3F, 0x44, 0x40, 0x20 }, // t
    { 0x3C, 0x40, 0x40, 0x20, 0x7C }, // u
    { 0x1C, 0x20, 0x40, 0x20, 0x1C }, // v
    { 0x3C, 0x40, 0x30, 0x40, 0x3C }, // w
    { 0x44, 0x28, 0x10, 0x28, 0x44 }, // x
    { 0x0C, 0x50, 0x50, 0x50, 0x3C }, // y
    { 0x44, 0x64, 0x54, 0x4C, 0x44 }, // z
    { 0x00, 0x08, 0x36, 0x41, 0x00 }, // {
    { 0x00, 0x00, 0x7F, 0x00, 0x00 }, // |
    { 0x00, 0x41, 0x36, 0x08, 0x00 }, // }
    { 0x08, 0x04, 0x08, 0x10, 0x08 }, // ~
    { 0x00, 0x00, 0x00, 0x00, 0x00 }, // 0x7F
};

const std::uint8_t* glyphFor(QChar ch)
{
    static const std::uint8_t blank[kGlyphCols] = { 0, 0, 0, 0, 0 };
    const int idx = ch.unicode() - 0x20;
    return (idx >= 0 && idx < 96) ? kFont5x7[idx] : blank;
}

int textDotColumns(const QString& s)
{
    return s.length() * kCellCols;
}

// The character ROM only covers printable ASCII (0x20..0x7E). Fold the
// common typographic characters that turn up in track metadata to their
// ASCII equivalents; anything else left becomes a space.
QString sanitize(QString s)
{
    s.replace(QChar(0x2014), QLatin1Char('-')); // em dash
    s.replace(QChar(0x2013), QLatin1Char('-')); // en dash
    s.replace(QChar(0x00B7), QLatin1Char('-')); // middle dot
    s.replace(QChar(0x2022), QLatin1Char('-')); // bullet
    s.replace(QChar(0x2018), QLatin1Char('\'')); // left single quote
    s.replace(QChar(0x2019), QLatin1Char('\'')); // right single quote
    s.replace(QChar(0x201C), QLatin1Char('"')); // left double quote
    s.replace(QChar(0x201D), QLatin1Char('"')); // right double quote
    s.replace(QString(QChar(0x2026)), QStringLiteral("...")); // ellipsis
    for (QChar& c : s) {
        if (c.unicode() < 0x20 || c.unicode() > 0x7E)
            c = QLatin1Char(' ');
    }
    return s;
}
} // namespace

DotMatrixDisplay::DotMatrixDisplay(QWidget* parent)
    : QWidget(parent)
{
    reloadMetricsFromSettings();
}

void DotMatrixDisplay::reloadMetricsFromSettings()
{
    using namespace station_settings;
    QSettings s;
    m_line1DotPx = std::clamp(s.value(kDeckLine1FontPx, kDefaultDeckLine1FontPx).toInt(), 2, 10);
    m_line2DotPx = std::clamp(s.value(kDeckLine2FontPx, kDefaultDeckLine2FontPx).toInt(), 2, 10);
    m_marqueeOffset = 0;
    setMinimumHeight(kGlyphRows * (m_line1DotPx + m_line2DotPx) + kLineGapPx + 18);
    updateMarqueeState();
    updateGeometry();
    update();
}

void DotMatrixDisplay::setText(const QString& text)
{
    if (text == m_text)
        return;
    m_text = text; // stored raw; sanitize() is applied at paint/measure time
    m_marqueeOffset = 0;
    updateMarqueeState();
    update();
}

void DotMatrixDisplay::setTagLine(const QString& tags)
{
    if (tags == m_tags)
        return;
    m_tags = tags;
    update();
}

void DotMatrixDisplay::setLampColor(const QColor& colour)
{
    if (!colour.isValid() || colour == m_lampColor)
        return;
    m_lampColor = colour;
    update();
}

QSize DotMatrixDisplay::sizeHint() const
{
    return QSize(400, kGlyphRows * (m_line1DotPx + m_line2DotPx) + kLineGapPx + 28);
}

QSize DotMatrixDisplay::minimumSizeHint() const
{
    return QSize(180, kGlyphRows * (m_line1DotPx + m_line2DotPx) + kLineGapPx + 18);
}

int DotMatrixDisplay::line1DotColumns() const
{
    return textDotColumns(sanitize(m_text));
}

void DotMatrixDisplay::resizeEvent(QResizeEvent*)
{
    updateMarqueeState();
}

void DotMatrixDisplay::showEvent(QShowEvent*)
{
    updateMarqueeState();
}

void DotMatrixDisplay::hideEvent(QHideEvent*)
{
    updateMarqueeState();
}

void DotMatrixDisplay::updateMarqueeState()
{
    const int screenCols = m_line1DotPx > 0 ? (width() - 2 * kPadX) / m_line1DotPx : 0;
    const bool overflowing = line1DotColumns() > screenCols;

    if (overflowing && isVisible()) {
        if (!m_marqueeTimer) {
            m_marqueeTimer = new QTimer(this);
            m_marqueeTimer->setInterval(kMarqueeIntervalMs);
            connect(m_marqueeTimer, &QTimer::timeout, this, [this]() {
                ++m_marqueeOffset;
                update();
            });
        }
        if (!m_marqueeTimer->isActive())
            m_marqueeTimer->start();
    } else if (m_marqueeTimer && m_marqueeTimer->isActive()) {
        m_marqueeTimer->stop();
        m_marqueeOffset = 0;
    }
}

namespace {
// Draws one text line onto the fixed dot grid. `scroll` is the marquee
// offset in dot-columns (ignored unless the text overflows).
void paintLcdLine(QPainter& painter, const QRect& area, const QString& text, int dotPx, int scroll, const QColor& lamp)
{
    if (dotPx < 2)
        return;
    const int gap = std::max(1, dotPx / 4);
    const int square = dotPx - gap;
    const int screenCols = area.width() / dotPx;
    if (screenCols <= 0)
        return;

    const int textCols = textDotColumns(text);
    const bool overflow = textCols > screenCols;
    const int period = textCols + kMarqueeGapCols;
    const int startCol = overflow ? 0 : (screenCols - textCols) / 2;

    const QColor on = lamp;
    const QColor halo = haloFor(lamp);
    const QColor off = offFor(lamp);

    for (int c = 0; c < screenCols; ++c) {
        int srcCol = c - startCol + (overflow ? scroll : 0);
        if (overflow)
            srcCol = ((srcCol % period) + period) % period;

        std::uint8_t bits = 0;
        if (srcCol >= 0 && srcCol < textCols) {
            const int charIdx = srcCol / kCellCols;
            const int colInChar = srcCol % kCellCols;
            if (colInChar < kGlyphCols)
                bits = glyphFor(text.at(charIdx))[colInChar];
        }

        const int x = area.left() + c * dotPx;
        for (int r = 0; r < kGlyphRows; ++r) {
            const int y = area.top() + r * dotPx;
            if ((bits >> r) & 1) {
                painter.fillRect(x - 1, y - 1, square + 2, square + 2, halo); // bloom
                painter.fillRect(x, y, square, square, on);
            } else {
                painter.fillRect(x, y, square, square, off);
            }
        }
    }
}
} // namespace

void DotMatrixDisplay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF full(rect());
    const qreal radius = 5.0;
    QPainterPath clip;
    clip.addRoundedRect(full, radius, radius);
    painter.setClipPath(clip);

    // Near-black screen that glows brighter toward the centre -- the
    // module's backlight bleed behind a negative-mode LCD.
    const QColor& lamp = m_lampColor;
    painter.fillRect(full, screenFor(lamp));
    QRadialGradient bloom(full.center(), full.width() * 0.72);
    QColor glowMid = lamp;
    glowMid.setAlpha(60);
    QColor glowEdge = lamp;
    glowEdge.setAlpha(10);
    bloom.setColorAt(0.0, glowMid);
    bloom.setColorAt(0.7, glowEdge);
    bloom.setColorAt(1.0, QColor(lamp.red(), lamp.green(), lamp.blue(), 0));
    painter.fillRect(full, bloom);

    // --- The two dot-grid lines, pixel-aligned so every dot is crisp ------
    painter.setRenderHint(QPainter::Antialiasing, false);
    const int line1H = kGlyphRows * m_line1DotPx;
    const int line2H = kGlyphRows * m_line2DotPx;
    const int blockH = line1H + (m_tags.isEmpty() ? 0 : kLineGapPx + line2H);
    const int top = static_cast<int>(full.top()) + std::max(4, (static_cast<int>(full.height()) - blockH) / 2);

    const QRect line1Area(kPadX, top, width() - 2 * kPadX, line1H);
    paintLcdLine(painter, line1Area, sanitize(m_text), m_line1DotPx, m_marqueeOffset, lamp);
    if (!m_tags.isEmpty()) {
        const QRect line2Area(kPadX, top + line1H + kLineGapPx, width() - 2 * kPadX, line2H);
        paintLcdLine(painter, line2Area, sanitize(m_tags), m_line2DotPx, 0, lamp);
    }
    painter.setRenderHint(QPainter::Antialiasing, true);

    // Inner vignette.
    QRadialGradient vignette(full.center(), full.width() * 0.78);
    vignette.setColorAt(0.0, QColor(0, 0, 0, 0));
    vignette.setColorAt(0.82, QColor(0, 0, 0, 0));
    vignette.setColorAt(1.0, QColor(0, 0, 0, 130));
    painter.setPen(Qt::NoPen);
    painter.setBrush(vignette);
    painter.drawRect(full);

    // Bezel.
    painter.setClipping(false);
    painter.setPen(QPen(theme::kLcdRim, 1));
    painter.setBrush(Qt::NoBrush);
    painter.drawRoundedRect(full.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
}

} // namespace radio::ui
