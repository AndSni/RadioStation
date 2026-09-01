#include "ui/LcdReadout.h"

#include <QCoreApplication>
#include <QTest>

using namespace radio::ui;

class LcdReadoutTest : public QObject {
    Q_OBJECT

private slots:
    void constructedStateIsEmpty();
    void setValueRoundTrips();
    void setCaptionRoundTrips();
};

void LcdReadoutTest::constructedStateIsEmpty()
{
    LcdReadout readout;
    QVERIFY(readout.value().isEmpty());
    QVERIFY(readout.caption().isEmpty());
}

void LcdReadoutTest::setValueRoundTrips()
{
    LcdReadout readout;
    readout.setValue(QStringLiteral("+6 dB"));
    QCOMPARE(readout.value(), QStringLiteral("+6 dB"));

    readout.setValue(QStringLiteral("-3 dB"));
    QCOMPARE(readout.value(), QStringLiteral("-3 dB"));
}

void LcdReadoutTest::setCaptionRoundTrips()
{
    LcdReadout readout;
    readout.setCaption(QStringLiteral("60Hz"));
    QCOMPARE(readout.caption(), QStringLiteral("60Hz"));
}

QTEST_MAIN(LcdReadoutTest)
#include "LcdReadoutTest.moc"
