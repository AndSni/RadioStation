#include "ui/AudioPillPainter.h"

#include <QTest>

using namespace radio::ui;

class AudioPillPainterTest : public QObject {
    Q_OBJECT

private slots:
    void contrastingTextColorIsReadableAgainstLightAndDarkBackgrounds();
    void neutralColorIsAValidOpaqueColor();
    void blendMixesByTheGivenRatio();
    void blendClampsRatioToValidRange();
};

void AudioPillPainterTest::contrastingTextColorIsReadableAgainstLightAndDarkBackgrounds()
{
    QCOMPARE(AudioPillPainter::contrastingTextColor(Qt::white), QColor(Qt::black));
    QCOMPARE(AudioPillPainter::contrastingTextColor(Qt::black), QColor(Qt::white));
    QCOMPARE(AudioPillPainter::contrastingTextColor(QColor(250, 250, 240)), QColor(Qt::black));
    QCOMPARE(AudioPillPainter::contrastingTextColor(QColor(10, 10, 15)), QColor(Qt::white));
}

void AudioPillPainterTest::neutralColorIsAValidOpaqueColor()
{
    QVERIFY(AudioPillPainter::kNeutralColor.isValid());
    QCOMPARE(AudioPillPainter::kNeutralColor.alpha(), 255);
}

void AudioPillPainterTest::blendMixesByTheGivenRatio()
{
    const QColor base(100, 100, 100);
    const QColor tint(200, 0, 50);

    // DeckWidget's actual use: 20% tint.
    const QColor result = AudioPillPainter::blend(base, tint, 0.2);
    QCOMPARE(result.red(), 120); // 100*0.8 + 200*0.2
    QCOMPARE(result.green(), 80); // 100*0.8 +   0*0.2
    QCOMPARE(result.blue(), 90); // 100*0.8 +  50*0.2

    QCOMPARE(AudioPillPainter::blend(base, tint, 0.0), base);
    QCOMPARE(AudioPillPainter::blend(base, tint, 1.0), tint);
}

void AudioPillPainterTest::blendClampsRatioToValidRange()
{
    const QColor base(100, 100, 100);
    const QColor tint(200, 0, 50);

    QCOMPARE(AudioPillPainter::blend(base, tint, -5.0), base);
    QCOMPARE(AudioPillPainter::blend(base, tint, 5.0), tint);
}

QTEST_MAIN(AudioPillPainterTest)
#include "AudioPillPainterTest.moc"
