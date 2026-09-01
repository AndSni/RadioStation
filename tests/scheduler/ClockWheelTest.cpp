#include "scheduler/ClockWheel.h"

#include <QTest>

using namespace radio::scheduler;
using namespace radio::db;

namespace {
ClockElementRecord makeElement(const QString& type, int minuteOffset = -1, const QString& timingMode = QStringLiteral("soft"), int itemCount = 1)
{
    ClockElementRecord element;
    element.elementType = type;
    element.minuteOffset = minuteOffset;
    element.timingMode = timingMode;
    element.itemCount = itemCount;
    return element;
}
}

class ClockWheelTest : public QObject {
    Q_OBJECT

private slots:
    void flowElementIsAlwaysReadyToPlay();
    void softTimedElementWaitsBeforeItsDue();
    void softTimedElementPlaysExactlyAtDue();
    void softTimedElementPlaysWhenLate();
    void hardTimedElementWaitsBeforeItsDue();
    void hardTimedElementForcesFadeExactlyAtDue();
    void hardTimedElementForcesFadeWhenLate();
    void positionPastEndIsExhausted();
    void negativePositionIsExhausted();
    void emptyElementListIsExhausted();
    void waitForTimeReportsExactSecondsRemaining();
    void minuteOffsetZeroAtSecondZeroPlaysImmediately();

    void isElementCompleteForMusicRequiresItemCount();
    void isElementCompleteForCartIgnoresItemCountAfterOneFire();
    void isElementCompleteTreatsZeroItemCountAsOne();

    void firstEligiblePositionReturnsZeroForAFlowStart();
    void firstEligiblePositionSkipsElementsFarInThePast();
    void firstEligiblePositionKeepsElementsWithinGraceWindow();
    void firstEligiblePositionReturnsSizeWhenEveryElementIsStale();
    void firstEligiblePositionPrefersEarliestEligibleAmongMixedTypes();
};

void ClockWheelTest::flowElementIsAlwaysReadyToPlay()
{
    const QVector<ClockElementRecord> elements = { makeElement(QStringLiteral("music_playlist")) };
    for (int second : { 0, 1799, 3599 }) {
        const auto decision = ClockWheel::evaluate(elements, 0, second);
        QCOMPARE(decision.action, ClockWheelAction::PlayElement);
        QCOMPARE(decision.elementIndex, 0);
    }
}

void ClockWheelTest::softTimedElementWaitsBeforeItsDue()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 15, QStringLiteral("soft"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 10 * 60);
    QCOMPARE(decision.action, ClockWheelAction::WaitForTime);
    QCOMPARE(decision.secondsUntilDue, 5 * 60);
}

void ClockWheelTest::softTimedElementPlaysExactlyAtDue()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 15, QStringLiteral("soft"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 15 * 60);
    QCOMPARE(decision.action, ClockWheelAction::PlayElement);
}

void ClockWheelTest::softTimedElementPlaysWhenLate()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 15, QStringLiteral("soft"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 20 * 60);
    QCOMPARE(decision.action, ClockWheelAction::PlayElement);
}

void ClockWheelTest::hardTimedElementWaitsBeforeItsDue()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 15, QStringLiteral("hard"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 10 * 60);
    QCOMPARE(decision.action, ClockWheelAction::WaitForTime);
    QCOMPARE(decision.secondsUntilDue, 5 * 60);
}

void ClockWheelTest::hardTimedElementForcesFadeExactlyAtDue()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 0, QStringLiteral("hard"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 0);
    QCOMPARE(decision.action, ClockWheelAction::ForceFadeNow);
}

void ClockWheelTest::hardTimedElementForcesFadeWhenLate()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 0, QStringLiteral("hard"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 90);
    QCOMPARE(decision.action, ClockWheelAction::ForceFadeNow);
}

void ClockWheelTest::positionPastEndIsExhausted()
{
    const QVector<ClockElementRecord> elements = { makeElement(QStringLiteral("music_playlist")) };
    QCOMPARE(ClockWheel::evaluate(elements, 1, 0).action, ClockWheelAction::Exhausted);
}

void ClockWheelTest::negativePositionIsExhausted()
{
    const QVector<ClockElementRecord> elements = { makeElement(QStringLiteral("music_playlist")) };
    QCOMPARE(ClockWheel::evaluate(elements, -1, 0).action, ClockWheelAction::Exhausted);
}

void ClockWheelTest::emptyElementListIsExhausted()
{
    QCOMPARE(ClockWheel::evaluate({}, 0, 0).action, ClockWheelAction::Exhausted);
}

void ClockWheelTest::waitForTimeReportsExactSecondsRemaining()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_random"), 28, QStringLiteral("soft"))
    };
    const auto decision = ClockWheel::evaluate(elements, 0, 27 * 60 + 45);
    QCOMPARE(decision.action, ClockWheelAction::WaitForTime);
    QCOMPARE(decision.secondsUntilDue, 15);
}

void ClockWheelTest::minuteOffsetZeroAtSecondZeroPlaysImmediately()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 0, QStringLiteral("soft"))
    };
    QCOMPARE(ClockWheel::evaluate(elements, 0, 0).action, ClockWheelAction::PlayElement);
}

void ClockWheelTest::isElementCompleteForMusicRequiresItemCount()
{
    const ClockElementRecord element = makeElement(QStringLiteral("music_playlist"), -1, QStringLiteral("soft"), 3);
    QVERIFY(!ClockWheel::isElementComplete(element, 0));
    QVERIFY(!ClockWheel::isElementComplete(element, 2));
    QVERIFY(ClockWheel::isElementComplete(element, 3));
    QVERIFY(ClockWheel::isElementComplete(element, 4));
}

void ClockWheelTest::isElementCompleteForCartIgnoresItemCountAfterOneFire()
{
    const ClockElementRecord element = makeElement(QStringLiteral("cart_color"), -1, QStringLiteral("soft"), 5);
    QVERIFY(!ClockWheel::isElementComplete(element, 0));
    QVERIFY(ClockWheel::isElementComplete(element, 1));
}

void ClockWheelTest::isElementCompleteTreatsZeroItemCountAsOne()
{
    const ClockElementRecord element = makeElement(QStringLiteral("music_smart_playlist"), -1, QStringLiteral("soft"), 0);
    QVERIFY(!ClockWheel::isElementComplete(element, 0));
    QVERIFY(ClockWheel::isElementComplete(element, 1));
}

void ClockWheelTest::firstEligiblePositionReturnsZeroForAFlowStart()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("music_playlist")), makeElement(QStringLiteral("cart_random"), 30, QStringLiteral("soft"))
    };
    QCOMPARE(ClockWheel::firstEligiblePosition(elements, 45 * 60, 120), 0);
}

void ClockWheelTest::firstEligiblePositionSkipsElementsFarInThePast()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 5, QStringLiteral("hard")), // due at 5:00, now way past grace
        makeElement(QStringLiteral("music_playlist")),
    };
    // secondOfHour = 45 minutes; element 0 was due at 5:00 -- 2400s ago, far beyond a 120s grace window.
    QCOMPARE(ClockWheel::firstEligiblePosition(elements, 45 * 60, 120), 1);
}

void ClockWheelTest::firstEligiblePositionKeepsElementsWithinGraceWindow()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 10, QStringLiteral("hard")), // due at 10:00
    };
    // secondOfHour = 10:01 -- only 60s late, within a 120s grace window.
    QCOMPARE(ClockWheel::firstEligiblePosition(elements, 10 * 60 + 60, 120), 0);
}

void ClockWheelTest::firstEligiblePositionReturnsSizeWhenEveryElementIsStale()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 5, QStringLiteral("hard")),
        makeElement(QStringLiteral("cart_specific"), 10, QStringLiteral("hard")),
    };
    QCOMPARE(ClockWheel::firstEligiblePosition(elements, 59 * 60, 120), elements.size());
}

void ClockWheelTest::firstEligiblePositionPrefersEarliestEligibleAmongMixedTypes()
{
    const QVector<ClockElementRecord> elements = {
        makeElement(QStringLiteral("cart_specific"), 5, QStringLiteral("hard")), // stale by now
        makeElement(QStringLiteral("cart_specific"), 20, QStringLiteral("soft")), // still upcoming
        makeElement(QStringLiteral("music_playlist")), // flow, always eligible
    };
    QCOMPARE(ClockWheel::firstEligiblePosition(elements, 15 * 60, 120), 1);
}

QTEST_MAIN(ClockWheelTest)
#include "ClockWheelTest.moc"
