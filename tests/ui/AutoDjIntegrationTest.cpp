#include "ui/ConsoleButton.h"
#include "ui/DeckWidget.h"
#include "ui/DotMatrixDisplay.h"
#include "ui/MainWindow.h"

#include "db/Database.h"
#include "db/RotationCategoryRepository.h"
#include "db/TrackRepository.h"

#include <QCoreApplication>
#include <QDir>
#include <QLabel>
#include <QPushButton>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTest>

#include <gst/gst.h>

using namespace radio::ui;
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

DeckWidget* findDeckWidget(QWidget* parent, const QString& deckId)
{
    for (auto* deck : parent->findChildren<DeckWidget*>()) {
        if (deck->deckId() == deckId)
            return deck;
    }
    return nullptr;
}

// The deck's play state now lives in the PLAY button's own light bar
// (ConsoleButton::isLit()) rather than a "State:" QLabel.
bool deckIsPlaying(DeckWidget* deck)
{
    auto* play = qobject_cast<ConsoleButton*>(findButtonByText(deck, QStringLiteral("PLAY")));
    return play && play->isLit();
}

QString titleLabelText(DeckWidget* deck)
{
    auto* label = deck->findChild<DotMatrixDisplay*>(QStringLiteral("titleLabel"));
    return label ? label->text() : QString();
}
}

// The missing integration test identified during the reliability audit: no
// prior test wired AutoDjEngine + CrossfadeController + AudioEngine
// together the way MainWindow actually does at runtime — each was only
// ever tested in isolation. This instantiates a real MainWindow, seeds the
// library, lets the station run hands-off exactly as a real user would
// experience at launch, then drives an actual button click and confirms
// the full signal chain (DeckWidget -> CrossfadeController -> the always-
// visible Auto-DJ status on the Controls toolbar) is genuinely wired up in
// production, not just correct in isolated unit tests.
class AutoDjIntegrationTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void manualDeckActionHoldsWhileStationRunsHandsOff();
};

void AutoDjIntegrationTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationAutoDjIntegrationTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationAutoDjIntegrationTest"));
    gst_init(nullptr, nullptr);
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void AutoDjIntegrationTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlist_items"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM rotation_categories"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM playlists"));
}

void AutoDjIntegrationTest::manualDeckActionHoldsWhileStationRunsHandsOff()
{
    const qint64 category = RotationCategoryRepository::addCategory(QStringLiteral("Music"), QString(), 1);

    // Both candidate tracks deliberately use tone880.wav (~9.3s), not
    // tone440.wav (~4.6s): CrossfadeController's default crossfadeLeadMs is
    // 5000ms, and AutoDjEngine's candidate picker chooses randomly between
    // trackA/trackB for deck A — a track shorter than the lead time
    // legitimately triggers an auto-crossfade (by design) almost
    // immediately after reaching PLAYING, which would silently swap deck A
    // out from under this test's own assertions.
    TrackRecord trackA;
    trackA.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    trackA.title = QStringLiteral("Track A");
    trackA.categoryId = category;
    TrackRepository::upsertScannedTrack(trackA);

    TrackRecord trackB;
    trackB.filePath = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone880.wav");
    trackB.title = QStringLiteral("Track B");
    trackB.categoryId = category;
    TrackRepository::upsertScannedTrack(trackB);

    MainWindow window;
    window.show();

    // The station bootstraps and starts running hands-off, exactly as a
    // real user would experience at launch (AutoDjEngine fills the queue,
    // CrossfadeController pulls from it onto both decks).
    auto* deckA = findDeckWidget(&window, QStringLiteral("A"));
    QVERIFY(deckA != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(deckIsPlaying(deckA), 5000);

    // The exact bug this assertion catches: the deck display previously
    // stayed stuck on "No track loaded" for tracks loaded via the Auto-DJ
    // queue-pull path. Which of the two tracks lands on deck A isn't
    // deterministic, so assert it's one of the real titles, not the
    // placeholder.
    const QString deckATitle = titleLabelText(deckA);
    QVERIFY2(deckATitle == QStringLiteral("Track A") || deckATitle == QStringLiteral("Track B"),
        qPrintable(QStringLiteral("Deck A title was '%1', expected a real track title, not the placeholder")
                       .arg(deckATitle)));

    // A human clicks Stop on deck A mid-run. Assert the FULL real wiring
    // actually connects DeckWidget's manualActionTaken all the way to the
    // always-visible Auto-DJ status readout on the Controls toolbar (which
    // replaced the per-deck override badge — see docs/console-ui-proposal.md
    // §6). The status line is one rich string (Auto-DJ state, block, now
    // playing, streaming) — check the Auto-DJ segment.
    auto* status = window.findChild<QLabel*>(QStringLiteral("autoDjStatusLabel"));
    QVERIFY(status != nullptr);
    QVERIFY(status->text().contains(QStringLiteral("hands-off")));
    QVERIFY(!status->text().contains(QStringLiteral("MANUAL")));

    auto* stopButton = findButtonByText(deckA, QStringLiteral("STOP"));
    QVERIFY(stopButton != nullptr);
    QTest::mouseClick(stopButton, Qt::LeftButton);

    QTRY_VERIFY_WITH_TIMEOUT(status->text().contains(QStringLiteral("MANUAL")), 3000);
    QVERIFY(status->text().contains(QStringLiteral("MANUAL (A)")));

    // Holds across several subsequent watch-tick intervals — the status
    // switching once isn't enough; it must not get silently cleared by the
    // still-running automation.
    for (int i = 0; i < 5; ++i) {
        QTest::qWait(200);
        QVERIFY(status->text().contains(QStringLiteral("MANUAL")));
        QVERIFY(!deckIsPlaying(deckA));
    }

    window.close();
}

QTEST_MAIN(AutoDjIntegrationTest)
#include "AutoDjIntegrationTest.moc"
