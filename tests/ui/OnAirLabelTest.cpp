#include "ui/OnAirLabel.h"

#include <QCoreApplication>
#include <QGraphicsDropShadowEffect>
#include <QTest>

using namespace radio::ui;

class OnAirLabelTest : public QObject {
    Q_OBJECT

private slots:
    void constructedStateIsOffWithNoGlow();
    void setOnAirTrueStartsThePulseAndBrightensText();
    void setOnAirFalseStopsThePulseAndZeroesGlow();
    void settingTheSameStateTwiceDoesNotRestartThePulse();
    void setColorsUpdatesStyleAndGlowTint();
    void setColorsWithSameValuesIsNoOp();
};

namespace {
QGraphicsDropShadowEffect* glowOf(OnAirLabel& label)
{
    return qobject_cast<QGraphicsDropShadowEffect*>(label.graphicsEffect());
}
}

void OnAirLabelTest::constructedStateIsOffWithNoGlow()
{
    OnAirLabel label;
    QVERIFY(!label.isOnAir());
    auto* glow = glowOf(label);
    QVERIFY(glow != nullptr);
    QCOMPARE(glow->blurRadius(), 0.0);
}

void OnAirLabelTest::setOnAirTrueStartsThePulseAndBrightensText()
{
    OnAirLabel label;
    label.setOnAir(true);
    QVERIFY(label.isOnAir());
    QVERIFY(label.styleSheet().contains(QStringLiteral("#ff3b3b")));

    // One leg of the breathing animation is 1200ms — after a fraction of
    // that, blurRadius should have moved off its rest position if the
    // animation is actually running (matches CrossfaderWidgetTest's own
    // qWait-then-sample approach for verifying a timer/animation-driven
    // effect, since QAbstractAnimation::state() isn't asserted anywhere
    // else in this codebase either).
    QTest::qWait(300);
    QVERIFY(glowOf(label)->blurRadius() > 0.0);
}

void OnAirLabelTest::setOnAirFalseStopsThePulseAndZeroesGlow()
{
    OnAirLabel label;
    label.setOnAir(true);
    QTest::qWait(300);
    QVERIFY(glowOf(label)->blurRadius() > 0.0);

    label.setOnAir(false);
    QVERIFY(!label.isOnAir());
    QVERIFY(label.styleSheet().contains(QStringLiteral("#5c1c1c")));
    QCOMPARE(glowOf(label)->blurRadius(), 0.0);

    // Stays at 0 — the animation must have actually stopped, not just been
    // reset once and left running to bounce back on its own.
    QTest::qWait(300);
    QCOMPARE(glowOf(label)->blurRadius(), 0.0);
}

void OnAirLabelTest::settingTheSameStateTwiceDoesNotRestartThePulse()
{
    OnAirLabel label;
    label.setOnAir(true);
    QTest::qWait(1000); // partway into the first leg (grow: min -> max, 1200ms)
    const qreal midBlur = glowOf(label)->blurRadius();
    QVERIFY(midBlur > 0.0);

    label.setOnAir(true); // no-op: must not reset the animation's phase
    // A restarted animation would jump back to the grow leg's start value
    // (kGlowMinBlur = 8); the no-op guard means blurRadius keeps moving
    // from wherever it already was instead of snapping back down to 8.
    QVERIFY(glowOf(label)->blurRadius() >= midBlur - 1.0);
}

void OnAirLabelTest::setColorsUpdatesStyleAndGlowTint()
{
    OnAirLabel label;
    label.setColors(QColor(0x00, 0xff, 0x00), QColor(0x00, 0x30, 0x00));
    // Off state (current) reflects the new off color immediately.
    QVERIFY(label.styleSheet().contains(QStringLiteral("#003000")));

    label.setOnAir(true);
    QVERIFY(label.styleSheet().contains(QStringLiteral("#00ff00")));
    QCOMPARE(glowOf(label)->color(), QColor(0x00, 0xff, 0x00));
}

void OnAirLabelTest::setColorsWithSameValuesIsNoOp()
{
    OnAirLabel label;
    label.setOnAir(true);
    QTest::qWait(1000);
    const qreal midBlur = glowOf(label)->blurRadius();

    // Default colors, unchanged — must not disturb the running animation's phase.
    label.setColors(QColor(0xff, 0x3b, 0x3b), QColor(0x5c, 0x1c, 0x1c));
    QVERIFY(glowOf(label)->blurRadius() >= midBlur - 1.0);
}

QTEST_MAIN(OnAirLabelTest)
#include "OnAirLabelTest.moc"
