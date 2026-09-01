#include "ui/SmartPlaylistPanelWidget.h"

#include "db/Database.h"
#include "db/RotationCategoryRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QLineEdit>
#include <QListWidget>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTableView>
#include <QTest>

using namespace radio::ui;
using namespace radio::db;

class SmartPlaylistPanelWidgetTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void refreshPopulatesTableAndCategoryList();
    void genreFilterNarrowsVisibleRows();
    void categoryCheckboxNarrowsVisibleRows();
    void selectingASavedSmartPlaylistAppliesItsFilter();
    void selectingNewFilterResetsToUnfiltered();
};

void SmartPlaylistPanelWidgetTest::initTestCase()
{
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStationSmartPlaylistPanelWidgetTest"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStationSmartPlaylistPanelWidgetTest"));
    QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).removeRecursively();
    QVERIFY(Database::open());
}

void SmartPlaylistPanelWidgetTest::init()
{
    QSqlDatabase db = Database::handle();
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM tracks"));
    QSqlQuery(db).exec(QStringLiteral("DELETE FROM smart_playlists"));
}

void SmartPlaylistPanelWidgetTest::refreshPopulatesTableAndCategoryList()
{
    TrackRecord track;
    track.filePath = QStringLiteral("/fake/panel-refresh.mp3");
    track.title = QStringLiteral("Refresh Track");
    TrackRepository::upsertScannedTrack(track);

    SmartPlaylistPanelWidget widget;
    widget.show();

    auto* view = widget.findChild<QTableView*>(QStringLiteral("smartPlaylistTableView"));
    QVERIFY(view != nullptr);
    QCOMPARE(view->model()->rowCount(), 1);

    auto* categoryList = widget.findChild<QListWidget*>(QStringLiteral("smartPlaylistCategoryList"));
    QVERIFY(categoryList != nullptr);
    QCOMPARE(categoryList->count(), RotationCategoryRepository::allCategories().size());
}

void SmartPlaylistPanelWidgetTest::genreFilterNarrowsVisibleRows()
{
    TrackRecord ambient;
    ambient.filePath = QStringLiteral("/fake/panel-ambient.mp3");
    ambient.title = QStringLiteral("Ambient Track");
    ambient.genre = QStringLiteral("Ambient");
    TrackRepository::upsertScannedTrack(ambient);

    TrackRecord metal;
    metal.filePath = QStringLiteral("/fake/panel-metal.mp3");
    metal.title = QStringLiteral("Metal Track");
    metal.genre = QStringLiteral("Metal");
    TrackRepository::upsertScannedTrack(metal);

    SmartPlaylistPanelWidget widget;
    widget.show();

    auto* view = widget.findChild<QTableView*>(QStringLiteral("smartPlaylistTableView"));
    QCOMPARE(view->model()->rowCount(), 2);

    auto* genreEdit = widget.findChild<QLineEdit*>(QStringLiteral("smartPlaylistGenreEdit"));
    QVERIFY(genreEdit != nullptr);
    genreEdit->setText(QStringLiteral("ambient"));

    QCOMPARE(view->model()->rowCount(), 1);
}

void SmartPlaylistPanelWidgetTest::categoryCheckboxNarrowsVisibleRows()
{
    const qint64 categoryId = RotationCategoryRepository::addCategory(QStringLiteral("TestCategory"), QString(), 1);

    TrackRecord categorized;
    categorized.filePath = QStringLiteral("/fake/panel-categorized.mp3");
    categorized.title = QStringLiteral("Categorized Track");
    categorized.categoryId = categoryId;
    TrackRepository::upsertScannedTrack(categorized);

    TrackRecord uncategorized;
    uncategorized.filePath = QStringLiteral("/fake/panel-uncategorized.mp3");
    uncategorized.title = QStringLiteral("Uncategorized Track");
    TrackRepository::upsertScannedTrack(uncategorized);

    SmartPlaylistPanelWidget widget;
    widget.show();

    auto* view = widget.findChild<QTableView*>(QStringLiteral("smartPlaylistTableView"));
    QCOMPARE(view->model()->rowCount(), 2);

    auto* categoryList = widget.findChild<QListWidget*>(QStringLiteral("smartPlaylistCategoryList"));
    QListWidgetItem* matchingItem = nullptr;
    for (int i = 0; i < categoryList->count(); ++i) {
        if (categoryList->item(i)->data(Qt::UserRole).toLongLong() == categoryId)
            matchingItem = categoryList->item(i);
    }
    QVERIFY(matchingItem != nullptr);
    matchingItem->setCheckState(Qt::Checked);

    QCOMPARE(view->model()->rowCount(), 1);
}

void SmartPlaylistPanelWidgetTest::selectingASavedSmartPlaylistAppliesItsFilter()
{
    TrackRecord ambient;
    ambient.filePath = QStringLiteral("/fake/panel-saved-ambient.mp3");
    ambient.title = QStringLiteral("Saved Ambient");
    ambient.genre = QStringLiteral("Ambient");
    TrackRepository::upsertScannedTrack(ambient);

    TrackRecord metal;
    metal.filePath = QStringLiteral("/fake/panel-saved-metal.mp3");
    metal.title = QStringLiteral("Saved Metal");
    metal.genre = QStringLiteral("Metal");
    TrackRepository::upsertScannedTrack(metal);

    SmartPlaylistRepository::create(QStringLiteral("Ambient Only"), QStringLiteral(R"({"genre": "Ambient"})"));

    SmartPlaylistPanelWidget widget;
    widget.show();

    auto* combo = widget.findChild<QComboBox*>(QStringLiteral("smartPlaylistCombo"));
    QVERIFY(combo != nullptr);
    const int index = combo->findText(QStringLiteral("Ambient Only"));
    QVERIFY(index >= 0);
    combo->setCurrentIndex(index);

    auto* view = widget.findChild<QTableView*>(QStringLiteral("smartPlaylistTableView"));
    QCOMPARE(view->model()->rowCount(), 1);

    auto* genreEdit = widget.findChild<QLineEdit*>(QStringLiteral("smartPlaylistGenreEdit"));
    QCOMPARE(genreEdit->text(), QStringLiteral("Ambient"));
}

void SmartPlaylistPanelWidgetTest::selectingNewFilterResetsToUnfiltered()
{
    TrackRecord ambient;
    ambient.filePath = QStringLiteral("/fake/panel-reset-ambient.mp3");
    ambient.title = QStringLiteral("Reset Ambient");
    ambient.genre = QStringLiteral("Ambient");
    TrackRepository::upsertScannedTrack(ambient);

    TrackRecord metal;
    metal.filePath = QStringLiteral("/fake/panel-reset-metal.mp3");
    metal.title = QStringLiteral("Reset Metal");
    metal.genre = QStringLiteral("Metal");
    TrackRepository::upsertScannedTrack(metal);

    SmartPlaylistRepository::create(QStringLiteral("Ambient Only"), QStringLiteral(R"({"genre": "Ambient"})"));

    SmartPlaylistPanelWidget widget;
    widget.show();

    auto* combo = widget.findChild<QComboBox*>(QStringLiteral("smartPlaylistCombo"));
    combo->setCurrentIndex(combo->findText(QStringLiteral("Ambient Only")));

    auto* view = widget.findChild<QTableView*>(QStringLiteral("smartPlaylistTableView"));
    QCOMPARE(view->model()->rowCount(), 1);

    combo->setCurrentIndex(0); // "(New Filter)"

    QCOMPARE(view->model()->rowCount(), 2);
    auto* genreEdit = widget.findChild<QLineEdit*>(QStringLiteral("smartPlaylistGenreEdit"));
    QVERIFY(genreEdit->text().isEmpty());
}

QTEST_MAIN(SmartPlaylistPanelWidgetTest)
#include "SmartPlaylistPanelWidgetTest.moc"
