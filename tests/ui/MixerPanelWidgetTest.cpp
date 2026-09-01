#include "ui/MixerPanelWidget.h"
#include "ui/RotaryKnob.h"

#include "audio/AudioEngine.h"

#include <QCoreApplication>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;

class MixerPanelWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void deckEqSliderPersistsAcrossRestart();
    void deckVolumeAndAutoCompPersistAcrossRestart();
    void masterControlsPersistAcrossRestart();
    void freshInstanceWithNoSavedSettingsUsesDefaults();
};

void MixerPanelWidgetTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationMixerPanelWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationMixerPanelWidgetTest"));
}

void MixerPanelWidgetTest::init()
{
    QSettings().clear(); // each test starts from a clean slate, independent of what earlier tests saved
}

void MixerPanelWidgetTest::deckEqSliderPersistsAcrossRestart()
{
    // Each deck's own EQ is just a Bass/Treble knob pair now (see
    // DeckStrip::bassKnob's doc comment) — band 4 is Treble (the full
    // per-source EQ still has 5 bands, see setEqBandGain()'s doc comment;
    // the Deck strip just only ever drives the two end shelves).
    {
        AudioEngine engine;
        engine.start();
        MixerPanelWidget widget(&engine);
        auto* knob = widget.findChild<RotaryKnob*>(QStringLiteral("deckTrebleKnob_A"));
        QVERIFY(knob != nullptr);
        knob->setValue(6);
        engine.shutdown();
    } // widget + engine destroyed - simulates the app quitting

    AudioEngine engine2;
    engine2.start();
    MixerPanelWidget widget2(&engine2);
    auto* knob2 = widget2.findChild<RotaryKnob*>(QStringLiteral("deckTrebleKnob_A"));
    QVERIFY(knob2 != nullptr);
    QCOMPARE(knob2->value(), 6);
    QCOMPARE(engine2.deckEqBandGain(QStringLiteral("A"), 4), 6.0);
    engine2.shutdown();
}

void MixerPanelWidgetTest::deckVolumeAndAutoCompPersistAcrossRestart()
{
    {
        AudioEngine engine;
        engine.start();
        MixerPanelWidget widget(&engine);
        auto* volumeKnob = widget.findChild<RotaryKnob*>(QStringLiteral("deckVolumeKnob_B"));
        auto* autoCompButton = widget.findChild<QPushButton*>(QStringLiteral("deckAutoCompButton_B"));
        QVERIFY(volumeKnob != nullptr);
        QVERIFY(autoCompButton != nullptr);
        volumeKnob->setValue(75);
        autoCompButton->setChecked(true);
        engine.shutdown();
    }

    AudioEngine engine2;
    engine2.start();
    MixerPanelWidget widget2(&engine2);
    auto* volumeKnob2 = widget2.findChild<RotaryKnob*>(QStringLiteral("deckVolumeKnob_B"));
    auto* autoCompButton2 = widget2.findChild<QPushButton*>(QStringLiteral("deckAutoCompButton_B"));
    QVERIFY(volumeKnob2 != nullptr);
    QVERIFY(autoCompButton2 != nullptr);
    QCOMPARE(volumeKnob2->value(), 75);
    QVERIFY(autoCompButton2->isChecked());
    QVERIFY(engine2.deckAutoGainCompensationEnabled(QStringLiteral("B")));
    engine2.shutdown();
}

void MixerPanelWidgetTest::masterControlsPersistAcrossRestart()
{
    {
        AudioEngine engine;
        engine.start();
        MixerPanelWidget widget(&engine);
        auto* bassBoostSlider = widget.findChild<QSlider*>(QStringLiteral("masterEqSlider_0"));
        auto* trebleSlider = widget.findChild<QSlider*>(QStringLiteral("masterEqSlider_5"));
        auto* masterVolumeKnob = widget.findChild<RotaryKnob*>(QStringLiteral("masterVolumeKnob"));
        auto* loudnessButton = widget.findChild<QPushButton*>(QStringLiteral("masterLoudnessButton"));
        auto* agcRefSlider = widget.findChild<QSlider*>(QStringLiteral("masterAgcRefSlider"));
        auto* highPassButton = widget.findChild<QPushButton*>(QStringLiteral("masterHighPassButton"));
        auto* limiterCeilingSlider = widget.findChild<QSlider*>(QStringLiteral("masterLimiterCeilingSlider"));
        auto* levelerButton = widget.findChild<QPushButton*>(QStringLiteral("masterLevelerButton"));
        auto* levelerRangeSlider = widget.findChild<QSlider*>(QStringLiteral("masterLevelerRangeSlider"));
        QVERIFY(bassBoostSlider != nullptr);
        QVERIFY(trebleSlider != nullptr);
        QVERIFY(masterVolumeKnob != nullptr);
        QVERIFY(loudnessButton != nullptr);
        QVERIFY(agcRefSlider != nullptr);
        QVERIFY(highPassButton != nullptr);
        QVERIFY(limiterCeilingSlider != nullptr);
        QVERIFY(levelerButton != nullptr);
        QVERIFY(levelerRangeSlider != nullptr);
        bassBoostSlider->setValue(3);
        trebleSlider->setValue(4);
        masterVolumeKnob->setValue(-2);
        loudnessButton->setChecked(true);
        agcRefSlider->setValue(-7);
        highPassButton->setChecked(true);
        limiterCeilingSlider->setValue(-2);
        levelerButton->setChecked(false); // starts true - toggling off is the real change to verify
        levelerRangeSlider->setValue(5);
        engine.shutdown();
    }

    AudioEngine engine2;
    engine2.start();
    MixerPanelWidget widget2(&engine2);
    auto* bassBoostSlider2 = widget2.findChild<QSlider*>(QStringLiteral("masterEqSlider_0"));
    auto* trebleSlider2 = widget2.findChild<QSlider*>(QStringLiteral("masterEqSlider_5"));
    auto* masterVolumeKnob2 = widget2.findChild<RotaryKnob*>(QStringLiteral("masterVolumeKnob"));
    auto* loudnessButton2 = widget2.findChild<QPushButton*>(QStringLiteral("masterLoudnessButton"));
    auto* agcRefSlider2 = widget2.findChild<QSlider*>(QStringLiteral("masterAgcRefSlider"));
    auto* highPassButton2 = widget2.findChild<QPushButton*>(QStringLiteral("masterHighPassButton"));
    auto* limiterCeilingSlider2 = widget2.findChild<QSlider*>(QStringLiteral("masterLimiterCeilingSlider"));
    auto* levelerButton2 = widget2.findChild<QPushButton*>(QStringLiteral("masterLevelerButton"));
    auto* levelerRangeSlider2 = widget2.findChild<QSlider*>(QStringLiteral("masterLevelerRangeSlider"));
    QCOMPARE(bassBoostSlider2->value(), 3);
    QCOMPARE(trebleSlider2->value(), 4);
    QCOMPARE(masterVolumeKnob2->value(), -2);
    QVERIFY(loudnessButton2->isChecked());
    QCOMPARE(agcRefSlider2->value(), -7);
    QCOMPARE(engine2.masterTargetDb(), -7.0);
    QVERIFY(highPassButton2->isChecked());
    QCOMPARE(limiterCeilingSlider2->value(), -2);
    QVERIFY(!levelerButton2->isChecked());
    QCOMPARE(levelerRangeSlider2->value(), 5);
    engine2.shutdown();
}

void MixerPanelWidgetTest::freshInstanceWithNoSavedSettingsUsesDefaults()
{
    AudioEngine engine;
    engine.start();
    MixerPanelWidget widget(&engine);

    auto* bassKnob = widget.findChild<RotaryKnob*>(QStringLiteral("deckBassKnob_A"));
    auto* volumeKnob = widget.findChild<RotaryKnob*>(QStringLiteral("deckVolumeKnob_A"));
    auto* autoCompButton = widget.findChild<QPushButton*>(QStringLiteral("deckAutoCompButton_A"));
    auto* loudnessButton = widget.findChild<QPushButton*>(QStringLiteral("masterLoudnessButton"));
    auto* highPassButton = widget.findChild<QPushButton*>(QStringLiteral("masterHighPassButton"));
    auto* trebleSlider = widget.findChild<QSlider*>(QStringLiteral("masterEqSlider_5"));
    auto* limiterCeilingSlider = widget.findChild<QSlider*>(QStringLiteral("masterLimiterCeilingSlider"));
    auto* levelerButton = widget.findChild<QPushButton*>(QStringLiteral("masterLevelerButton"));
    auto* levelerRangeSlider = widget.findChild<QSlider*>(QStringLiteral("masterLevelerRangeSlider"));
    QCOMPARE(bassKnob->value(), 0);
    QCOMPARE(volumeKnob->value(), 100);
    QVERIFY(!autoCompButton->isChecked());
    QVERIFY(!loudnessButton->isChecked());
    QVERIFY(!highPassButton->isChecked());
    QCOMPARE(trebleSlider->value(), 0);
    QCOMPARE(limiterCeilingSlider->value(), -1);
    QVERIFY(levelerButton->isChecked()); // defaults ON, unlike Loudness/High-Pass
    QCOMPARE(levelerRangeSlider->value(), 3);

    engine.shutdown();
}

QTEST_MAIN(MixerPanelWidgetTest)
#include "MixerPanelWidgetTest.moc"
