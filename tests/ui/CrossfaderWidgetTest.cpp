#include "ui/CrossfaderWidget.h"
#include "ui/ConsoleButton.h"
#include "ui/LedIndicator.h"

#include "audio/AudioEngine.h"
#include "audio/CrossfadeController.h"

#include "db/Database.h"

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;
using namespace radio::db;

namespace {
QPushButton* findButtonByText(QWidget* parent, const QString& text)
{
    for (auto* button : parent->findChildren<QPushButton*>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}
}

class CrossfaderWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void fadeNowDisabledUntilBothDecksReady();
    void clickingFadeNowStartsACrossfade();
    void fadeNowDisabledWhileCrossfadeInProgress();
    void curveButtonsAreExclusiveAndDriveTheController();
    void deckLabelsReflectActiveDeckAfterACrossfade();
    void deckEndLabelsArePlainText();
    void fadeControlButtonsShareOneUniformSize();
};

void CrossfaderWidgetTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationCrossfaderWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationCrossfaderWidgetTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void CrossfaderWidgetTest::fadeNowDisabledUntilBothDecksReady()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    // tone440.wav is only ~4.6s, shorter than the default 5s crossfade lead
    // time — without this, a real automatic crossfade could start during
    // this test's own wait window and flip the button back to disabled
    // right as it's being observed (same gotcha CrossfadeControllerTest hit
    // — see its manualFadeAvailabilityChangedFiresOnlyOnTransition()).
    controller.setCrossfadeLeadMs(1);

    CrossfaderWidget widget(&controller);
    widget.show();

    auto* fadeNowButton = findButtonByText(&widget, QStringLiteral("Fade Now"));
    QVERIFY(fadeNowButton != nullptr);
    QVERIFY(!fadeNowButton->isEnabled()); // nothing loaded yet

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));

    QTRY_VERIFY_WITH_TIMEOUT(fadeNowButton->isEnabled(), 3000);

    engine.shutdown();
}

void CrossfaderWidgetTest::clickingFadeNowStartsACrossfade()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1); // isolate from a real automatic trigger

    CrossfaderWidget widget(&controller);
    widget.show();

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));

    auto* fadeNowButton = findButtonByText(&widget, QStringLiteral("Fade Now"));
    QVERIFY(fadeNowButton != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fadeNowButton->isEnabled(), 3000);

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    QTest::mouseClick(fadeNowButton, Qt::LeftButton);
    QCOMPARE(startedSpy.count(), 1);

    engine.shutdown();
}

void CrossfaderWidgetTest::fadeNowDisabledWhileCrossfadeInProgress()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1);
    controller.setFadeDurationMs(2000); // long enough to observe the disabled state mid-fade

    CrossfaderWidget widget(&controller);
    widget.show();

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));

    auto* fadeNowButton = qobject_cast<ConsoleButton*>(findButtonByText(&widget, QStringLiteral("Fade Now")));
    QVERIFY(fadeNowButton != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(fadeNowButton->isEnabled(), 3000);
    const QSize sizeBeforeFade = fadeNowButton->size();

    QTest::mouseClick(fadeNowButton, Qt::LeftButton);
    QVERIFY(!fadeNowButton->isEnabled());
    // onAutoCrossfadeStarted() puts the button into its blinking state
    // synchronously; ConsoleButton pulses its own light bar internally
    // (no stylesheet swap), so the button's on-screen size can't shift.
    QVERIFY(fadeNowButton->isBlinking());
    QCOMPARE(fadeNowButton->size(), sizeBeforeFade);

    // Still blinking part-way through the (2s) fade, still the same size.
    QTest::qWait(500);
    QVERIFY(fadeNowButton->isBlinking());
    QCOMPARE(fadeNowButton->size(), sizeBeforeFade);

    engine.shutdown();
}

void CrossfaderWidgetTest::curveButtonsAreExclusiveAndDriveTheController()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    CrossfaderWidget widget(&controller);
    widget.show();

    auto* equalPowerButton = findButtonByText(&widget, QStringLiteral("Equal Power"));
    auto* linearButton = findButtonByText(&widget, QStringLiteral("Linear"));
    auto* slowDownButton = findButtonByText(&widget, QStringLiteral("Slow Down"));
    auto* speedUpButton = findButtonByText(&widget, QStringLiteral("Speed Up"));
    QVERIFY(equalPowerButton != nullptr);
    QVERIFY(linearButton != nullptr);
    QVERIFY(slowDownButton != nullptr);
    QVERIFY(speedUpButton != nullptr);

    QVERIFY(equalPowerButton->isChecked()); // matches CrossfadeController's default
    QCOMPARE(controller.fadeCurve(), RampCurve::EqualPower);

    QTest::mouseClick(linearButton, Qt::LeftButton);
    QVERIFY(linearButton->isChecked());
    QVERIFY(!equalPowerButton->isChecked()); // exclusive - selecting one deselects the others
    QCOMPARE(controller.fadeCurve(), RampCurve::Linear);

    QTest::mouseClick(slowDownButton, Qt::LeftButton);
    QVERIFY(slowDownButton->isChecked());
    QVERIFY(!linearButton->isChecked());
    QCOMPARE(controller.fadeCurve(), RampCurve::SlowDown);

    QTest::mouseClick(speedUpButton, Qt::LeftButton);
    QVERIFY(speedUpButton->isChecked());
    QVERIFY(!slowDownButton->isChecked());
    QCOMPARE(controller.fadeCurve(), RampCurve::SpeedUp);

    engine.shutdown();
}

void CrossfaderWidgetTest::deckLabelsReflectActiveDeckAfterACrossfade()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1);
    controller.setFadeDurationMs(200);

    CrossfaderWidget widget(&controller);
    widget.show();

    // "A" / "B" are plain text labels at each end; which deck is live shows
    // on an LedIndicator beside each.
    auto* ledA = widget.findChild<LedIndicator*>(QStringLiteral("deckALed"));
    auto* ledB = widget.findChild<LedIndicator*>(QStringLiteral("deckBLed"));
    QVERIFY(ledA != nullptr);
    QVERIFY(ledB != nullptr);

    // A starts active.
    QVERIFY(ledA->isOn());
    QVERIFY(!ledB->isOn());

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));

    QSignalSpy finishedSpy(&controller, &CrossfadeController::autoCrossfadeFinished);
    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 8000); // the ~1ms lead time triggers this automatically

    QVERIFY(ledB->isOn());
    QVERIFY(!ledA->isOn());

    engine.shutdown();
}

void CrossfaderWidgetTest::deckEndLabelsArePlainText()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    CrossfaderWidget widget(&controller);
    widget.show();

    QLabel* labelA = nullptr;
    QLabel* labelB = nullptr;
    for (auto* label : widget.findChildren<QLabel*>()) {
        if (label->text() == QStringLiteral("A"))
            labelA = label;
        else if (label->text() == QStringLiteral("B"))
            labelB = label;
    }
    QVERIFY(labelA != nullptr);
    QVERIFY(labelB != nullptr);
    // No longer pill "buttons" — just end captions. The active-deck state
    // lives on the LedIndicators, not on these labels.
    QVERIFY(!labelA->styleSheet().contains(QStringLiteral("border")));

    engine.shutdown();
}

void CrossfaderWidgetTest::fadeControlButtonsShareOneUniformSize()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    CrossfaderWidget widget(&controller);
    widget.show();

    auto* fadeNowButton = findButtonByText(&widget, QStringLiteral("Fade Now"));
    auto* equalPowerButton = findButtonByText(&widget, QStringLiteral("Equal Power"));
    auto* linearButton = findButtonByText(&widget, QStringLiteral("Linear"));
    auto* slowDownButton = findButtonByText(&widget, QStringLiteral("Slow Down"));
    auto* speedUpButton = findButtonByText(&widget, QStringLiteral("Speed Up"));
    QVERIFY(fadeNowButton != nullptr);
    QVERIFY(equalPowerButton != nullptr);
    QVERIFY(linearButton != nullptr);
    QVERIFY(slowDownButton != nullptr);
    QVERIFY(speedUpButton != nullptr);

    // Different text lengths ("Equal Power" vs "Linear") must not translate
    // into different button sizes.
    QCOMPARE(fadeNowButton->size(), equalPowerButton->size());
    QCOMPARE(equalPowerButton->size(), linearButton->size());
    QCOMPARE(linearButton->size(), slowDownButton->size());
    QCOMPARE(slowDownButton->size(), speedUpButton->size());

    engine.shutdown();
}

QTEST_MAIN(CrossfaderWidgetTest)
#include "CrossfaderWidgetTest.moc"
