#include "ui/IndicatorButton.h"
#include "ui/RoundButton.h"

#include <QCoreApplication>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

using namespace radio::ui;

class IndicatorButtonTest : public QObject {
    Q_OBJECT

private slots:
    void constructedStateIsUncheckedAndCheckable();
    void setCheckedTogglesStateAndEmitsSignal();
    void findChildByObjectNameResolvesAsQPushButton();
    void roundButtonIsNotCheckableByDefault();
};

void IndicatorButtonTest::constructedStateIsUncheckedAndCheckable()
{
    IndicatorButton button;
    QVERIFY(button.isCheckable());
    QVERIFY(!button.isChecked());
}

void IndicatorButtonTest::setCheckedTogglesStateAndEmitsSignal()
{
    IndicatorButton button;
    QSignalSpy toggledSpy(&button, &QAbstractButton::toggled);

    button.setChecked(true);
    QVERIFY(button.isChecked());
    QCOMPARE(toggledSpy.count(), 1);
    QCOMPARE(toggledSpy.first().first().toBool(), true);

    button.setChecked(false);
    QVERIFY(!button.isChecked());
    QCOMPARE(toggledSpy.count(), 2);
    QCOMPARE(toggledSpy.last().first().toBool(), false);
}

void IndicatorButtonTest::findChildByObjectNameResolvesAsQPushButton()
{
    // The exact concern that would silently break MixerPanelWidgetTest.cpp's
    // persistence assertions if IndicatorButton were ever a composite
    // QWidget wrapping a separate internal QPushButton instead of a genuine
    // subclass -- findChild<QPushButton*> must still resolve it.
    QWidget parent;
    auto* button = new IndicatorButton(QString(), &parent);
    button->setObjectName(QStringLiteral("testIndicatorButton"));
    button->setCheckable(true);

    auto* found = parent.findChild<QPushButton*>(QStringLiteral("testIndicatorButton"));
    QVERIFY(found != nullptr);
    found->setChecked(true);
    QVERIFY(button->isChecked());
}

void IndicatorButtonTest::roundButtonIsNotCheckableByDefault()
{
    // RoundButton itself (used directly for momentary actions like Mixer's
    // "Flat") must NOT be checkable -- only IndicatorButton opts into that.
    RoundButton button;
    QVERIFY(!button.isCheckable());
}

QTEST_MAIN(IndicatorButtonTest)
#include "IndicatorButtonTest.moc"
