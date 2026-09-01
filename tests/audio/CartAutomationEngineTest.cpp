#include "audio/AudioEngine.h"
#include "audio/CartAutomationEngine.h"
#include "audio/CrossfadeController.h"

#include "db/CartRepository.h"
#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "scheduler/ClockEngine.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::audio;
using namespace radio::db;
using radio::scheduler::ClockEngine;

class CartAutomationEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void firesCartWhenBlockFrequencyThresholdReached();
    void doesNotFireBeforeThresholdReached();
    void doesNotFireWhenNoBlockActive();
    void doesNotFireWhenCartFrequencyIsZero();
    void overlayModeSkipsInBetweenOnlyClipsButStillFiresNormalOnes();
    void insertModeTriggersCartOnSuppressedHandoffThenStartsIdleDeck();
    void insertModeCutsAwayImmediatelyWhenNoClipAvailable();
    void insertModeDoesNotForceStartIdleDeckIfItWentManualWhileCartPlayed();
    void overlayFireEmitsCartTriggeredWithClipIdAndToken();
    void insertFireEmitsCartTriggeredWithClipIdAndToken();

    void clockDrivenBlockIgnoresCartFrequencyCounter();
    void clockCartElementFiresOnSuppressedHandoff();
    void clockCartSpecificFiresTheNamedClip();

private:
    static qint64 createAlwaysOnBlock(int cartFrequency, const QString& cartMode = QStringLiteral("random"),
        const QString& cartPlaybackType = QStringLiteral("overlay"));
    static qint64 createAlwaysOnClockBlock(qint64 clockId, int legacyCartFrequency = 1);
};

void CartAutomationEngineTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationCartAutomationEngineTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationCartAutomationEngineTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void CartAutomationEngineTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM cart_clips"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
}

qint64 CartAutomationEngineTest::createAlwaysOnBlock(
    int cartFrequency, const QString& cartMode, const QString& cartPlaybackType)
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Cart Test Playlist"));

    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On");
    block.daysMask = 0b1111111; // every day
    block.startMinute = 0;
    block.endMinute = 1440; // All Day
    block.playlistId = playlistId;
    block.cartFrequency = cartFrequency;
    block.cartMode = cartMode;
    block.cartOffsetSeconds = 0;
    block.cartPlaybackType = cartPlaybackType;
    return ScheduleBlockRepository::createBlock(block);
}

qint64 CartAutomationEngineTest::createAlwaysOnClockBlock(qint64 clockId, int legacyCartFrequency)
{
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Always On (Clock)");
    block.daysMask = 0b1111111; // every day
    block.startMinute = 0;
    block.endMinute = 1440; // All Day
    block.clockId = clockId;
    // Deliberately nonzero/legacy-shaped -- proves the clock branch takes
    // precedence and these columns are simply ignored for a clock block.
    block.cartFrequency = legacyCartFrequency;
    block.cartPlaybackType = QStringLiteral("overlay");
    return ScheduleBlockRepository::createBlock(block);
}

void CartAutomationEngineTest::firesCartWhenBlockFrequencyThresholdReached()
{
    createAlwaysOnBlock(1);
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    QCOMPARE(engine.activeCartCount(), size_t(0));
    // Calling the signal function directly (it's declared under a plain
    // `signals:` section, so it's a normal public member function) fires it
    // exactly as a real crossfade completion would, without needing to
    // orchestrate an actual multi-second crossfade sequence.
    controller.autoCrossfadeFinished(QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);

    engine.shutdown();
}

void CartAutomationEngineTest::doesNotFireBeforeThresholdReached()
{
    createAlwaysOnBlock(3);
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    controller.autoCrossfadeFinished(QStringLiteral("A")); // 1/3
    controller.autoCrossfadeFinished(QStringLiteral("B")); // 2/3
    QTest::qWait(300);
    QCOMPARE(engine.activeCartCount(), size_t(0));

    controller.autoCrossfadeFinished(QStringLiteral("A")); // 3/3 - fires
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);

    engine.shutdown();
}

void CartAutomationEngineTest::doesNotFireWhenNoBlockActive()
{
    // No schedule blocks exist at all.
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    controller.autoCrossfadeFinished(QStringLiteral("A"));
    QTest::qWait(300);
    QCOMPARE(engine.activeCartCount(), size_t(0));

    engine.shutdown();
}

void CartAutomationEngineTest::doesNotFireWhenCartFrequencyIsZero()
{
    createAlwaysOnBlock(0); // 0 = disabled
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    controller.autoCrossfadeFinished(QStringLiteral("A"));
    QTest::qWait(300);
    QCOMPARE(engine.activeCartCount(), size_t(0));

    engine.shutdown();
}

void CartAutomationEngineTest::overlayModeSkipsInBetweenOnlyClipsButStillFiresNormalOnes()
{
    createAlwaysOnBlock(1); // overlay mode, frequency 1
    // Only clip available is flagged in-between-only: an overlay cart must
    // NOT pick it (it may carry its own musical bed, would clash with the
    // song still going out).
    CartRepository::addClip(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Bed Jingle"),
        QString(), 0, 0, QString(), 900, /*inBetweenOnly=*/true);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    controller.autoCrossfadeFinished(QStringLiteral("A"));
    QTest::qWait(300);
    QCOMPARE(engine.activeCartCount(), size_t(0));

    // Add a normal (overlay-eligible) clip — the next transition fires it.
    CartRepository::addClip(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Sweeper"),
        QString(), 0, 1, QString(), 900);
    controller.autoCrossfadeFinished(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);

    engine.shutdown();
}

void CartAutomationEngineTest::insertModeTriggersCartOnSuppressedHandoffThenStartsIdleDeck()
{
    createAlwaysOnBlock(1, QStringLiteral("random"), QStringLiteral("insert"));
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    // Isolate from the real watch-tick/EOS path — this test drives the
    // suppressed hand-off directly rather than waiting out A's real ~4.6s
    // duration (see autoCrossfadeSuppressed() calls below).
    controller.setAutoAdvanceEnabled(false);

    QCOMPARE(engine.activeCartCount(), size_t(0));
    // Calling the signal function directly (it's declared under a plain
    // `signals:` section, so it's a normal public member function) fires it
    // exactly as CrossfadeController::onDeckEos() would at the real
    // suppressed-hand-off moment, without needing to wait out a real track.
    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));

    // The cart fires immediately (0s offset)...
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);
    // ...B stays paused/silent the whole time the cart plays — no ramp, no early start.
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused);
    // ...and once the cart finishes, B starts at full volume and becomes active.
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Playing, 3000);
    QCOMPARE(controller.activeDeck(), QStringLiteral("B"));

    engine.shutdown();
}

void CartAutomationEngineTest::insertModeCutsAwayImmediatelyWhenNoClipAvailable()
{
    createAlwaysOnBlock(1, QStringLiteral("random"), QStringLiteral("insert"));
    // Deliberately no cart clips added — nothing for CartPicker to choose.

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    controller.setAutoAdvanceEnabled(false);

    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));

    // No clip to play — must still complete the hand-off rather than
    // leaving the station stuck with B sitting silent forever (never
    // leave dead air, matching this codebase's established posture).
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Playing, 3000);
    QCOMPARE(controller.activeDeck(), QStringLiteral("B"));
    QCOMPARE(engine.activeCartCount(), size_t(0));

    engine.shutdown();
}

void CartAutomationEngineTest::insertModeDoesNotForceStartIdleDeckIfItWentManualWhileCartPlayed()
{
    createAlwaysOnBlock(1, QStringLiteral("random"), QStringLiteral("insert"));
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    controller.setAutoAdvanceEnabled(false);

    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);

    // Human takes over B WHILE the cart is playing.
    controller.notifyManualAction(QStringLiteral("B"));

    // Cart finishes shortly after — automation must NOT force B to start
    // over the human's own action.
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    QTest::qWait(300); // give the poll timer a chance to act if it were going to
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused);
    QCOMPARE(controller.activeDeck(), QStringLiteral("A")); // bookkeeping never swapped

    engine.shutdown();
}

void CartAutomationEngineTest::overlayFireEmitsCartTriggeredWithClipIdAndToken()
{
    createAlwaysOnBlock(1);
    const qint64 clipId = CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    QSignalSpy triggeredSpy(&automation, &CartAutomationEngine::cartTriggered);
    controller.autoCrossfadeFinished(QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(triggeredSpy.count() == 1, 3000);
    QCOMPARE(triggeredSpy.first().at(0).toLongLong(), clipId);
    QVERIFY(!triggeredSpy.first().at(1).toString().isEmpty());

    engine.shutdown();
}

void CartAutomationEngineTest::insertFireEmitsCartTriggeredWithClipIdAndToken()
{
    createAlwaysOnBlock(1, QStringLiteral("random"), QStringLiteral("insert"));
    const qint64 clipId = CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    controller.setAutoAdvanceEnabled(false);

    QSignalSpy triggeredSpy(&automation, &CartAutomationEngine::cartTriggered);
    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));

    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(triggeredSpy.first().at(0).toLongLong(), clipId);
    QVERIFY(!triggeredSpy.first().at(1).toString().isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    engine.shutdown();
}

void CartAutomationEngineTest::clockDrivenBlockIgnoresCartFrequencyCounter()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    createAlwaysOnClockBlock(clockId, /*legacyCartFrequency=*/1);
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);
    // Deliberately NOT calling setClockEngine() -- proves the guard in
    // onAutoCrossfadeFinished() keys off block.clockId alone, not on whether
    // a ClockEngine happens to be attached.

    controller.autoCrossfadeFinished(QStringLiteral("A")); // would hit legacy threshold (1) if not guarded
    controller.autoCrossfadeFinished(QStringLiteral("B"));
    controller.autoCrossfadeFinished(QStringLiteral("A"));
    QTest::qWait(300);
    QCOMPARE(engine.activeCartCount(), size_t(0));

    engine.shutdown();
}

void CartAutomationEngineTest::clockCartElementFiresOnSuppressedHandoff()
{
    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("cart_random");
    element.cartMode = QStringLiteral("random");
    ClockRepository::createElement(element);

    const qint64 blockId = createAlwaysOnClockBlock(clockId, /*legacyCartFrequency=*/0);
    const auto block = ScheduleBlockRepository::allBlocks().first();
    QCOMPARE(block.id, blockId);

    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Beep"), QString(), 0, 0, QString(), 900);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);
    ClockEngine clockEngine;
    automation.setClockEngine(&clockEngine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    controller.setAutoAdvanceEnabled(false);

    QCOMPARE(engine.activeCartCount(), size_t(0));
    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused);
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Playing, 3000);
    QCOMPARE(controller.activeDeck(), QStringLiteral("B"));

    // The wheel must have advanced past the (only) cart element.
    QCOMPARE(ClockRepository::wheelStateFor(blockId).position, 1);

    engine.shutdown();
}

void CartAutomationEngineTest::clockCartSpecificFiresTheNamedClip()
{
    const qint64 wantedClipId = CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Wanted"), QString(), 0, 0, QString(), 900);
    CartRepository::addClip(
        QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"), QStringLiteral("Decoy"), QString(), 0, 1, QString(), 900);

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("cart_specific");
    element.cartClipId = wantedClipId;
    ClockRepository::createElement(element);

    createAlwaysOnClockBlock(clockId, /*legacyCartFrequency=*/0);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    CartAutomationEngine automation(&engine, &controller);
    ClockEngine clockEngine;
    automation.setClockEngine(&clockEngine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);
    controller.setAutoAdvanceEnabled(false);

    QSignalSpy triggeredSpy(&automation, &CartAutomationEngine::cartTriggered);
    controller.autoCrossfadeSuppressed(QStringLiteral("A"), QStringLiteral("B"));

    QCOMPARE(triggeredSpy.count(), 1);
    QCOMPARE(triggeredSpy.first().at(0).toLongLong(), wantedClipId);

    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    engine.shutdown();
}

QTEST_MAIN(CartAutomationEngineTest)
#include "CartAutomationEngineTest.moc"
