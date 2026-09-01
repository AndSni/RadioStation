#include "ui/DeckWidget.h"
#include "ui/AudioObjectMime.h"
#include "ui/ConsoleButton.h"
#include "ui/DotMatrixDisplay.h"
#include "ui/StarRatingWidget.h"

#include "audio/AudioEngine.h"

#include "db/Database.h"
#include "db/PlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QDropEvent>
#include <QGroupBox>
#include <QMimeData>
#include <QPushButton>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
using namespace radio::audio;
using namespace radio::db;

namespace {
QPushButton* findButtonByText(QWidget* parent, const QString& text)
{
    for (auto* button : parent->findChildren<QPushButton*>()) {
        if (button->text() == text)
            return button;
    }
    return nullptr;
}

// dropEvent()/dragEnterEvent() are protected (standard QWidget access level
// for these) and, unlike mouse/keyboard events, Qt does not route a
// synthetic QDropEvent sent via QCoreApplication::sendEvent() through them —
// real drag-and-drop delivery goes through a separate, platform-driven path
// that a bare sendEvent() doesn't engage. Calling the protected override
// directly via this test-only subclass (the same pattern used for
// QueueListWidget::mimeData() in QueueListWidgetTest.cpp) is the reliable
// way to exercise this logic without a real OS-level drag gesture.
class TestableDeckWidget : public DeckWidget {
public:
    using DeckWidget::DeckWidget;
    void callDropEvent(QDropEvent* event) { dropEvent(event); }
};
}

class DeckWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void playClickCallsEngineAndEmitsManualAction();
    void transportButtonsDisabledUntilATrackIsLoaded();
    void cueBlinksWhileCuedAndStopsOncePlaying();
    void trackLoadedUpdatesVisibleTitle();
    void trackLoadedEnablesStarRatingAndReflectsCurrentValue();
    void clickingStarRatingPersistsIt();
    void trackLoadedDoesNotTintThePanelBackground();
    void dropFromQueueRemovesItem();
    void dropFromLibraryLoadsWithoutTouchingQueue();
    void dropWithUnknownTrackIdIsIgnored();
};

void DeckWidgetTest::initTestCase()
{
    gst_init(nullptr, nullptr);

    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationDeckWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationDeckWidgetTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void DeckWidgetTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
}

void DeckWidgetTest::playClickCallsEngineAndEmitsManualAction()
{
    AudioEngine engine;
    engine.start();

    const QString fixture = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), fixture);
    engine.pause(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Paused, 3000);

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    QSignalSpy manualSpy(&widget, &DeckWidget::manualActionTaken);

    auto* playButton = findButtonByText(&widget, QStringLiteral("PLAY"));
    QVERIFY(playButton != nullptr);
    QTest::mouseClick(playButton, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QCOMPARE(manualSpy.count(), 1);
    QCOMPARE(manualSpy.takeFirst().at(0).toString(), QStringLiteral("A"));

    // The PLAY button's light bar now carries the "is playing" state — no
    // separate State: label any more.
    auto* consolePlay = qobject_cast<ConsoleButton*>(playButton);
    QVERIFY(consolePlay != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(consolePlay->isLit(), 3000);

    // Clicking again pauses (one combined play/pause button). The button's
    // lit state follows the engine's deckStateChanged signal, which can
    // land a beat after engine.state() itself flips — so retry.
    QTest::mouseClick(playButton, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Paused, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!consolePlay->isLit(), 3000);

    engine.shutdown();
}

void DeckWidgetTest::transportButtonsDisabledUntilATrackIsLoaded()
{
    AudioEngine engine;
    engine.start();

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    for (const QString& label : { QStringLiteral("CUE"), QStringLiteral("PLAY"), QStringLiteral("STOP") }) {
        auto* button = findButtonByText(&widget, label);
        QVERIFY2(button != nullptr, qPrintable(label));
        QVERIFY2(!button->isEnabled(), qPrintable(label)); // nothing loaded yet

        // No "Load..." button — drag-and-drop is the only load path now.
        QVERIFY(findButtonByText(&widget, QStringLiteral("Load...")) == nullptr);
    }

    engine.shutdown();
}

void DeckWidgetTest::cueBlinksWhileCuedAndStopsOncePlaying()
{
    AudioEngine engine;
    engine.start();

    const QString fixture = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), fixture);
    engine.pause(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Paused, 3000);

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();
    // The deck learns "a track is loaded" from onTrackLoaded / a drop / the
    // engine's state signal; simulate the queue-pull path.
    widget.onTrackLoaded(QStringLiteral("A"), QStringLiteral("Cued"), QString(), -1, -1);

    auto* cue = qobject_cast<ConsoleButton*>(findButtonByText(&widget, QStringLiteral("CUE")));
    QVERIFY(cue != nullptr);
    QVERIFY(cue->isBlinking()); // loaded but never played -> cued and waiting

    auto* play = findButtonByText(&widget, QStringLiteral("PLAY"));
    QTest::mouseClick(play, Qt::LeftButton);
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!cue->isBlinking(), 3000); // playback started -> no longer "waiting"

    engine.shutdown();
}

void DeckWidgetTest::trackLoadedUpdatesVisibleTitle()
{
    // Regression test for a real, previously-shipped bug: the title label
    // was only ever set by DeckWidget's own manual Load button — a track
    // loaded via any other path (e.g. Auto-DJ's queue-pull, wired through
    // CrossfadeController::trackLoaded) left it stuck on "No track loaded"
    // even while the deck was actually playing. Asserts on what's actually
    // VISIBLE, not internal engine state, per that lesson.
    AudioEngine engine;
    engine.start();

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    auto* titleLabel = widget.findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->text(), QStringLiteral("No track loaded"));

    // A signal for a DIFFERENT deck id must be ignored, matching the
    // existing onManualOverrideChanged/onDeckStateChanged guard pattern.
    widget.onTrackLoaded(QStringLiteral("B"), QStringLiteral("Wrong Deck's Track"), QStringLiteral("Nobody"), 1, 42);
    QCOMPARE(titleLabel->text(), QStringLiteral("No track loaded"));

    // playlistItemId of -1 (no color registry / no match) is fine — this
    // widget was constructed without one, so it always falls back to the
    // neutral pill color regardless of the id passed here. "Artist — Title"
    // matches the convention used everywhere else (Library/Queue/Playlist),
    // via the shared formatTrackLabel() helper.
    widget.onTrackLoaded(QStringLiteral("A"), QStringLiteral("Real Track Title"), QStringLiteral("Real Artist"), 7, -1);
    QCOMPARE(titleLabel->text(), QStringLiteral("Real Artist — Real Track Title"));

    // Empty artist falls back to just the title, same fallback style used
    // elsewhere (Queue/Playlist/Library pills).
    widget.onTrackLoaded(QStringLiteral("A"), QStringLiteral("No Artist Track"), QString(), 8, -1);
    QCOMPARE(titleLabel->text(), QStringLiteral("No Artist Track"));

    engine.shutdown();
}

void DeckWidgetTest::trackLoadedEnablesStarRatingAndReflectsCurrentValue()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/tmp/does-not-need-to-exist.wav");
    track.title = QStringLiteral("Rated Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);
    QVERIFY(TrackRepository::setRating(trackId, 3));

    AudioEngine engine;
    engine.start();

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    auto* starRating = widget.findChild<StarRatingWidget*>();
    QVERIFY(starRating != nullptr);
    QVERIFY(!starRating->isEnabled()); // nothing loaded yet
    QCOMPARE(starRating->rating(), 0);

    widget.onTrackLoaded(QStringLiteral("A"), track.title, QString(), trackId, -1);
    QVERIFY(starRating->isEnabled());
    QCOMPARE(starRating->rating(), 3);

    engine.shutdown();
}

void DeckWidgetTest::clickingStarRatingPersistsIt()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/tmp/does-not-need-to-exist-2.wav");
    track.title = QStringLiteral("Clickable Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);

    AudioEngine engine;
    engine.start();

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();
    widget.onTrackLoaded(QStringLiteral("A"), track.title, QString(), trackId, -1);

    auto* starRating = widget.findChild<StarRatingWidget*>();
    QVERIFY(starRating != nullptr);

    QSignalSpy ratingSpy(starRating, &StarRatingWidget::ratingChanged);
    // Click roughly in the middle of the 4th star (0-indexed star 3 of 5, at
    // 16px each — matches StarRatingWidget's own kStarSize).
    QTest::mouseClick(starRating, Qt::LeftButton, Qt::NoModifier, QPoint(3 * 16 + 8, 8));

    QCOMPARE(ratingSpy.count(), 1);
    QCOMPARE(ratingSpy.first().at(0).toInt(), 4);
    QCOMPARE(TrackRepository::trackById(trackId).rating, 4);

    engine.shutdown();
}

void DeckWidgetTest::trackLoadedDoesNotTintThePanelBackground()
{
    AudioEngine engine;
    engine.start();

    DeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    auto* box = widget.findChild<QGroupBox*>();
    QVERIFY(box != nullptr);
    auto* display = widget.findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    QVERIFY(display != nullptr);

    TrackRecord track;
    track.filePath = QStringLiteral("/tmp/tint-test.wav");
    track.title = QStringLiteral("Tinted Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);

    widget.onTrackLoaded(QStringLiteral("A"), track.title, QString(), trackId, -1);

    // The panel background must NOT be tinted (docs/console-ui-proposal.md
    // §3: docks stay a unified background regardless of content). The
    // display itself is a fixed console red now.
    Q_UNUSED(display);
    QVERIFY(!box->styleSheet().contains(QStringLiteral("background-color")));

    engine.shutdown();
}

void DeckWidgetTest::dropFromQueueRemovesItem()
{
    TrackRecord track;
    track.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    track.title = QStringLiteral("Queued Track");
    track.artist = QStringLiteral("Queue Artist");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);
    QVERIFY(PlaylistRepository::appendToQueue(trackId, QStringLiteral("manual")));
    const qint64 playlistItemId = PlaylistRepository::queueItems().first().playlistItemId;

    AudioEngine engine;
    engine.start();
    TestableDeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    AudioObjectDragPayload payload;
    payload.trackId = trackId;
    payload.playlistItemId = playlistItemId;
    payload.fromQueue = true;
    auto* mime = new QMimeData();
    AudioObjectMime::encode(mime, payload);

    QDropEvent dropEvent(QPointF(5, 5), Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    widget.callDropEvent(&dropEvent);

    auto* titleLabel = widget.findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->text(), QStringLiteral("Queue Artist — Queued Track"));

    // Removed from the queue — the destructive pairing.
    QVERIFY(PlaylistRepository::queueItems().isEmpty());

    engine.shutdown();
}

void DeckWidgetTest::dropFromLibraryLoadsWithoutTouchingQueue()
{
    TrackRecord track;
    track.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    track.title = QStringLiteral("Library Track");
    const qint64 trackId = TrackRepository::upsertScannedTrack(track);

    AudioEngine engine;
    engine.start();
    TestableDeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    AudioObjectDragPayload payload;
    payload.trackId = trackId;
    payload.fromQueue = false; // dragged directly from Library, never touched the queue
    auto* mime = new QMimeData();
    AudioObjectMime::encode(mime, payload);

    QDropEvent dropEvent(QPointF(5, 5), Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    widget.callDropEvent(&dropEvent);

    auto* titleLabel = widget.findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->text(), QStringLiteral("Library Track"));

    engine.shutdown();
}

void DeckWidgetTest::dropWithUnknownTrackIdIsIgnored()
{
    AudioEngine engine;
    engine.start();
    TestableDeckWidget widget(QStringLiteral("A"), &engine);
    widget.show();

    auto* titleLabel = widget.findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    QVERIFY(titleLabel != nullptr);
    QCOMPARE(titleLabel->text(), QStringLiteral("No track loaded"));

    AudioObjectDragPayload payload;
    payload.trackId = 999999; // no such track
    auto* mime = new QMimeData();
    AudioObjectMime::encode(mime, payload);

    QDropEvent dropEvent(QPointF(5, 5), Qt::MoveAction, mime, Qt::LeftButton, Qt::NoModifier);
    widget.callDropEvent(&dropEvent);

    // Nothing to load — display stays exactly as it was.
    QCOMPARE(titleLabel->text(), QStringLiteral("No track loaded"));

    engine.shutdown();
}

QTEST_MAIN(DeckWidgetTest)
#include "DeckWidgetTest.moc"
