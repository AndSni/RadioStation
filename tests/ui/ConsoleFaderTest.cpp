#include "ui/ConsoleFader.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QSignalSpy>
#include <QTest>

using namespace radio::ui;

class ConsoleFaderTest : public QObject {
    Q_OBJECT

private slots:
    void behavesAsAQSliderForValueAndSignals();
    void setCenterDetentIsANoOpRepaintToggle();
    void capMovesMonotonicallyWithValue();
    void capRespectsInvertedAppearance();
    void grooveClickLandsNearTheExpectedFraction();
    void rendersAcrossSizesOrientationsAndStates();
};

void ConsoleFaderTest::behavesAsAQSliderForValueAndSignals()
{
    ConsoleFader fader(Qt::Vertical);
    fader.setRange(-12, 12);
    fader.setValue(0);

    // It IS a QSlider -- the same lookup MixerPanelWidgetTest relies on.
    QVERIFY(qobject_cast<QSlider*>(&fader) != nullptr);

    QSignalSpy spy(&fader, &QSlider::valueChanged);
    fader.setValue(6);
    QCOMPARE(fader.value(), 6);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 6);

    fader.setValue(6); // unchanged -- QSlider already suppresses this
    QCOMPARE(spy.count(), 1);

    fader.setValue(999); // clamps to the range
    QCOMPARE(fader.value(), 12);
}

void ConsoleFaderTest::setCenterDetentIsANoOpRepaintToggle()
{
    ConsoleFader fader(Qt::Vertical);
    QCOMPARE(fader.centerDetent(), false);
    fader.setCenterDetent(true);
    QCOMPARE(fader.centerDetent(), true);
    fader.setCenterDetent(true); // idempotent
    QCOMPARE(fader.centerDetent(), true);
}

void ConsoleFaderTest::capMovesMonotonicallyWithValue()
{
    ConsoleFader fader(Qt::Vertical);
    fader.setRange(0, 100);
    fader.resize(36, 200);

    fader.setValue(0);
    const int yLow = fader.capRect().center().y();
    fader.setValue(50);
    const int yMid = fader.capRect().center().y();
    fader.setValue(100);
    const int yHigh = fader.capRect().center().y();

    // Higher value == higher up the fader == smaller y.
    QVERIFY(yLow > yMid);
    QVERIFY(yMid > yHigh);

    // The cap stays inside the widget at both extremes.
    fader.setValue(0);
    QVERIFY(fader.rect().contains(fader.capRect().center()));
    fader.setValue(100);
    QVERIFY(fader.rect().contains(fader.capRect().center()));
}

void ConsoleFaderTest::capRespectsInvertedAppearance()
{
    ConsoleFader fader(Qt::Vertical);
    fader.setRange(0, 100);
    fader.resize(36, 200);

    fader.setValue(100);
    const int normalTop = fader.capRect().center().y();
    fader.setInvertedAppearance(true);
    const int invertedTop = fader.capRect().center().y();

    // Inverting flips which end "max" sits at.
    QVERIFY(invertedTop > normalTop);
}

void ConsoleFaderTest::grooveClickLandsNearTheExpectedFraction()
{
    ConsoleFader fader(Qt::Vertical);
    fader.setRange(0, 100);
    fader.resize(36, 200);
    fader.show();

    // Click the vertical centre of the groove -> ~50%.
    QTest::mouseClick(&fader, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(18, 100));
    QCoreApplication::processEvents();
    QVERIFY2(qAbs(fader.value() - 50) <= 5, qPrintable(QStringLiteral("value=%1").arg(fader.value())));

    // Click near the top -> near the maximum.
    QTest::mouseClick(&fader, Qt::LeftButton, Qt::KeyboardModifiers(), QPoint(18, 12));
    QCoreApplication::processEvents();
    QVERIFY2(fader.value() >= 90, qPrintable(QStringLiteral("value=%1").arg(fader.value())));
}

void ConsoleFaderTest::rendersAcrossSizesOrientationsAndStates()
{
    for (auto orientation : { Qt::Vertical, Qt::Horizontal }) {
        for (bool detent : { false, true }) {
            ConsoleFader fader(orientation);
            fader.setRange(-12, 12);
            fader.setTickInterval(2);
            fader.setCenterDetent(detent);

            for (const QSize size : { QSize(36, 60), QSize(36, 240), QSize(240, 34), QSize(20, 20) }) {
                fader.resize(size);
                for (int value : { -12, -3, 0, 7, 12 }) {
                    fader.setValue(value);
                    QPixmap pixmap(fader.size());
                    pixmap.fill(Qt::transparent);
                    fader.render(&pixmap); // must not crash / assert
                    QVERIFY(!pixmap.isNull());
                }
            }

            fader.setEnabled(false); // disabled paints at reduced opacity
            QPixmap disabled(fader.size());
            disabled.fill(Qt::transparent);
            fader.render(&disabled);
            QVERIFY(!disabled.isNull());
        }
    }
}

QTEST_MAIN(ConsoleFaderTest)
#include "ConsoleFaderTest.moc"
