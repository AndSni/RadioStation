#include "OnAirLabel.h"

#include <QGraphicsDropShadowEffect>
#include <QPropertyAnimation>
#include <QSequentialAnimationGroup>

namespace radio::ui {

namespace {
constexpr qreal kGlowMinBlur = 8.0;
constexpr qreal kGlowMaxBlur = 28.0;
constexpr int kPulseLegDurationMs = 1200; // one leg (dim->bright or bright->dim) of the breathing cycle

const QColor kDefaultOnColor(0xff, 0x3b, 0x3b); // vivid, illuminated
const QColor kDefaultOffColor(0x5c, 0x1c, 0x1c); // dim, unlit outline - not simply hidden
}

OnAirLabel::OnAirLabel(QWidget* parent)
    : QLabel(QStringLiteral("ON AIR"), parent)
    , m_onColor(kDefaultOnColor)
    , m_offColor(kDefaultOffColor)
{
    setAlignment(Qt::AlignCenter);
    QFont f = font();
    f.setBold(true);
    setFont(f);

    m_glow = new QGraphicsDropShadowEffect(this);
    m_glow->setOffset(0, 0);
    m_glow->setColor(m_onColor);
    m_glow->setBlurRadius(0);
    setGraphicsEffect(m_glow);

    auto* grow = new QPropertyAnimation(m_glow, "blurRadius", this);
    grow->setStartValue(kGlowMinBlur);
    grow->setEndValue(kGlowMaxBlur);
    grow->setDuration(kPulseLegDurationMs);

    auto* shrink = new QPropertyAnimation(m_glow, "blurRadius", this);
    shrink->setStartValue(kGlowMaxBlur);
    shrink->setEndValue(kGlowMinBlur);
    shrink->setDuration(kPulseLegDurationMs);

    m_pulseAnimation = new QSequentialAnimationGroup(this);
    m_pulseAnimation->addAnimation(grow);
    m_pulseAnimation->addAnimation(shrink);
    m_pulseAnimation->setLoopCount(-1); // breathes forever while running - see setOnAir()

    applyStyle(); // starts off; setOnAir() isn't called here since m_onAir already defaults to false (its own no-op guard would skip re-applying this)
}

void OnAirLabel::setOnAir(bool onAir)
{
    if (onAir == m_onAir)
        return;
    m_onAir = onAir;

    if (onAir) {
        m_pulseAnimation->start();
    } else {
        m_pulseAnimation->stop();
        m_glow->setBlurRadius(0);
    }
    applyStyle();
}

void OnAirLabel::setColors(const QColor& onColor, const QColor& offColor)
{
    if (onColor == m_onColor && offColor == m_offColor)
        return;
    m_onColor = onColor;
    m_offColor = offColor;
    m_glow->setColor(m_onColor); // the glow always tints toward the "on" color; the off state has no glow anyway (blurRadius 0)
    applyStyle();
}

void OnAirLabel::applyStyle()
{
    // A real neon sign's tube and its rectangular frame are the same color
    // and light together — border matches text color in both states.
    const QColor& color = m_onAir ? m_onColor : m_offColor;
    setStyleSheet(QStringLiteral("color: %1; border: 2px solid %1; border-radius: 6px; padding: 3px 10px;")
                       .arg(color.name()));
}

} // namespace radio::ui
