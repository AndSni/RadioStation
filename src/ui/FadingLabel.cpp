#include "FadingLabel.h"
#include "ConsoleTheme.h"

#include <QLinearGradient>
#include <QPainter>

#include <algorithm>

namespace radio::ui {

namespace {
constexpr int kMaxHintWidth = 240; // the widest this label ever *asks* the layout for
constexpr int kFadeWidth = 20; // overflow fades out over this many px at the right edge
} // namespace

FadingLabel::FadingLabel(QWidget* parent)
    : QLabel(parent)
{
    setTextFormat(Qt::PlainText);
    // Preferred (not Minimum/Expanding) so the layout is free to give it
    // less than its hint; Fixed vertically -- one line.
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize FadingLabel::sizeHint() const
{
    const QSize base = QLabel::sizeHint();
    return QSize(std::min(base.width(), kMaxHintWidth), base.height());
}

QSize FadingLabel::minimumSizeHint() const
{
    // Width 0 -- it can be squeezed to nothing; the fade handles the rest.
    return QSize(0, QLabel::minimumSizeHint().height());
}

void FadingLabel::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::TextAntialiasing);

    const QRect r = contentsRect();
    const QString content = text();
    painter.setFont(font());
    painter.setPen(palette().color(foregroundRole())); // picks up the stylesheet `color:`

    painter.setClipRect(r);
    painter.drawText(r, Qt::AlignLeft | Qt::AlignVCenter, content);

    // Overflow -> fade the tail out toward the bezel colour, so a long
    // value slides under the next column instead of colliding with it.
    if (fontMetrics().horizontalAdvance(content) > r.width()) {
        QColor bg = theme::kBezelBottom;
        QColor bg0 = bg;
        bg0.setAlpha(0);
        QLinearGradient fade(r.right() - kFadeWidth, 0, r.right(), 0);
        fade.setColorAt(0.0, bg0);
        fade.setColorAt(1.0, bg);
        painter.setPen(Qt::NoPen);
        painter.fillRect(QRect(r.right() - kFadeWidth, r.top(), kFadeWidth, r.height()), fade);
    }
}

} // namespace radio::ui
