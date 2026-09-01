#include "audio/DuckingController.h"

#include <QTest>

using namespace radio::audio;

class DuckingControllerTest : public QObject {
    Q_OBJECT

private slots:
    void gateEngagesImmediatelyOnceLevelCrossesThreshold();
    void gateStaysDisengagedWhileQuietAndNeverWasDucking();
    void gateStartsTheHoldTimerTheMomentItGoesQuiet();
    void gateStaysEngagedWhileWithinTheHold();
    void gateReleasesOnceTheHoldFullyElapses();
    void aLevelSpikeDuringTheHoldResetsTheQuietTimer();

    void stepMovesTowardTargetByAtMostTheMaxStep();
    void stepClampsToTargetWhenWithinOneStep();
    void stepReturnsUnchangedWhenAlreadyAtTarget();
    void stepWorksInBothDirections();
};

void DuckingControllerTest::gateEngagesImmediatelyOnceLevelCrossesThreshold()
{
    qint64 outQuietSinceMs = 0;
    const bool ducking = DuckingController::updateDuckingGate(
        /*wasDucking=*/false, /*micLevel=*/0.5, /*thresholdLinear=*/0.1, /*quietSinceMs=*/-1, /*nowMs=*/1000,
        /*releaseHoldMs=*/300, outQuietSinceMs);
    QVERIFY(ducking);
    QCOMPARE(outQuietSinceMs, -1);
}

void DuckingControllerTest::gateStaysDisengagedWhileQuietAndNeverWasDucking()
{
    qint64 outQuietSinceMs = 0;
    const bool ducking = DuckingController::updateDuckingGate(
        /*wasDucking=*/false, /*micLevel=*/0.01, /*thresholdLinear=*/0.1, /*quietSinceMs=*/-1, /*nowMs=*/1000,
        /*releaseHoldMs=*/300, outQuietSinceMs);
    QVERIFY(!ducking);
    QCOMPARE(outQuietSinceMs, -1);
}

void DuckingControllerTest::gateStartsTheHoldTimerTheMomentItGoesQuiet()
{
    qint64 outQuietSinceMs = 0;
    const bool ducking = DuckingController::updateDuckingGate(
        /*wasDucking=*/true, /*micLevel=*/0.01, /*thresholdLinear=*/0.1, /*quietSinceMs=*/-1, /*nowMs=*/1000,
        /*releaseHoldMs=*/300, outQuietSinceMs);
    QVERIFY2(ducking, "must not release on the very first quiet poll");
    QCOMPARE(outQuietSinceMs, 1000);
}

void DuckingControllerTest::gateStaysEngagedWhileWithinTheHold()
{
    qint64 outQuietSinceMs = 0;
    const bool ducking = DuckingController::updateDuckingGate(
        /*wasDucking=*/true, /*micLevel=*/0.01, /*thresholdLinear=*/0.1, /*quietSinceMs=*/1000, /*nowMs=*/1200,
        /*releaseHoldMs=*/300, outQuietSinceMs);
    QVERIFY(ducking);
    QCOMPARE(outQuietSinceMs, 1000); // unchanged -- still counting from the original quiet-start time
}

void DuckingControllerTest::gateReleasesOnceTheHoldFullyElapses()
{
    qint64 outQuietSinceMs = 0;
    const bool ducking = DuckingController::updateDuckingGate(
        /*wasDucking=*/true, /*micLevel=*/0.01, /*thresholdLinear=*/0.1, /*quietSinceMs=*/1000, /*nowMs=*/1300,
        /*releaseHoldMs=*/300, outQuietSinceMs);
    QVERIFY(!ducking);
    QCOMPARE(outQuietSinceMs, -1);
}

void DuckingControllerTest::aLevelSpikeDuringTheHoldResetsTheQuietTimer()
{
    // Simulates a real conversation: duck, go quiet for a moment (but not
    // long enough to release), speak again (the timer must reset, not
    // release later based on the FIRST quiet moment), go quiet again --
    // must only release relative to THIS most recent quiet start.
    bool ducking = true;
    qint64 quietSinceMs = -1;

    ducking = DuckingController::updateDuckingGate(ducking, 0.01, 0.1, quietSinceMs, 1000, 300, quietSinceMs);
    QVERIFY(ducking);
    QCOMPARE(quietSinceMs, 1000);

    // Still within the hold when the level spikes back up.
    ducking = DuckingController::updateDuckingGate(ducking, 0.5, 0.1, quietSinceMs, 1200, 300, quietSinceMs);
    QVERIFY(ducking);
    QCOMPARE(quietSinceMs, -1); // reset -- no longer counting a quiet period at all

    // Goes quiet again -- a fresh hold period starts from THIS moment.
    ducking = DuckingController::updateDuckingGate(ducking, 0.01, 0.1, quietSinceMs, 1250, 300, quietSinceMs);
    QVERIFY(ducking);
    QCOMPARE(quietSinceMs, 1250);

    // 300ms after the FIRST quiet moment (1000+300=1300) but only 100ms
    // after the reset one (1250) -- must still be ducking (a broken
    // implementation using the stale 1000 timestamp would release here).
    ducking = DuckingController::updateDuckingGate(ducking, 0.01, 0.1, quietSinceMs, 1350, 300, quietSinceMs);
    QVERIFY2(ducking, "must count from the most recent quiet start, not a stale one");

    // Finally, 300ms after the reset quiet start (1250+300=1550) -- releases.
    ducking = DuckingController::updateDuckingGate(ducking, 0.01, 0.1, quietSinceMs, 1550, 300, quietSinceMs);
    QVERIFY(!ducking);
}

void DuckingControllerTest::stepMovesTowardTargetByAtMostTheMaxStep()
{
    const double result = DuckingController::stepDuckGainLinear(/*current=*/1.0, /*target=*/0.1, /*maxStep=*/0.15);
    QVERIFY2(qAbs(result - 0.85) < 1e-9, qPrintable(QString::number(result)));
}

void DuckingControllerTest::stepClampsToTargetWhenWithinOneStep()
{
    const double result = DuckingController::stepDuckGainLinear(/*current=*/0.2, /*target=*/0.1, /*maxStep=*/0.15);
    QCOMPARE(result, 0.1); // must not overshoot past the target
}

void DuckingControllerTest::stepReturnsUnchangedWhenAlreadyAtTarget()
{
    const double result = DuckingController::stepDuckGainLinear(/*current=*/0.5, /*target=*/0.5, /*maxStep=*/0.15);
    QCOMPARE(result, 0.5);
}

void DuckingControllerTest::stepWorksInBothDirections()
{
    const double releasing = DuckingController::stepDuckGainLinear(/*current=*/0.1, /*target=*/1.0, /*maxStep=*/0.15);
    QVERIFY2(qAbs(releasing - 0.25) < 1e-9, qPrintable(QString::number(releasing)));
}

QTEST_MAIN(DuckingControllerTest)
#include "DuckingControllerTest.moc"
