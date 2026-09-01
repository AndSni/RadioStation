#include "audio/AudioEngine.h"
#include "audio/CrossfadeController.h"

#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

using namespace radio::audio;
using namespace radio::db;

class CrossfadeControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void autoCrossfadeSwapsActiveDeck();
    void manualPositionAppliesEqualPowerCurve();
    void queueBootstrapNeverPlaysBothDecksAtOnce();
    void manualOverrideBlocksAutoLoadOnIdleDeck();
    void manualOverrideBlocksAutoCrossfade();
    void manualOverrideIsPerDeckNotGlobal();
    void manualOverrideSignalOnlyFiresOnTransition();
    void handsOffResumedFiresOnlyWhenBothDecksBecomeAuto();
    void manualOverrideAutoClearsOnEos();
    void concurrentManualAndWatchTickActionsOnSameDeck();
    void autoCrossfadeSuppressorBlocksRampedCrossfade();
    void suppressedActiveDeckReachingEosEmitsAutoCrossfadeSuppressed();
    void autoCrossfadeSuppressedNotEmittedWhenIdleDeckNotCued();
    void autoCrossfadeSuppressedNotEmittedWhenActiveDeckManual();
    void completeSuppressedCrossfadeStartsIdleDeckAndSwapsActive();
    void trackLoadedCarriesTheConsumedQueueEntrysId();
    void trackLoadedFiresBeforeQueueConsumed();
    void manualFadeUnavailableWhenIdleDeckNotCued();
    void manualFadeAvailableWhenBothDecksReady();
    void manualFadeUnavailableWhileAlreadyCrossfading();
    void manualFadeUnavailableWhenAutoAdvanceDisabled();
    void manualFadeUnavailableWhenEitherDeckManual();
    void requestManualFadeStartsImmediatelyRegardlessOfLeadTime();
    void requestManualFadeIsNoOpWhenUnavailable();
    void manualFadeAvailabilityChangedFiresOnlyOnTransition();
    void discardIdleCueIsNoOpWhenIdleDeckAlreadyEmpty();
    void discardIdleCueIsNoOpWhenIdleDeckManuallyOverridden();
    void discardIdleCueUnloadsAnOtherwiseCuedIdleDeck();
};

void CrossfadeControllerTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationCrossfadeTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationCrossfadeTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void CrossfadeControllerTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM rotation_categories"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
}

void CrossfadeControllerTest::autoCrossfadeSwapsActiveDeck()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    const QString trackB = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");

    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), trackB);
    engine.pause(QStringLiteral("B")); // cued, not yet audible

    QTRY_VERIFY_WITH_TIMEOUT(engine.duration(QStringLiteral("A")) > 0, 3000);

    // Force the crossfade to start on the very next watch tick regardless of
    // the fixture's actual length, and finish fast for a quick test.
    controller.setCrossfadeLeadMs(999'999'999);
    controller.setFadeDurationMs(300);

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    QSignalSpy finishedSpy(&controller, &CrossfadeController::autoCrossfadeFinished);

    QCOMPARE(controller.activeDeck(), QStringLiteral("A"));

    QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 3000);

    // The huge crossfadeLeadMs above doesn't just force ONE crossfade — it
    // makes onWatchTick()'s "duration - position <= leadMs" check true on
    // literally every tick, so left alone the controller immediately starts
    // re-crossfading back (B -> A -> B -> ...) forever. Disable auto-advance
    // while THIS crossfade is still in flight (m_crossfading is already
    // true, so this doesn't interrupt it — onCrossfadeFinished() is driven
    // by m_finalizeTimer, independent of m_autoAdvanceEnabled) rather than
    // after waiting for it to finish: disabling only after finishedSpy
    // fires leaves a real race window between "test thread observes
    // finishedSpy.count()>0" and "test thread's next line actually runs",
    // during which the watch timer (also on this thread) can already fire
    // again and re-trigger a second crossfade — confirmed empirically, this
    // wasn't just theoretical. Disabling now, before that finalize timer
    // even fires, closes the window entirely: whenever onWatchTick() next
    // runs, !m_autoAdvanceEnabled short-circuits it before the crossfade
    // re-trigger check is reached.
    controller.setAutoAdvanceEnabled(false);

    QTRY_VERIFY_WITH_TIMEOUT(finishedSpy.count() > 0, 3000);

    QCOMPARE(controller.activeDeck(), QStringLiteral("B"));
    QCOMPARE(controller.idleDeck(), QStringLiteral("A"));

    // The deck that finished fading out should have been fully unloaded
    // (Null, not just Ready) — see CrossfadeController::onCrossfadeFinished.
    // Generous timeout: this test deliberately uses an unrealistically fast
    // 300ms fade to keep the suite quick, which occasionally exposes real
    // (but harmless at normal multi-second fade durations) GStreamer async
    // teardown latency rather than any actual bug.
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Null, 10000);
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Playing);

    engine.shutdown();
}

void CrossfadeControllerTest::manualPositionAppliesEqualPowerCurve()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    controller.setManualPosition(0.0);
    QCOMPARE(engine.volume(QStringLiteral("A")), 1.0);
    QCOMPARE(engine.volume(QStringLiteral("B")), 0.0);

    controller.setManualPosition(1.0);
    QVERIFY(engine.volume(QStringLiteral("A")) < 0.01);
    QVERIFY(engine.volume(QStringLiteral("B")) > 0.99);

    controller.setManualPosition(0.5);
    const double volA = engine.volume(QStringLiteral("A"));
    const double volB = engine.volume(QStringLiteral("B"));
    QVERIFY(qAbs(volA - volB) < 0.01);
    QVERIFY(volA > 0.6 && volA < 0.8); // cos(pi/4) ~= 0.707

    engine.shutdown();
}

void CrossfadeControllerTest::queueBootstrapNeverPlaysBothDecksAtOnce()
{
    // Regression test for a real bug: a freshly-attached deck bin syncs to
    // the MASTER PIPELINE's state, not just its last fully-settled state.
    // If that sync races the pipeline's own still-in-flight transition to
    // PLAYING (real audio-device negotiation can take a fraction of a
    // second — synthetic test sinks settle too fast to ever expose this),
    // a deck that's only meant to be CUED (silent, paused) can start
    // audibly playing immediately, alongside the actual active deck. This
    // exercises the real queue-pulling bootstrap path in
    // CrossfadeController::onWatchTick/maybeLoadNextFromQueue — the earlier
    // tests in this file never did, since they preloaded decks directly.
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);

    TrackRecord trackA;
    trackA.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    trackA.title = QStringLiteral("Track A");
    trackA.categoryId = category;
    const qint64 trackIdA = TrackRepository::upsertScannedTrack(trackA);

    TrackRecord trackB;
    trackB.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    trackB.title = QStringLiteral("Track B");
    trackB.categoryId = category;
    const qint64 trackIdB = TrackRepository::upsertScannedTrack(trackB);

    QVERIFY(PlaylistRepository::appendToQueue(trackIdA, QStringLiteral("manual")));
    QVERIFY(PlaylistRepository::appendToQueue(trackIdB, QStringLiteral("manual")));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine); // watch timer starts immediately, pulls from the queue
    // tone440.wav is only ~4.6s — shorter than the default 5s crossfade
    // lead time, which would otherwise trigger a legitimate crossfade
    // (playing B on purpose) almost immediately and confound this test.
    // Keep it small enough that it can't fire within this test's window.
    controller.setCrossfadeLeadMs(200);

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) != DeckState::Null, 3000);

    // The crux of the bug: the cued deck must NOT be Playing while the
    // active deck already is.
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused);

    // Hold for a bit longer — the bug let the cued deck keep playing
    // indefinitely once it slipped through, not just for an instant.
    QTest::qWait(500);
    QCOMPARE(engine.state(QStringLiteral("A")), DeckState::Playing);
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused);

    engine.shutdown();
}

void CrossfadeControllerTest::manualOverrideBlocksAutoLoadOnIdleDeck()
{
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);

    TrackRecord trackA;
    trackA.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    trackA.title = QStringLiteral("Track A");
    trackA.categoryId = category;
    const qint64 trackIdA = TrackRepository::upsertScannedTrack(trackA);

    TrackRecord trackB;
    trackB.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    trackB.title = QStringLiteral("Track B");
    trackB.categoryId = category;
    const qint64 trackIdB = TrackRepository::upsertScannedTrack(trackB);

    QVERIFY(PlaylistRepository::appendToQueue(trackIdA, QStringLiteral("manual")));
    QVERIFY(PlaylistRepository::appendToQueue(trackIdB, QStringLiteral("manual")));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    // Claim the idle deck before the watch timer's very first tick can
    // touch it — simulates a human clicking a deck control the instant the
    // station starts up.
    controller.notifyManualAction(QStringLiteral("B"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTest::qWait(1000); // several watch-tick intervals
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Null); // still untouched by auto-fill

    controller.resumeAutoAdvance(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) != DeckState::Null, 3000);

    engine.shutdown();
}

void CrossfadeControllerTest::manualOverrideBlocksAutoCrossfade()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    const QString trackB = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");

    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), trackB);
    engine.pause(QStringLiteral("B"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.duration(QStringLiteral("A")) > 0, 3000);

    controller.setCrossfadeLeadMs(999'999'999); // eligible on the very next tick
    controller.setFadeDurationMs(300);
    controller.notifyManualAction(QStringLiteral("B")); // idle deck claimed — should block the crossfade

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);

    QTest::qWait(1000); // several ticks — would normally have started well within this window
    QCOMPARE(startedSpy.count(), 0);

    controller.resumeAutoAdvance(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(startedSpy.count() > 0, 3000);

    engine.shutdown();
}

void CrossfadeControllerTest::manualOverrideIsPerDeckNotGlobal()
{
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);

    TrackRecord trackA;
    trackA.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    trackA.title = QStringLiteral("Track A");
    trackA.categoryId = category;
    const qint64 trackIdA = TrackRepository::upsertScannedTrack(trackA);

    TrackRecord trackB;
    trackB.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    trackB.title = QStringLiteral("Track B");
    trackB.categoryId = category;
    const qint64 trackIdB = TrackRepository::upsertScannedTrack(trackB);

    QVERIFY(PlaylistRepository::appendToQueue(trackIdA, QStringLiteral("manual")));
    QVERIFY(PlaylistRepository::appendToQueue(trackIdB, QStringLiteral("manual")));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    // Override the (default) ACTIVE deck only — the idle deck must still
    // bootstrap normally, proving the flag is scoped per-deck, not global.
    controller.notifyManualAction(QStringLiteral("A"));

    QTest::qWait(1000);
    QCOMPARE(engine.state(QStringLiteral("A")), DeckState::Null); // still blocked

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) != DeckState::Null, 3000);
    QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Paused); // idle-deck bootstrap cues silently

    engine.shutdown();
}

void CrossfadeControllerTest::manualOverrideSignalOnlyFiresOnTransition()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    QSignalSpy spy(&controller, &CrossfadeController::manualOverrideChanged);

    controller.notifyManualAction(QStringLiteral("A"));
    controller.notifyManualAction(QStringLiteral("A")); // redundant — no transition
    QCOMPARE(spy.count(), 1);

    controller.resumeAutoAdvance(QStringLiteral("A"));
    QCOMPARE(spy.count(), 2);
    controller.resumeAutoAdvance(QStringLiteral("A")); // redundant — no transition
    QCOMPARE(spy.count(), 2);

    engine.shutdown();
}

void CrossfadeControllerTest::handsOffResumedFiresOnlyWhenBothDecksBecomeAuto()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    QSignalSpy spy(&controller, &CrossfadeController::handsOffResumed);

    // Both decks start non-overridden already. Overriding then resuming
    // just A (B was never touched, so it's already hands-off) should fire
    // immediately - that's the moment both become simultaneously auto.
    controller.notifyManualAction(QStringLiteral("A"));
    QCOMPARE(spy.count(), 0);
    controller.resumeAutoAdvance(QStringLiteral("A"));
    QCOMPARE(spy.count(), 1);

    // Overriding BOTH, then resuming one at a time: must fire exactly once,
    // only once the SECOND one clears - not on the first.
    controller.notifyManualAction(QStringLiteral("A"));
    controller.notifyManualAction(QStringLiteral("B"));
    controller.resumeAutoAdvance(QStringLiteral("A"));
    QCOMPARE(spy.count(), 1); // B still overridden - not fully resumed yet
    controller.resumeAutoAdvance(QStringLiteral("B"));
    QCOMPARE(spy.count(), 2); // now both clear

    // A redundant resume (deck already not overridden) must not re-fire.
    controller.resumeAutoAdvance(QStringLiteral("B"));
    QCOMPARE(spy.count(), 2);

    engine.shutdown();
}

void CrossfadeControllerTest::manualOverrideAutoClearsOnEos()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    // short_beep.wav (~0.9s) — short enough to reach genuine EOS quickly.
    const QString shortTrack = QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav");
    engine.loadTrack(QStringLiteral("A"), shortTrack);
    engine.play(QStringLiteral("A"));
    controller.notifyManualAction(QStringLiteral("A"));
    QVERIFY(controller.isManualOverride(QStringLiteral("A")));

    // Without the EOS auto-clear, an overridden active deck that finishes
    // would sit stuck forever (override suppresses the crossfade-start
    // check that would otherwise recover from this) — silent dead air.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isManualOverride(QStringLiteral("A")), 5000);

    engine.shutdown();
}

void CrossfadeControllerTest::concurrentManualAndWatchTickActionsOnSameDeck()
{
    // End-to-end integration check, exercising the exact call sequence
    // DeckWidget uses (notifyManualAction, then loadTrack+play directly —
    // see MainWindow's wiring): a manually-loaded deck must stay exactly
    // as the human left it across many subsequent watch ticks, and a
    // track still sitting in the queue for the OTHER deck must never be
    // silently pulled onto this one. (Verified by temporarily removing the
    // idle-deck override guard in onWatchTick(): this specific test still
    // passed, because by the time any Qt timer tick can fire, the engine
    // thread — which runs independently of the Qt event loop — has already
    // settled deck B's state past Null, so the guard's real value here is
    // structural/defense-in-depth on top of what manualOverrideBlocks-
    // AutoLoadOnIdleDeck already exercises directly, not closing a live
    // interleaving window in this particular sequence.)
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);
    TrackRecord queueTrack;
    // Deliberately distinct from both A's track and the user's manual
    // track below — if a regression let the controller win the race, B
    // would end up loaded with THIS file instead.
    queueTrack.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    queueTrack.title = QStringLiteral("Queue Track");
    queueTrack.categoryId = category;
    const qint64 queueTrackId = TrackRepository::upsertScannedTrack(queueTrack);
    QVERIFY(PlaylistRepository::appendToQueue(queueTrackId, QStringLiteral("manual")));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(200);

    // Load deck A directly rather than via the controller's queue-driven
    // bootstrap — with only one item queued, the idle-deck-first bootstrap
    // order would otherwise grab it for B before this test ever gets a
    // chance to manually claim B, leaving nothing in the queue to act as
    // bait for the race this test targets. GStreamer state transitions run
    // on the engine thread independently of the Qt event loop, so this
    // completes in the background regardless of what happens next below —
    // and nothing below pumps the event loop, so the watch timer cannot
    // fire a single tick until this whole setup has already completed.
    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));

    // Simulate a human manually taking over deck B with a DIFFERENT track
    // than the one still sitting in the queue — exactly what DeckWidget
    // does: notify the controller, then issue the engine calls directly.
    const QString userTrack = QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav");
    controller.notifyManualAction(QStringLiteral("B"));
    engine.loadTrack(QStringLiteral("B"), userTrack);
    engine.play(QStringLiteral("B"));

    // Hold across several watch-tick intervals — the bug this targets let
    // the controller's own queue-pull silently win the race and
    // replace/pause the user's manually-loaded track. (short_beep.wav
    // finishing mid-loop doesn't spontaneously change the reported
    // GStreamer state — only an explicit set_state call would — so this
    // assertion holds regardless.)
    for (int i = 0; i < 5; ++i) {
        QTest::qWait(200);
        QCOMPARE(engine.state(QStringLiteral("B")), DeckState::Playing);
    }

    // The queued track was never touched by the controller.
    QCOMPARE(PlaylistRepository::queueItems().size(), 1);

    engine.shutdown();
}

void CrossfadeControllerTest::autoCrossfadeSuppressorBlocksRampedCrossfade()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    const QString trackB = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");

    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), trackB);
    engine.pause(QStringLiteral("B"));

    QTRY_VERIFY_WITH_TIMEOUT(engine.duration(QStringLiteral("A")) > 0, 3000);

    controller.setCrossfadeLeadMs(999'999'999); // eligible on the very next tick
    controller.setAutoCrossfadeSuppressor([]() { return true; });

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    QTest::qWait(1000); // several ticks — would normally have started well within this window
    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(controller.activeDeck(), QStringLiteral("A")); // untouched

    engine.shutdown();
}

void CrossfadeControllerTest::suppressedActiveDeckReachingEosEmitsAutoCrossfadeSuppressed()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    // short_beep.wav (~0.9s) — short enough to reach genuine EOS quickly.
    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);

    controller.setAutoCrossfadeSuppressor([]() { return true; });

    QSignalSpy suppressedSpy(&controller, &CrossfadeController::autoCrossfadeSuppressed);
    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);

    // The suppressor holds from t=0 (default 5s lead time already exceeds
    // short_beep.wav's whole ~0.9s length) straight through to genuine EOS —
    // if suppression didn't actually hold, startedSpy would fire first.
    QTRY_VERIFY_WITH_TIMEOUT(suppressedSpy.count() > 0, 5000);
    QCOMPARE(startedSpy.count(), 0);

    QCOMPARE(suppressedSpy.constFirst().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(suppressedSpy.constFirst().at(1).toString(), QStringLiteral("B"));

    // Bookkeeping doesn't change until completeSuppressedCrossfade() is
    // explicitly called — that timing is entirely up to the caller (e.g.
    // CartAutomationEngine, waiting on a cart to finish).
    QCOMPARE(controller.activeDeck(), QStringLiteral("A"));

    engine.shutdown();
}

void CrossfadeControllerTest::autoCrossfadeSuppressedNotEmittedWhenIdleDeckNotCued()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    engine.play(QStringLiteral("A"));
    // B stays completely untouched (Null) — nothing cued to hand off to, and
    // the queue is empty (init() wipes playlist_items every test), so
    // onWatchTick()'s own idle-refill can't cue it either.

    controller.setAutoCrossfadeSuppressor([]() { return true; });

    QSignalSpy suppressedSpy(&controller, &CrossfadeController::autoCrossfadeSuppressed);

    QTest::qWait(2000); // short_beep.wav reaches genuine EOS well within this
    QCOMPARE(suppressedSpy.count(), 0);

    engine.shutdown();
}

void CrossfadeControllerTest::autoCrossfadeSuppressedNotEmittedWhenActiveDeckManual()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    controller.notifyManualAction(QStringLiteral("A")); // human is driving A

    controller.setAutoCrossfadeSuppressor([]() { return true; });

    QSignalSpy suppressedSpy(&controller, &CrossfadeController::autoCrossfadeSuppressed);

    // A's EOS should fall into the pre-existing manual-override-auto-clear
    // branch instead (see manualOverrideAutoClearsOnEos above) — the two are
    // mutually exclusive via onDeckEos()'s early return once that branch
    // handles it.
    QTRY_VERIFY_WITH_TIMEOUT(!controller.isManualOverride(QStringLiteral("A")), 5000);
    QCOMPARE(suppressedSpy.count(), 0);

    engine.shutdown();
}

void CrossfadeControllerTest::completeSuppressedCrossfadeStartsIdleDeckAndSwapsActive()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);

    // Freeze auto-advance so onWatchTick() can't itself start a ramped
    // crossfade or otherwise touch either deck while this runs — isolates
    // completeSuppressedCrossfade()'s own bookkeeping from anything else.
    controller.setAutoAdvanceEnabled(false);

    QSignalSpy finishedSpy(&controller, &CrossfadeController::autoCrossfadeFinished);

    controller.completeSuppressedCrossfade();

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Playing, 3000);
    QCOMPARE(controller.activeDeck(), QStringLiteral("B"));
    QCOMPARE(controller.idleDeck(), QStringLiteral("A"));
    QCOMPARE(finishedSpy.count(), 1);
    QCOMPARE(finishedSpy.constFirst().at(0).toString(), QStringLiteral("B"));

    // The deck that just finished should be fully unloaded, matching the
    // ramped path's own onCrossfadeFinished() behavior (see
    // autoCrossfadeSwapsActiveDeck above) — both funnel through the same
    // shared finishTransition() helper.
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Null, 10000);

    engine.shutdown();
}

void CrossfadeControllerTest::trackLoadedCarriesTheConsumedQueueEntrysId()
{
    // Regression coverage for the extended trackLoaded(deckId, title,
    // artist, trackId, playlistItemId) signature — UI-layer consumers rely
    // on trackId (a star rating to display) and playlistItemId (DeckWidget,
    // via QueueColorRegistry, to carry a queue-assigned pill color forward
    // onto the deck it landed on).
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);
    TrackRecord track;
    track.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    track.title = QStringLiteral("Track A");
    track.artist = QStringLiteral("Some Artist");
    track.categoryId = category;
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);
    QVERIFY(PlaylistRepository::appendToQueue(trackId, QStringLiteral("manual")));

    const auto queuedBefore = PlaylistRepository::queueItems();
    QCOMPARE(queuedBefore.size(), 1);
    const qint64 expectedPlaylistItemId = queuedBefore.first().playlistItemId;

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine); // watch timer starts immediately, pulls from the queue

    QSignalSpy trackLoadedSpy(&controller, &CrossfadeController::trackLoaded);
    QTRY_VERIFY_WITH_TIMEOUT(trackLoadedSpy.count() > 0, 3000);

    // The idle deck (B) is refilled before the active one — see
    // onWatchTick()'s bootstrap order, also noted in
    // concurrentManualAndWatchTickActionsOnSameDeck() above — so with only
    // one queue entry, it lands on B, not A.
    QCOMPARE(trackLoadedSpy.first().at(0).toString(), QStringLiteral("B"));
    QCOMPARE(trackLoadedSpy.first().at(1).toString(), QStringLiteral("Track A"));
    QCOMPARE(trackLoadedSpy.first().at(2).toString(), QStringLiteral("Some Artist"));
    QCOMPARE(trackLoadedSpy.first().at(3).toLongLong(), trackId);
    QCOMPARE(trackLoadedSpy.first().at(4).toLongLong(), expectedPlaylistItemId);

    engine.shutdown();
}

void CrossfadeControllerTest::trackLoadedFiresBeforeQueueConsumed()
{
    // Regression test: a real bug had these reversed. queueConsumed's own
    // UI-layer consumer (QueueWidget::refresh()) prunes any per-queue-entry
    // display state (e.g. a pill color) for whatever's no longer in the
    // queue — which, by definition, includes the entry that was JUST
    // removed to feed this very load. If queueConsumed fired first, that
    // prune would discard the entry before trackLoaded's own UI-layer
    // consumer (DeckWidget) ever got a chance to claim it via
    // QueueColorRegistry::takeColor(), so the deck would end up with an
    // unrelated fresh color instead of the one the queue had shown.
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);
    TrackRecord track;
    track.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    track.title = QStringLiteral("Track A");
    track.categoryId = category;
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);
    QVERIFY(PlaylistRepository::appendToQueue(trackId, QStringLiteral("manual")));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    QStringList firedOrder;
    connect(&controller, &CrossfadeController::trackLoaded, &controller,
        [&firedOrder]() { firedOrder.append(QStringLiteral("trackLoaded")); });
    connect(&controller, &CrossfadeController::queueConsumed, &controller,
        [&firedOrder]() { firedOrder.append(QStringLiteral("queueConsumed")); });

    QTRY_VERIFY_WITH_TIMEOUT(firedOrder.size() >= 2, 3000);
    QCOMPARE(firedOrder.first(), QStringLiteral("trackLoaded"));
    QCOMPARE(firedOrder.at(1), QStringLiteral("queueConsumed"));

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeUnavailableWhenIdleDeckNotCued()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    // B deliberately left untouched (Null) — nothing to fade into.

    QVERIFY(!controller.canRequestManualFade());

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeAvailableWhenBothDecksReady()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("B")) == DeckState::Paused, 3000);

    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeUnavailableWhileAlreadyCrossfading()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.duration(QStringLiteral("A")) > 0, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    controller.requestManualFade();
    QVERIFY(!controller.canRequestManualFade()); // now mid-fade

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeUnavailableWhenAutoAdvanceDisabled()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    controller.setAutoAdvanceEnabled(false);
    QVERIFY(!controller.canRequestManualFade());

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeUnavailableWhenEitherDeckManual()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    controller.notifyManualAction(QStringLiteral("B"));
    QVERIFY(!controller.canRequestManualFade());

    controller.resumeAutoAdvance(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    engine.shutdown();
}

void CrossfadeControllerTest::requestManualFadeStartsImmediatelyRegardlessOfLeadTime()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1); // would never trigger automatically within this test's window

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));
    QTRY_VERIFY_WITH_TIMEOUT(controller.canRequestManualFade(), 3000);

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    controller.requestManualFade();

    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(0).toString(), QStringLiteral("A"));
    QCOMPARE(startedSpy.first().at(1).toString(), QStringLiteral("B"));

    engine.shutdown();
}

void CrossfadeControllerTest::requestManualFadeIsNoOpWhenUnavailable()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    // B left Null — nothing cued, so a fade shouldn't be possible.
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QVERIFY(!controller.canRequestManualFade());

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    controller.requestManualFade();

    QCOMPARE(startedSpy.count(), 0);

    engine.shutdown();
}

void CrossfadeControllerTest::manualFadeAvailabilityChangedFiresOnlyOnTransition()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    // tone440.wav is only ~4.6s — shorter than the default 5s crossfade
    // lead time, which would otherwise trigger a real automatic crossfade
    // partway through this test's own observation window (same gotcha
    // noted in queueBootstrapNeverPlaysBothDecksAtOnce() above) and flip
    // availability a second, legitimate time — confusing this test's actual
    // target, which is purely about not re-emitting for no reason.
    controller.setCrossfadeLeadMs(1);

    QSignalSpy availabilitySpy(&controller, &CrossfadeController::manualFadeAvailabilityChanged);

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(QStringLiteral("A"));
    engine.loadTrack(QStringLiteral("B"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(QStringLiteral("B"));

    // Goes from unavailable (nothing loaded yet) to available exactly once.
    QTRY_VERIFY_WITH_TIMEOUT(availabilitySpy.count() >= 1, 3000);
    QVERIFY(availabilitySpy.last().at(0).toBool());

    const int countOnceAvailable = availabilitySpy.count();
    QTest::qWait(1000); // several watch ticks — must NOT re-emit while unchanged
    QCOMPARE(availabilitySpy.count(), countOnceAvailable);

    engine.shutdown();
}

void CrossfadeControllerTest::discardIdleCueIsNoOpWhenIdleDeckAlreadyEmpty()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    QCOMPARE(engine.state(controller.idleDeck()), DeckState::Null);
    controller.discardIdleCue(); // must not crash / must stay a no-op
    QCOMPARE(engine.state(controller.idleDeck()), DeckState::Null);

    engine.shutdown();
}

void CrossfadeControllerTest::discardIdleCueIsNoOpWhenIdleDeckManuallyOverridden()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    const QString idle = controller.idleDeck();
    engine.loadTrack(idle, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(idle);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(idle) == DeckState::Paused, 3000);

    controller.notifyManualAction(idle);
    controller.discardIdleCue();

    // Still cued — a human's manual cue is not something to rip out from
    // under them.
    QCOMPARE(engine.state(idle), DeckState::Paused);

    engine.shutdown();
}

void CrossfadeControllerTest::discardIdleCueUnloadsAnOtherwiseCuedIdleDeck()
{
    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);

    const QString idle = controller.idleDeck();
    engine.loadTrack(idle, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(idle);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(idle) == DeckState::Paused, 3000);

    controller.discardIdleCue();
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(idle) == DeckState::Null, 3000);

    engine.shutdown();
}

QTEST_MAIN(CrossfadeControllerTest)
#include "CrossfadeControllerTest.moc"
