#include "ui/RotaryKnob.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>

using namespace radio::ui;

class RotaryKnobTest : public QObject {
    Q_OBJECT

private slots:
    void constructedValueClampsToRange();
    void setValueClampsAndEmitsOnRealChange();
    void setValueIsNoOpWhenUnchanged();
};

void RotaryKnobTest::constructedValueClampsToRange()
{
    RotaryKnob knob(0, 150, 999); // default outside the range
    QCOMPARE(knob.value(), 150);

    RotaryKnob knob2(0, 150, -5);
    QCOMPARE(knob2.value(), 0);

    RotaryKnob knob3(0, 150, 100);
    QCOMPARE(knob3.value(), 100);
}

void RotaryKnobTest::setValueClampsAndEmitsOnRealChange()
{
    RotaryKnob knob(0, 150, 100);
    QSignalSpy spy(&knob, &RotaryKnob::valueChanged);

    knob.setValue(75);
    QCOMPARE(knob.value(), 75);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().first().toInt(), 75);

    knob.setValue(999); // clamps to the max
    QCOMPARE(knob.value(), 150);
    QCOMPARE(spy.count(), 2);
}

void RotaryKnobTest::setValueIsNoOpWhenUnchanged()
{
    RotaryKnob knob(0, 150, 100);
    QSignalSpy spy(&knob, &RotaryKnob::valueChanged);

    knob.setValue(100); // already the value
    QCOMPARE(spy.count(), 0);
}

QTEST_MAIN(RotaryKnobTest)
#include "RotaryKnobTest.moc"
