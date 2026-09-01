#include "ui/DebugConsoleDockWidget.h"
#include "ui/MainWindow.h"

#include "db/Database.h"

#include <QAction>
#include <QCoreApplication>
#include <QPushButton>
#include <QSettings>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;

class MainWindowTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void debugConsoleTogglePersistsAcrossRestart();
    void debugConsoleDefaultsVisible();
    // The master Auto DJ on/off toggle lives ONLY on this always-visible
    // toolbar now (moved from the, hideable, Auto DJ dock -- see
    // AutoDjPanelWidget's own doc comment) -- these are its regression
    // tests, ported from the removed AutoDjPanelWidgetTest cases of the
    // same shape.
    void autoDjToggleReflectsAutoRunningByDefault();
    void autoDjToggleTogglesOnClickAndStaysFixedSize();
};

void MainWindowTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationUiTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationUiTest"));
    gst_init(nullptr, nullptr);
    QVERIFY(radio::db::Database::open());
}

void MainWindowTest::debugConsoleDefaultsVisible()
{
    QSettings().clear();

    MainWindow window;
    window.show();
    auto* dock = window.findChild<DebugConsoleDockWidget*>();
    QVERIFY(dock != nullptr);
    QVERIFY(dock->isVisible());
}

void MainWindowTest::debugConsoleTogglePersistsAcrossRestart()
{
    QSettings().clear();

    {
        MainWindow window;
        window.show();
        auto* dock = window.findChild<DebugConsoleDockWidget*>();
        QVERIFY(dock != nullptr);
        QVERIFY(dock->isVisible());

        dock->toggleViewAction()->trigger(); // hide it
        QVERIFY(!dock->isVisible());

        window.close(); // synchronously invokes closeEvent -> persists settings
    }

    QSettings settings;
    QVERIFY(settings.contains(QStringLiteral("ui/debugConsoleVisible")));
    QCOMPARE(settings.value(QStringLiteral("ui/debugConsoleVisible")).toBool(), false);

    {
        MainWindow window2;
        window2.show();
        auto* dock2 = window2.findChild<DebugConsoleDockWidget*>();
        QVERIFY(dock2 != nullptr);
        QVERIFY(!dock2->isVisible());
    }

    QSettings().clear();
}

void MainWindowTest::autoDjToggleReflectsAutoRunningByDefault()
{
    MainWindow window;
    window.show();

    auto* toggleButton = window.findChild<QPushButton*>(QStringLiteral("autoDjToggleButton"));
    QVERIFY(toggleButton != nullptr);
    QVERIFY(toggleButton->isChecked()); // neither deck manually overridden yet - Auto DJ is "running" by default
    QCOMPARE(toggleButton->text(), QStringLiteral("Auto DJ: ON"));
}

void MainWindowTest::autoDjToggleTogglesOnClickAndStaysFixedSize()
{
    MainWindow window;
    window.show();

    auto* toggleButton = window.findChild<QPushButton*>(QStringLiteral("autoDjToggleButton"));
    QVERIFY(toggleButton != nullptr);
    const QSize sizeWhileOn = toggleButton->size();

    QTest::mouseClick(toggleButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(!toggleButton->isChecked(), 3000);
    QCOMPARE(toggleButton->text(), QStringLiteral("Auto DJ: OFF"));
    QCOMPARE(toggleButton->size(), sizeWhileOn); // the "OFF" text is the longer of the two -- must not resize the button

    QTest::mouseClick(toggleButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(toggleButton->isChecked(), 3000);
    QCOMPARE(toggleButton->text(), QStringLiteral("Auto DJ: ON"));
    QCOMPARE(toggleButton->size(), sizeWhileOn);
}

QTEST_MAIN(MainWindowTest)
#include "MainWindowTest.moc"
