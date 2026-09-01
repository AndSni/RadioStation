#include "ui/CartButton.h"
#include "ui/CartGridWidget.h"

#include "audio/AudioEngine.h"
#include "db/CartRepository.h"
#include "db/Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;
using namespace radio::db;

namespace {
CartButton* findButtonForClip(CartGridWidget& widget, qint64 clipId)
{
    for (auto* button : widget.findChildren<CartButton*>()) {
        if (button->clip() && button->clip()->id == clipId)
            return button;
    }
    return nullptr;
}
}

class CartGridWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void clickingCartButtonBlinksUntilCartFinishes();
    void externalTriggerBlinksTheMatchingButton();
};

void CartGridWidgetTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationCartGridWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationCartGridWidgetTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void CartGridWidgetTest::init()
{
    QSqlQuery(Database::handle()).exec(QStringLiteral("DELETE FROM cart_clips"));
}

void CartGridWidgetTest::clickingCartButtonBlinksUntilCartFinishes()
{
    const qint64 clipId = CartRepository::addClip(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"),
        QStringLiteral("Beep"), QString(), 0, 0, QString(), 0);
    QVERIFY(clipId >= 0);

    AudioEngine engine;
    engine.start();
    CartGridWidget widget(&engine);

    CartButton* button = findButtonForClip(widget, clipId);
    QVERIFY(button != nullptr);
    QVERIFY(!button->isPlaying());

    button->click();
    QTRY_VERIFY_WITH_TIMEOUT(button->isPlaying(), 1000);

    // short_beep.wav is ~0.9s - blinking should stop on its own once the
    // poll timer notices the triggered instance's token is no longer active.
    QTRY_VERIFY_WITH_TIMEOUT(!button->isPlaying(), 3000);

    engine.shutdown();
}

void CartGridWidgetTest::externalTriggerBlinksTheMatchingButton()
{
    // Simulates CartAutomationEngine::cartTriggered() — a schedule-driven
    // fire that never goes through this button's own click handler at all.
    const qint64 clipId = CartRepository::addClip(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"),
        QStringLiteral("Beep"), QString(), 0, 0, QString(), 0);
    QVERIFY(clipId >= 0);

    AudioEngine engine;
    engine.start();
    CartGridWidget widget(&engine);

    CartButton* button = findButtonForClip(widget, clipId);
    QVERIFY(button != nullptr);

    const QString token = engine.triggerCart(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    QVERIFY(!token.isEmpty());
    widget.onExternalCartTriggered(clipId, token);

    QVERIFY(button->isPlaying());
    QTRY_VERIFY_WITH_TIMEOUT(!button->isPlaying(), 3000);

    engine.shutdown();
}

QTEST_MAIN(CartGridWidgetTest)
#include "CartGridWidgetTest.moc"
