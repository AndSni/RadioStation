#include "ui/BlockTransitionController.h"

#include "audio/AudioEngine.h"
#include "audio/CrossfadeController.h"

#include "db/ClockRepository.h"
#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "scheduler/AutoDjEngine.h"
#include "scheduler/ClockEngine.h"

#include <QCoreApplication>
#include <QDate>
#include <QDir>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>
#include <QTimeZone>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;
using namespace radio::scheduler;
using namespace radio::db;

namespace {
qint64 insertTrack(const QString& path)
{
    QSqlQuery query(Database::handle());
    query.prepare(QStringLiteral(
        "INSERT INTO tracks (file_path, title, artist, category_id, duration_ms, missing, play_count) "
        "VALUES (:path, :path, 'Artist', -1, 180000, 0, 0)"));
    query.bindValue(QStringLiteral(":path"), path);
    query.exec();
    return query.lastInsertId().toLongLong();
}

qint64 makeBlockWithTrack(const QString& name, int startMinute, int endMinute, const QString& trackPath)
{
    const qint64 playlistId = PlaylistRepository::createPlaylist(name + QStringLiteral(" Playlist"));
    const qint64 trackId = insertTrack(trackPath);
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    ScheduleBlockRecord block;
    block.name = name;
    block.daysMask = 1 << (QDate::currentDate().dayOfWeek() - 1); // today's bit — irrelevant to the synthetic `now` values used below, but must still be set for resolveActiveBlockId() to match at all
    block.startMinute = startMinute;
    block.endMinute = endMinute;
    block.playlistId = playlistId;
    return ScheduleBlockRepository::createBlock(block);
}

qint64 makeAllDayClockBlock(qint64 clockId)
{
    ScheduleBlockRecord block;
    block.name = QStringLiteral("Clock Block");
    block.daysMask = 1 << (QDate::currentDate().dayOfWeek() - 1);
    block.startMinute = 0;
    block.endMinute = 1440;
    block.clockId = clockId;
    return ScheduleBlockRepository::createBlock(block);
}
}

class BlockTransitionControllerTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void preStagesNextBlockAheadOfTheBoundary();
    void catchesUpWhenBoundaryIsReachedWithoutPriorPreStaging();
    void forcedFadeFiresAtBoundaryOncePreStaged();
    void forcedFadeStaysPendingUntilManualOverrideReleased();
    void dstFallBackRepeatOfTheSameBoundaryDoesNotRefireTheForcedFade();

    void hardTimedClockElementForcesFadeAtItsMinute();
    void hardTimedFadeStillDefersToManualOverride();
};

void BlockTransitionControllerTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationBlockTransitionControllerTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationBlockTransitionControllerTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void BlockTransitionControllerTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM schedule_blocks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_wheel_state"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clock_elements"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM clocks"));
}

void BlockTransitionControllerTest::preStagesNextBlockAheadOfTheBoundary()
{
    const QDate today = QDate::currentDate();
    makeBlockWithTrack(QStringLiteral("Block A"), 0, 10, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    makeBlockWithTrack(QStringLiteral("Block B"), 10, 20, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    AutoDjEngine autoDjEngine;
    autoDjEngine.setQueueLowWatermark(1); // each block's playlist here has exactly one track
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setLeadSeconds(5);

    // Baseline tick, well inside Block A, far from its end — must not
    // pre-stage anything yet.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 0)));
    QVERIFY(PlaylistRepository::queueItems().isEmpty());

    // 4 seconds left in Block A - within the 5s lead - should pre-stage
    // Block B's content into the queue.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 9, 56)));

    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().filePath, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    engine.shutdown();
}

void BlockTransitionControllerTest::catchesUpWhenBoundaryIsReachedWithoutPriorPreStaging()
{
    const QDate today = QDate::currentDate();
    makeBlockWithTrack(QStringLiteral("Block A"), 0, 10, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    makeBlockWithTrack(QStringLiteral("Block B"), 10, 20, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    AutoDjEngine autoDjEngine;
    autoDjEngine.setQueueLowWatermark(1); // each block's playlist here has exactly one track
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setLeadSeconds(5);

    // Baseline tick, far from the boundary - no pre-stage.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 0, 0)));
    QVERIFY(PlaylistRepository::queueItems().isEmpty());

    // Jump straight past the boundary - the lead-time tick that would
    // normally have pre-staged Block B never happened, so this must catch
    // up right here instead of losing the cutover.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 5)));

    const auto items = PlaylistRepository::queueItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().filePath, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    engine.shutdown();
}

void BlockTransitionControllerTest::forcedFadeFiresAtBoundaryOncePreStaged()
{
    const QDate today = QDate::currentDate();
    makeBlockWithTrack(QStringLiteral("Block A"), 0, 10, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    makeBlockWithTrack(QStringLiteral("Block B"), 10, 20, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1); // suppress the normal end-of-track auto-crossfade racing this test
    AutoDjEngine autoDjEngine;
    autoDjEngine.setQueueLowWatermark(1); // each block's playlist here has exactly one track
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setLeadSeconds(5);

    // Whatever's already playing when the boundary approaches - independent
    // of the block/queue machinery.
    engine.loadTrack(controller.activeDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(controller.activeDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.activeDeck()) == DeckState::Playing, 3000);

    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 0))); // baseline
    transitionController.evaluateAt(QDateTime(today, QTime(0, 9, 56))); // pre-stage Block B

    // Let CrossfadeController's own real watch timer notice the idle deck
    // is empty and cue Block B's track onto it from the freshly pre-staged
    // queue.
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.idleDeck()) == DeckState::Paused, 3000);
    QVERIFY(controller.canRequestManualFade());

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    const QString preStagedIdleDeck = controller.idleDeck();

    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 2))); // boundary crossed

    QCOMPARE(startedSpy.count(), 1);
    QCOMPARE(startedSpy.first().at(1).toString(), preStagedIdleDeck);

    engine.shutdown();
}

void BlockTransitionControllerTest::forcedFadeStaysPendingUntilManualOverrideReleased()
{
    const QDate today = QDate::currentDate();
    makeBlockWithTrack(QStringLiteral("Block A"), 0, 10, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    makeBlockWithTrack(QStringLiteral("Block B"), 10, 20, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1);
    AutoDjEngine autoDjEngine;
    autoDjEngine.setQueueLowWatermark(1); // each block's playlist here has exactly one track
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setLeadSeconds(5);

    engine.loadTrack(controller.activeDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(controller.activeDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.activeDeck()) == DeckState::Playing, 3000);

    // The human has taken over the idle deck before the boundary arrives.
    controller.notifyManualAction(controller.idleDeck());

    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 0)));
    transitionController.evaluateAt(QDateTime(today, QTime(0, 9, 56)));

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);

    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 2))); // boundary crossed, but idle deck is manual
    QCOMPARE(startedSpy.count(), 0);

    // A later tick after the human hands back control should pick up the
    // still-pending forced fade.
    controller.resumeAutoAdvance(controller.idleDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.idleDeck()) != DeckState::Null, 3000);
    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 3)));
    QCOMPARE(startedSpy.count(), 1);

    engine.shutdown();
}

void BlockTransitionControllerTest::dstFallBackRepeatOfTheSameBoundaryDoesNotRefireTheForcedFade()
{
    // Regression test for the DST fall-back guard: BlockTimeResolver
    // resolves purely from QDateTime::time() (local wall-clock time-of-day),
    // so during the repeated local hour after a fall-back, the identical
    // (from, to) block transition can be detected twice even though real
    // elapsed time only crossed it once. Reproduced here without depending
    // on the host's actual timezone/DST calendar: two QDateTimes built with
    // a fixed-offset QTimeZone carry the SAME .time() (so
    // resolveActiveBlockId() sees an identical transition) but a DIFFERENT
    // UTC instant one hour apart (so toUTC().toSecsSinceEpoch() -- what the
    // guard actually keys on -- correctly tells them apart from a genuine
    // second occurrence).
    const QDate today = QDate::currentDate();
    makeBlockWithTrack(QStringLiteral("Block A"), 0, 10, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    makeBlockWithTrack(QStringLiteral("Block B"), 10, 20, QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1); // suppress the normal end-of-track auto-crossfade racing this test
    AutoDjEngine autoDjEngine;
    autoDjEngine.setQueueLowWatermark(1); // each block's playlist here has exactly one track
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setLeadSeconds(5);

    engine.loadTrack(controller.activeDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(controller.activeDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.activeDeck()) == DeckState::Playing, 3000);

    constexpr int kOffsetSecs = 2 * 60 * 60; // an arbitrary fixed offset, one hour apart from kOffsetSecs - 3600 below
    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 0), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs))); // baseline
    transitionController.evaluateAt(QDateTime(today, QTime(0, 9, 56), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs))); // pre-stage Block B

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.idleDeck()) == DeckState::Paused, 3000);
    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);

    // Boundary crossed for real: fires the forced fade once.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 2), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs)));
    QCOMPARE(startedSpy.count(), 1);

    // The DST fall-back artifact: identical .time() sequence walking through
    // the SAME A->B boundary again, one hour of real elapsed time earlier in
    // UTC terms (offset one hour smaller — the pre-transition offset the
    // repeated local hour's first pass would have used) rather than later.
    // Must be silently absorbed, not treated as a second genuine boundary
    // crossing.
    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 0), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs - 3600)));
    transitionController.evaluateAt(QDateTime(today, QTime(0, 9, 56), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs - 3600)));
    transitionController.evaluateAt(QDateTime(today, QTime(0, 10, 2), QTimeZone::fromSecondsAheadOfUtc(kOffsetSecs - 3600)));

    QCOMPARE(startedSpy.count(), 1);

    engine.shutdown();
}

void BlockTransitionControllerTest::hardTimedClockElementForcesFadeAtItsMinute()
{
    const QDate today = QDate::currentDate();
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Clock Playlist"));
    const qint64 trackId = insertTrack(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.playlistId = playlistId;
    element.minuteOffset = 5;
    element.timingMode = QStringLiteral("hard");
    ClockRepository::createElement(element);
    makeAllDayClockBlock(clockId);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1); // suppress the normal end-of-track auto-crossfade racing this test
    ClockEngine clockEngine;
    AutoDjEngine autoDjEngine;
    autoDjEngine.setClockEngine(&clockEngine);
    autoDjEngine.setQueueLowWatermark(1);
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setClockEngine(&clockEngine);
    transitionController.setLeadSeconds(5);

    engine.loadTrack(controller.activeDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(controller.activeDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.activeDeck()) == DeckState::Playing, 3000);

    // Cue the idle deck directly rather than via AutoDjEngine's own queue
    // filling -- that path keys off the REAL wall clock (see
    // ClockEngine::fillLimit()'s "now" parameter), which would make this
    // test's own outcome depend on what real minute-of-hour it happens to
    // run at, unrelated to what's being tested here (BlockTransitionController's
    // hard-cut trigger, driven entirely by the synthetic `now` passed to
    // evaluateAt() below).
    engine.loadTrack(controller.idleDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(controller.idleDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.idleDeck()) == DeckState::Paused, 3000);

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);

    transitionController.evaluateAt(QDateTime(today, QTime(0, 0, 0))); // baseline, well before :05
    QCOMPARE(startedSpy.count(), 0);

    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 1))); // the element's minute has arrived
    QCOMPARE(startedSpy.count(), 1);

    engine.shutdown();
}

void BlockTransitionControllerTest::hardTimedFadeStillDefersToManualOverride()
{
    const QDate today = QDate::currentDate();
    const qint64 playlistId = PlaylistRepository::createPlaylist(QStringLiteral("Clock Playlist"));
    const qint64 trackId = insertTrack(QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    PlaylistRepository::addTrackToPlaylist(playlistId, trackId, QStringLiteral("manual"));

    const qint64 clockId = ClockRepository::createClock(QStringLiteral("Test Clock"));
    ClockElementRecord element;
    element.clockId = clockId;
    element.elementType = QStringLiteral("music_playlist");
    element.playlistId = playlistId;
    element.minuteOffset = 5;
    element.timingMode = QStringLiteral("hard");
    ClockRepository::createElement(element);
    makeAllDayClockBlock(clockId);

    AudioEngine engine;
    engine.start();
    CrossfadeController controller(&engine);
    controller.setCrossfadeLeadMs(1);
    ClockEngine clockEngine;
    AutoDjEngine autoDjEngine;
    autoDjEngine.setClockEngine(&clockEngine);
    autoDjEngine.setQueueLowWatermark(1);
    BlockTransitionController transitionController(&controller, &autoDjEngine);
    transitionController.setClockEngine(&clockEngine);
    transitionController.setLeadSeconds(5);

    engine.loadTrack(controller.activeDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.play(controller.activeDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.activeDeck()) == DeckState::Playing, 3000);

    // Cue the idle deck directly -- see the analogous comment in
    // hardTimedClockElementForcesFadeAtItsMinute() for why not via
    // AutoDjEngine's own (real-wall-clock-bound) queue filling.
    engine.loadTrack(controller.idleDeck(), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav"));
    engine.pause(controller.idleDeck());
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(controller.idleDeck()) == DeckState::Paused, 3000);

    // The human has taken over the idle deck before the element's minute arrives.
    controller.notifyManualAction(controller.idleDeck());

    QSignalSpy startedSpy(&controller, &CrossfadeController::autoCrossfadeStarted);
    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 1))); // due, but idle deck is manual
    QCOMPARE(startedSpy.count(), 0);

    // A later tick after the human hands back control should pick up the
    // still-pending forced fade.
    controller.resumeAutoAdvance(controller.idleDeck());
    transitionController.evaluateAt(QDateTime(today, QTime(0, 5, 2)));
    QCOMPARE(startedSpy.count(), 1);

    engine.shutdown();
}

QTEST_MAIN(BlockTransitionControllerTest)
#include "BlockTransitionControllerTest.moc"
