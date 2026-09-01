#include "LcdReadout.h"
#include "ConsoleTheme.h"
#include "MixerSliderHelpers.h"
#include "UiStyleHelpers.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace radio::ui {

namespace {
constexpr int kScreenHeight = 24;
constexpr int kCaptionHeight = 13;
constexpr int kMinPixelSize = 8;
constexpr int kMaxPixelSize = kScreenHeight - 6;
constexpr int kHorizontalPadding = 4;

const QColor kScreenBg = theme::kLcdScreen;
const QColor kScreenRim = theme::kLcdRim;
const QColor kDigitColor = theme::kLedRed; // same "lit" red as OnAirLabel/IndicatorButton's LED
const QColor kCaptionColor = theme::kLcdCaption;
}

LcdReadout::LcdReadout(QWidget* parent)
    : QWidget(parent)
{
}

void LcdReadout::setCaption(const QString& caption)
{
    if (caption == m_caption)
        return;
    m_caption = caption;
    update();
}

void LcdReadout::setValue(const QString& value)
{
    if (value == m_value)
        return;
    m_value = value;
    update();
}

QSize LcdReadout::sizeHint() const
{
    return QSize(kMixerColumnWidth, kScreenHeight + kCaptionHeight);
}

void LcdReadout::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRect screenRect(0, 0, width(), kScreenHeight);
    painter.fillRect(screenRect, kScreenBg);
    painter.setPen(QPen(kScreenRim, 1));
    painter.drawRect(screenRect.adjusted(0, 0, -1, -1));

    const QString shown = m_value.isEmpty() ? QStringLiteral("--") : m_value;
    const QRect textRect = screenRect.adjusted(kHorizontalPadding, 2, -kHorizontalPadding, -2);

    QFont valueFont = font();
    const QString family = lcdFontFamily();
    if (!family.isEmpty())
        valueFont.setFamily(family);

    // Auto-shrink to fit -- a fixed pixel size regardless of content length
    // overflowed/garbled longer strings ("-16.7", "100%") instead of just
    // reading small; measuring via QFontMetrics and backing off until the
    // text actually fits textRect's width fixes that for any value this
    // widget is ever asked to show.
    int pixelSize = kMaxPixelSize;
    valueFont.setPixelSize(pixelSize);
    QFontMetrics metrics(valueFont);
    while (pixelSize > kMinPixelSize && metrics.horizontalAdvance(shown) > textRect.width()) {
        --pixelSize;
        valueFont.setPixelSize(pixelSize);
        metrics = QFontMetrics(valueFont);
    }

    // One soft, bold, low-alpha pass behind the crisp digits reads as a
    // gentle glow without smearing the glyphs -- multiple small-offset
    // copies (the original approach here) look fine on a simple shape like
    // IndicatorButton's LED dot, but garble genuinely readable text at
    // these pixel sizes, especially narrow glyphs like '%'/'-'.
    QFont haloFont = valueFont;
    haloFont.setBold(true);
    QColor halo = kDigitColor;
    halo.setAlpha(70);
    painter.setFont(haloFont);
    painter.setPen(halo);
    painter.drawText(textRect, Qt::AlignCenter, shown);

    painter.setFont(valueFont);
    painter.setPen(kDigitColor);
    painter.drawText(textRect, Qt::AlignCenter, shown);

    if (!m_caption.isEmpty()) {
        const QRect captionRect(0, kScreenHeight, width(), kCaptionHeight);
        QFont captionFont = font();
        captionFont.setPixelSize(kCaptionHeight - 3);
        painter.setFont(captionFont);
        painter.setPen(kCaptionColor);
        painter.drawText(captionRect, Qt::AlignCenter, m_caption);
    }
}

} // namespace radio::ui
