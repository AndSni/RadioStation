#include "LedMeterPainter.h"
#include "ConsoleTheme.h"

#include <QColor>
#include <QPainter>
#include <QRect>
#include <algorithm>
#include <cmath>

namespace radio::ui {

namespace {
constexpr int kSegmentCount = 16;
// Gap between segments = 1/3 of a lit segment's own height. Each slot is
// ledHeight + gap; solving gap = ledHeight/3 with slot = ledHeight + gap
// gives gap = slot/4, i.e. 0.25 of the slot (not the LED) height -- that's
// the ratio actually applied below.
constexpr double kSegmentGapRatio = 0.25;

// Zone thresholds by segment index counting from the bottom (0) -- top 2
// segments red (clip/over), next 3 amber, remaining 11 green. Matches the
// proportions of a typical broadcast LED ladder meter (most of the scale is
// green, with a compact amber "hot" zone and a small red peak zone at top).
QColor segmentBaseColor(int indexFromBottom)
{
    if (indexFromBottom >= kSegmentCount - 2)
        return theme::kMeterRed;
    if (indexFromBottom >= kSegmentCount - 5)
        return theme::kMeterAmber;
    return theme::kMeterGreen;
}
}

void paintLedMeterBar(QPainter& painter, const QRect& rect, float normalizedLevel, bool clipped)
{
    painter.fillRect(rect, theme::kMeterBackground);

    const double segmentHeight = static_cast<double>(rect.height()) / kSegmentCount;
    const int gap = std::max(1, static_cast<int>(segmentHeight * kSegmentGapRatio));

    const float clamped = std::clamp(normalizedLevel, 0.0f, 1.0f);
    const int litCount = clipped ? kSegmentCount : static_cast<int>(std::round(clamped * kSegmentCount));

    for (int i = 0; i < kSegmentCount; ++i) {
        const int top = rect.bottom() - static_cast<int>((i + 1) * segmentHeight) + gap / 2;
        const int segmentRectHeight = std::max(1, static_cast<int>(segmentHeight) - gap);
        const QRect segmentRect(rect.left(), top, rect.width(), segmentRectHeight);

        const QColor base = segmentBaseColor(i);
        if (clipped || i < litCount) {
            // Soft halo behind the crisp fill -- same cheap "extra low-alpha
            // pass behind the real one" technique as LcdReadout's digit glow
            // and IndicatorButton's LED, rather than a QGraphicsEffect per
            // segment (a single meter can have 16 of these, and a panel has
            // roughly a dozen meters at once -- an effect per segment would
            // be a real, avoidable cost multiplied across all of them).
            QColor halo = base;
            halo.setAlpha(90);
            painter.fillRect(segmentRect.adjusted(-2, -2, 2, 2), halo);
            painter.fillRect(segmentRect, base);
        } else {
            painter.fillRect(segmentRect, base.darker(380));
        }
    }

    painter.setPen(theme::kMeterBorder);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
}

} // namespace radio::ui
