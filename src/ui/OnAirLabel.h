#pragma once

#include <QColor>
#include <QLabel>

class QGraphicsDropShadowEffect;
class QSequentialAnimationGroup;

namespace radio::ui {

// "ON AIR" text framed in a neon-sign-style rectangular border, with a
// continuous breathing pulse (QGraphicsDropShadowEffect's blurRadius
// animated back and forth, tinting both text and border together) while
// something is actually playing, a dark/dim border+text (no glow) when not —
// the classic "switched off neon sign" look, not simply hidden. See
// setOnAir()'s doc comment for why this is a breathing animation rather
// than the binary on/off blink every other pulsing control in this app
// uses (Fade Now, Cart buttons).
class OnAirLabel : public QLabel {
    Q_OBJECT

public:
    explicit OnAirLabel(QWidget* parent = nullptr);

    // No-op if `onAir` matches the current state — starting/stopping the
    // animation group and restyling on every poll tick regardless of
    // whether anything changed would be wasteful and, worse, would keep
    // resetting the pulse's phase instead of letting it breathe smoothly.
    void setOnAir(bool onAir);
    bool isOnAir() const { return m_onAir; }

    // Both user-configurable (StationSettingsDialog) — no-op if unchanged,
    // same rationale as setOnAir()'s guard (avoids rebuilding the
    // stylesheet every 1s poll tick for no reason).
    void setColors(const QColor& onColor, const QColor& offColor);

private:
    void applyStyle();

    QGraphicsDropShadowEffect* m_glow;
    QSequentialAnimationGroup* m_pulseAnimation;
    bool m_onAir = false;
    QColor m_onColor;
    QColor m_offColor;
};

} // namespace radio::ui
