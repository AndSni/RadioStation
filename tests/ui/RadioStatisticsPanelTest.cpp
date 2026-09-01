#include "ui/RadioStatisticsPanel.h"
#include "ui/OnAirLabel.h"

#include "audio/AudioEngine.h"
#include "db/Database.h"
#include "db/ScheduleBlockRepository.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QFontDatabase>
#include <QLabel>
#include <QSettings>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include <algorithm>
#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;
using namespace radio::db;

namespace {
bool anyLabelHasText(QWidget* parent, const QString& text)
{
    const auto labels = parent->findChildren<QLabel*>();
    return std::any_of(labels.begin(), labels.end(), [&text](QLabel* label) { return label->text() == text; });
}

bool anyLabelEndsWith(QWidget* parent, const QString& suffix)
{
    const auto labels = parent->findChildren<QLabel*>();
    return std::any_of(labels.begin(), labels.end(), [&suffix](QLabel* label) { return label->text().endsWith(suffix); });
}
}

class RadioStatisticsPanelTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void showsNoActiveBlockWhenNoneMatch();
    void showsBlockNameAndRemainingCountdown();
    void onAirReflectsRealDeckPlaybackState();
    void radioNameReflectsSettingsOnNextTick();
    void clockUsesTheEmbeddedLcdFont();
};

void RadioStatisticsPanelTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    // Regression guard: resources.qrc is compiled into rs_ui, a STATIC
    // library — without this call, nothing forces the linker to keep the
    // generated resource-registration object file, so every ":/..." path
    // (the clock's embedded LCD font here) silently resolves to nothing at
    // runtime. See main.cpp's own Q_INIT_RESOURCE call for the same reason.
    Q_INIT_RESOURCE(resources);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationRadioStatisticsPanelTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationRadioStatisticsPanelTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void RadioStatisticsPanelTest::init()
{
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSettings().clear();
}

void RadioStatisticsPanelTest::showsNoActiveBlockWhenNoneMatch()
{
    AudioEngine engine;
    engine.start();

    RadioStatisticsPanel panel(&engine);
    panel.show();

    QVERIFY(anyLabelHasText(&panel, QStringLiteral("No active block")));

    engine.shutdown();
}

void RadioStatisticsPanelTest::showsBlockNameAndRemainingCountdown()
{
    // Pinned to today's own day-bit (bit0=Mon..bit6=Sun, QDate::dayOfWeek()
    // is 1=Mon..7=Sun) and an All Day span — same determinism trick used
    // throughout this codebase's schedule-block tests so this doesn't flake
    // depending on which day it actually runs.
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Morning Show");
    block.daysMask = 1 << (QDate::currentDate().dayOfWeek() - 1);
    block.startMinute = 0;
    block.endMinute = 1440;
    ScheduleBlockRepository::createBlock(block);

    AudioEngine engine;
    engine.start();

    RadioStatisticsPanel panel(&engine);
    panel.show();

    QVERIFY(anyLabelHasText(&panel, QStringLiteral("Morning Show")));
    QVERIFY(anyLabelEndsWith(&panel, QStringLiteral(" remaining")));

    engine.shutdown();
}

void RadioStatisticsPanelTest::onAirReflectsRealDeckPlaybackState()
{
    AudioEngine engine;
    engine.start();

    RadioStatisticsPanel panel(&engine);
    panel.show();

    auto* onAirLabel = panel.findChild<OnAirLabel*>();
    QVERIFY(onAirLabel != nullptr);
    QVERIFY(!onAirLabel->isOnAir());

    const QString fixture = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), fixture);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    // Panel's own 1s poll timer picks this up; onTick() is private, so this
    // waits on the real timer rather than calling it directly.
    QTRY_VERIFY_WITH_TIMEOUT(onAirLabel->isOnAir(), 3000);

    engine.pause(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) != DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!onAirLabel->isOnAir(), 3000);

    engine.shutdown();
}

void RadioStatisticsPanelTest::radioNameReflectsSettingsOnNextTick()
{
    AudioEngine engine;
    engine.start();

    RadioStatisticsPanel panel(&engine);
    panel.show();

    QSettings settings;
    settings.setValue(QStringLiteral("station/radioName"), QStringLiteral("KKRS"));

    QTRY_VERIFY_WITH_TIMEOUT(anyLabelHasText(&panel, QStringLiteral("KKRS")), 3000);

    engine.shutdown();
}

void RadioStatisticsPanelTest::clockUsesTheEmbeddedLcdFont()
{
    AudioEngine engine;
    engine.start();

    RadioStatisticsPanel panel(&engine);
    panel.show();

    auto* clockLabel = panel.findChild<QLabel*>(QStringLiteral("radioStatisticsClockLabel"));
    QVERIFY(clockLabel != nullptr);
    QCOMPARE(clockLabel->font().family(), QStringLiteral("LCD AT&T Phone Time/Date"));

    engine.shutdown();
}

QTEST_MAIN(RadioStatisticsPanelTest)
#include "RadioStatisticsPanelTest.moc"
