#include "VuMeterWidget.h"
#include "LedMeterPainter.h"

#include <QPainter>
#include <QSizePolicy>
#include <QTimerEvent>
#include <algorithm>

namespace radio::ui {

namespace {
constexpr int kChannelGap = 4; // gap between the L/R bars, see paintEvent()
constexpr int kTotalWidth = 2 * kMeterChannelWidth + kChannelGap; // both channels' shared width -- see kMeterChannelWidth's own doc comment
}

VuMeterWidget::VuMeterWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(kTotalWidth - 4, 80); // slightly narrower minimum than the sizeHint -- same margin the old literal (24 vs. 28) already had
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // No timer started here -- at rest (both channels already at 0), there's
    // nothing to decay yet. setLevel() starts it lazily once a real peak
    // arrives; timerEvent() stops it again once fully decayed back to
    // silence. See both methods' comments.
}

void VuMeterWidget::setLevel(float left, float right)
{
    // Instant attack (a new peak is shown immediately), decay handled by
    // timerEvent() — a real VU meter's needle doesn't snap down on every
    // quiet sample, and a hard-instant readout is hard to read at a glance.
    m_displayLeft = std::max(m_displayLeft, left);
    m_displayRight = std::max(m_displayRight, right);
    if (left >= kClipThreshold)
        m_clipLeft = true;
    if (right >= kClipThreshold)
        m_clipRight = true;

    // The decay timer's only job is animating the display values back down
    // toward 0 — nothing to animate while both are already at rest, so it's
    // stopped whenever they reach that point (see timerEvent()) and
    // restarted here only once a real peak actually arrives, rather than
    // ticking at 25Hz forever regardless of whether anything is playing.
    if (m_decayTimerId == 0 && (m_displayLeft > 0.0f || m_displayRight > 0.0f || m_clipLeft || m_clipRight))
        m_decayTimerId = startTimer(40);

    update();
}

QSize VuMeterWidget::sizeHint() const
{
    return QSize(kTotalWidth, 100);
}

void VuMeterWidget::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_decayTimerId)
        return;

    m_displayLeft = std::max(0.0f, m_displayLeft - kDecayPerTick);
    m_displayRight = std::max(0.0f, m_displayRight - kDecayPerTick);
    if (m_displayLeft < kClipThreshold)
        m_clipLeft = false;
    if (m_displayRight < kClipThreshold)
        m_clipRight = false;
    update();

    // Fully decayed back to silence -- stop ticking until setLevel() sees a
    // real peak again (see its own comment).
    if (m_displayLeft == 0.0f && m_displayRight == 0.0f && !m_clipLeft && !m_clipRight) {
        killTimer(m_decayTimerId);
        m_decayTimerId = 0;
    }
}

void VuMeterWidget::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);

    // A clear gap between the two channels, each kMeterChannelWidth wide --
    // rather than two fills crammed edge to edge.
    const QRect leftRect(0, 0, kMeterChannelWidth, height());
    const QRect rightRect(kMeterChannelWidth + kChannelGap, 0, kMeterChannelWidth, height());

    // Discrete glowing LED segments, not one continuous fill -- see
    // LedMeterPainter's own doc comment; shared with LevelBarWidget's dB
    // meters so every bar meter in this panel reads as one consistent
    // "real hardware" language.
    paintLedMeterBar(painter, leftRect, m_displayLeft, m_clipLeft);
    paintLedMeterBar(painter, rightRect, m_displayRight, m_clipRight);
}

} // namespace radio::ui
