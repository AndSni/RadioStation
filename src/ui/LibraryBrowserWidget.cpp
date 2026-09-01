#include "LibraryBrowserWidget.h"
#include "AudioPillDelegate.h"
#include "ImportDialog.h"
#include "RatingDelegate.h"
#include "TrackEditDialog.h"
#include "TrackTableModel.h"

#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/TrackRepository.h"

#include <QAction>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QTableView>
#include <QTimer>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::PlaylistRepository;
using radio::db::RotationCategoryRepository;
using radio::db::TrackRepository;

namespace {
const QString kSettingsHiddenColumns = QStringLiteral("library/hiddenColumns");
}

LibraryBrowserWidget::LibraryBrowserWidget(QWidget* parent)
    : QWidget(parent)
{
    m_model = new TrackTableModel(this);

    auto* topRow = new QHBoxLayout();
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search title/artist/album/genre..."));
    topRow->addWidget(m_searchEdit, 1);

    m_importButton = new QPushButton(QStringLiteral("Import Folder..."), this);
    topRow->addWidget(m_importButton);

    m_addToQueueButton = new QPushButton(QStringLiteral("Add to Queue"), this);
    topRow->addWidget(m_addToQueueButton);

    m_addToPlaylistButton = new QPushButton(QStringLiteral("Add to Playlist..."), this);
    topRow->addWidget(m_addToPlaylistButton);

    m_view = new QTableView(this);
    m_view->setModel(m_model);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    // Drag source only — Library never accepts drops (nothing drags INTO it).
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setVisible(false);
    m_view->setItemDelegateForColumn(TrackTableModel::RatingColumn, new RatingDelegate(m_view));
    m_view->setItemDelegateForColumn(TrackTableModel::TitleColumn, new AudioPillDelegate(m_view));

    m_view->horizontalHeader()->setContextMenuPolicy(Qt::CustomContextMenu);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(topRow);
    layout->addWidget(m_view, 1);
    setLayout(layout);

    // Debounced (~200ms) rather than searching on every keystroke — a leading-
    // wildcard search is a full-table scan (see TrackRepository::searchTracks()'s
    // doc comment), a real, user-visible stall per keystroke at real-world
    // library sizes if run that often.
    m_searchDebounceTimer = new QTimer(this);
    m_searchDebounceTimer->setSingleShot(true);
    m_searchDebounceTimer->setInterval(200);
    connect(m_searchDebounceTimer, &QTimer::timeout, this, &LibraryBrowserWidget::onSearchDebounceTimeout);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &LibraryBrowserWidget::onSearchTextChanged);
    connect(m_importButton, &QPushButton::clicked, this, &LibraryBrowserWidget::onImportClicked);
    connect(m_view, &QTableView::doubleClicked, this, &LibraryBrowserWidget::onEditRequested);
    connect(m_addToQueueButton, &QPushButton::clicked, this, &LibraryBrowserWidget::onAddToQueueClicked);
    connect(m_addToPlaylistButton, &QPushButton::clicked, this, &LibraryBrowserWidget::onAddToPlaylistClicked);
    connect(static_cast<RatingDelegate*>(m_view->itemDelegateForColumn(TrackTableModel::RatingColumn)),
        &RatingDelegate::ratingEdited, this, &LibraryBrowserWidget::onRatingEdited);
    connect(m_view->horizontalHeader(), &QHeaderView::customContextMenuRequested, this,
        &LibraryBrowserWidget::onHeaderContextMenuRequested);

    restoreColumnVisibility();
    // Deferred rather than called synchronously here: this constructor runs
    // before the main window is ever painted, and refresh()'s full library
    // scan is real work that scales with library size -- QTimer::singleShot(0,
    // ...) still runs it before the user can interact with anything, just on
    // the first event-loop iteration instead of blocking first paint.
    QTimer::singleShot(0, this, &LibraryBrowserWidget::refresh);
}

void LibraryBrowserWidget::refresh()
{
    reloadCategoryNames();
    m_model->setTracks(TrackRepository::searchTracks(m_searchEdit->text()));
}

void LibraryBrowserWidget::reloadCategoryNames()
{
    QHash<qint64, QString> names;
    const auto categories = RotationCategoryRepository::allCategories();
    for (const auto& category : categories)
        names.insert(category.id, category.name);
    m_model->setCategoryNames(names);
}

void LibraryBrowserWidget::onSearchTextChanged(const QString&)
{
    m_searchDebounceTimer->start(); // restarts if already running -- only fires once typing pauses
}

void LibraryBrowserWidget::onSearchDebounceTimeout()
{
    m_model->setTracks(TrackRepository::searchTracks(m_searchEdit->text()));
}

void LibraryBrowserWidget::onImportClicked()
{
    if (auto* importer = ImportDialog::runImport(this))
        connect(importer, &ImportDialog::importFinished, this, &LibraryBrowserWidget::refresh);
}

void LibraryBrowserWidget::onEditRequested(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    const auto& track = m_model->trackAt(index.row());
    TrackEditDialog dialog(track, this);
    if (dialog.exec() == QDialog::Accepted) {
        TrackRepository::updateEditableFields(
            track.id, dialog.title(), dialog.artist(), dialog.album(), dialog.genre(), dialog.categoryId());
        refresh();
    }
}

void LibraryBrowserWidget::onAddToQueueClicked()
{
    const auto selectedRows = m_view->selectionModel()->selectedRows();
    for (const QModelIndex& index : selectedRows)
        PlaylistRepository::appendToQueue(m_model->trackAt(index.row()).id, QStringLiteral("manual"));
    if (!selectedRows.isEmpty())
        emit trackQueued();
}

void LibraryBrowserWidget::onAddToPlaylistClicked()
{
    const auto selectedRows = m_view->selectionModel()->selectedRows();
    if (selectedRows.isEmpty())
        return;

    const auto playlists = PlaylistRepository::listPlaylists();
    if (playlists.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Add to Playlist"),
            QStringLiteral("No playlists exist yet — create one in the Playlists panel first."));
        return;
    }

    QStringList names;
    for (const auto& playlist : playlists)
        names.append(playlist.name);

    bool ok = false;
    const QString chosen
        = QInputDialog::getItem(this, QStringLiteral("Add to Playlist"), QStringLiteral("Playlist:"), names, 0, false, &ok);
    if (!ok)
        return;

    const int chosenIndex = names.indexOf(chosen);
    if (chosenIndex < 0)
        return;
    const qint64 playlistId = playlists.at(chosenIndex).id;

    for (const QModelIndex& index : selectedRows)
        PlaylistRepository::addTrackToPlaylist(playlistId, m_model->trackAt(index.row()).id, QStringLiteral("manual"));
    emit trackAddedToPlaylist();
}

void LibraryBrowserWidget::onRatingEdited(qint64 trackId, int newRating)
{
    TrackRepository::setRating(trackId, newRating);
}

void LibraryBrowserWidget::onHeaderContextMenuRequested(const QPoint& pos)
{
    QMenu menu(this);
    for (int column = 0; column < TrackTableModel::ColumnCount; ++column) {
        // TitleColumn stays permanently visible — it's the pill anchor, not
        // optional clutter like the rest.
        if (column == TrackTableModel::TitleColumn)
            continue;
        auto* action = menu.addAction(m_model->headerData(column, Qt::Horizontal, Qt::DisplayRole).toString());
        action->setCheckable(true);
        action->setChecked(!m_view->isColumnHidden(column));
        connect(action, &QAction::toggled, this, [this, column](bool checked) {
            m_view->setColumnHidden(column, !checked);

            QSettings settings;
            QStringList hidden = settings.value(kSettingsHiddenColumns).toStringList();
            const QString key = QString::number(column);
            hidden.removeAll(key);
            if (!checked)
                hidden.append(key);
            settings.setValue(kSettingsHiddenColumns, hidden);
        });
    }
    menu.exec(m_view->horizontalHeader()->mapToGlobal(pos));
}

void LibraryBrowserWidget::restoreColumnVisibility()
{
    QSettings settings;
    const QStringList hidden = settings.value(kSettingsHiddenColumns).toStringList();
    for (const QString& entry : hidden) {
        bool ok = false;
        const int column = entry.toInt(&ok);
        if (ok && column != TrackTableModel::TitleColumn && column < TrackTableModel::ColumnCount)
            m_view->setColumnHidden(column, true);
    }
}

} // namespace radio::ui
