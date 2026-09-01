#include "AudioPillPainter.h"

#include <QFontMetrics>
#include <QPainter>
#include <QRect>

#include <algorithm>
#include <cmath>

namespace radio::ui::AudioPillPainter {

const QColor kNeutralColor(QStringLiteral("#4a443b")); // warm dark grey-brown, matches the console bezel

QColor contrastingTextColor(const QColor& background)
{
    // Standard relative-luminance threshold (ITU-R BT.601 coefficients).
    const double luminance = (0.299 * background.red() + 0.587 * background.green() + 0.114 * background.blue()) / 255.0;
    return luminance > 0.6 ? QColor(Qt::black) : QColor(Qt::white);
}

void paint(QPainter& painter, const QRect& rect, const QString& text, const QColor& background)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    const QRect pillRect = rect.adjusted(2, 2, -2, -2);
    painter.setPen(Qt::NoPen);
    painter.setBrush(background);
    const int radius = pillRect.height() / 2;
    painter.drawRoundedRect(pillRect, radius, radius);

    painter.setPen(contrastingTextColor(background));
    const QRect textRect = pillRect.adjusted(radius, 0, -radius, 0);
    const QFontMetrics metrics(painter.font());
    painter.drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft, metrics.elidedText(text, Qt::ElideRight, textRect.width()));

    painter.restore();
}

QColor blend(const QColor& base, const QColor& tint, double tintRatio)
{
    tintRatio = std::clamp(tintRatio, 0.0, 1.0);
    const auto mix
        = [tintRatio](int baseChannel, int tintChannel) { return static_cast<int>(std::lround(baseChannel * (1.0 - tintRatio) + tintChannel * tintRatio)); };
    return QColor(mix(base.red(), tint.red()), mix(base.green(), tint.green()), mix(base.blue(), tint.blue()));
}

} // namespace radio::ui::AudioPillPainter
