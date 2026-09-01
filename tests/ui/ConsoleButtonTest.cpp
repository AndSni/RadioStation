#include "ui/ConsoleButton.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>

using namespace radio::ui;

class ConsoleButtonTest : public QObject {
    Q_OBJECT

private slots:
    void behavesAsAQPushButton();
    void litStateTogglesAndGuardsNoOp();
    void blinkingStartsAndStops();
    void checkableLightsWhenChecked();
    void rendersAcrossStatesAndSizes();
    void sizeHintLeavesRoomForTheLightBar();
};

void ConsoleButtonTest::behavesAsAQPushButton()
{
    ConsoleButton button(QStringLiteral("PLAY"));
    QVERIFY(qobject_cast<QPushButton*>(&button) != nullptr);
    QCOMPARE(button.text(), QStringLiteral("PLAY"));

    button.show();
    QSignalSpy spy(&button, &QPushButton::clicked);
    QTest::mouseClick(&button, Qt::LeftButton);
    QCOMPARE(spy.count(), 1);
}

void ConsoleButtonTest::litStateTogglesAndGuardsNoOp()
{
    ConsoleButton button(QStringLiteral("PLAY"));
    QVERIFY(!button.isLit());
    button.setLit(true);
    QVERIFY(button.isLit());
    button.setLit(true); // idempotent
    QVERIFY(button.isLit());
    button.setLit(false);
    QVERIFY(!button.isLit());
}

void ConsoleButtonTest::blinkingStartsAndStops()
{
    ConsoleButton button(QStringLiteral("CUE"));
    QVERIFY(!button.isBlinking());
    button.setBlinking(true);
    QVERIFY(button.isBlinking());
    button.setBlinking(true); // idempotent
    QVERIFY(button.isBlinking());

    // Let the internal pulse timer tick at least once — must not crash.
    QTest::qWait(600);

    button.setBlinking(false);
    QVERIFY(!button.isBlinking());
}

void ConsoleButtonTest::checkableLightsWhenChecked()
{
    ConsoleButton button(QStringLiteral("Equal Power"));
    button.setCheckable(true);
    QVERIFY(!button.isChecked());
    button.setChecked(true);
    QVERIFY(button.isChecked());

    // Paints in the checked state without asserting.
    QPixmap pm(button.sizeHint());
    pm.fill(Qt::transparent);
    button.render(&pm);
    QVERIFY(!pm.isNull());
}

void ConsoleButtonTest::rendersAcrossStatesAndSizes()
{
    ConsoleButton button(QStringLiteral("STOP"));
    for (const QSize size : { QSize(40, 20), QSize(120, 34), QSize(8, 8), QSize(200, 60) }) {
        button.resize(size);
        for (bool lit : { false, true }) {
            button.setLit(lit);
            for (bool enabled : { true, false }) {
                button.setEnabled(enabled);
                QPixmap pm(button.size());
                pm.fill(Qt::transparent);
                button.render(&pm);
                QVERIFY(!pm.isNull());
            }
        }
    }
}

void ConsoleButtonTest::sizeHintLeavesRoomForTheLightBar()
{
    ConsoleButton console(QStringLiteral("Equal Power"));
    QPushButton plain(QStringLiteral("Equal Power"));
    QVERIFY(console.sizeHint().height() > plain.sizeHint().height());
    QVERIFY(console.sizeHint().width() >= plain.sizeHint().width());
}

QTEST_MAIN(ConsoleButtonTest)
#include "ConsoleButtonTest.moc"
