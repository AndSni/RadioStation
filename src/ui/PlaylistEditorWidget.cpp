#include "PlaylistEditorWidget.h"
#include "AudioPillDelegate.h"
#include "ImportDialog.h"
#include "PlaylistTrackListWidget.h"
#include "TrackLabel.h"

#include "db/PlaylistFolderRepository.h"
#include "db/PlaylistRepository.h"

#include <QHash>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::PlaylistFolderRepository;
using radio::db::PlaylistRepository;

PlaylistEditorWidget::PlaylistEditorWidget(QWidget* parent)
    : QWidget(parent)
{
    m_tree = new QTreeWidget(this);
    m_tree->setHeaderHidden(true);

    auto* treeButtonRow = new QHBoxLayout();
    m_newPlaylistButton = new QPushButton(QStringLiteral("New Playlist"), this);
    m_newFolderButton = new QPushButton(QStringLiteral("New Folder"), this);
    m_renameButton = new QPushButton(QStringLiteral("Rename"), this);
    m_deleteButton = new QPushButton(QStringLiteral("Delete"), this);
    treeButtonRow->addWidget(m_newPlaylistButton);
    treeButtonRow->addWidget(m_newFolderButton);
    treeButtonRow->addWidget(m_renameButton);
    treeButtonRow->addWidget(m_deleteButton);

    auto* leftContainer = new QWidget(this);
    auto* leftLayout = new QVBoxLayout(leftContainer);
    leftLayout->addLayout(treeButtonRow);
    leftLayout->addWidget(m_tree, 1);

    m_trackList = new PlaylistTrackListWidget(this);
    // Both a drag source (Playlist -> Deck, Playlist -> Queue) and a drop
    // target (Library -> Playlist) — no internal reorder involved, so
    // plain DragDrop (not the InternalMove-aware handling QueueListWidget
    // needs) is all this needs.
    m_trackList->setDragDropMode(QAbstractItemView::DragDrop);
    // Always neutral — playlist membership isn't queue membership, no color
    // to carry (see QueueColorRegistry's doc comment). Pill-shaped purely
    // for visual consistency with Library/Queue/Deck and to participate in
    // the same drag-and-drop system.
    m_trackList->setItemDelegate(new AudioPillDelegate(m_trackList));

    auto* trackButtonRow = new QHBoxLayout();
    m_importFolderButton = new QPushButton(QStringLiteral("Import Folder..."), this);
    m_removeTrackButton = new QPushButton(QStringLiteral("Remove from Playlist"), this);
    trackButtonRow->addWidget(m_importFolderButton);
    trackButtonRow->addWidget(m_removeTrackButton);

    auto* rightContainer = new QWidget(this);
    auto* rightLayout = new QVBoxLayout(rightContainer);
    rightLayout->addLayout(trackButtonRow);
    rightLayout->addWidget(m_trackList, 1);

    auto* mainLayout = new QHBoxLayout(this);
    mainLayout->addWidget(leftContainer, 1);
    mainLayout->addWidget(rightContainer, 1);
    setLayout(mainLayout);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this, &PlaylistEditorWidget::onTreeSelectionChanged);
    connect(m_newPlaylistButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onNewPlaylistClicked);
    connect(m_newFolderButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onNewFolderClicked);
    connect(m_renameButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onRenameClicked);
    connect(m_deleteButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onDeleteClicked);
    connect(m_importFolderButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onImportFolderClicked);
    connect(m_removeTrackButton, &QPushButton::clicked, this, &PlaylistEditorWidget::onRemoveTrackClicked);
    connect(m_trackList, &PlaylistTrackListWidget::audioObjectDropped, this, &PlaylistEditorWidget::onAudioObjectDropped);

    refresh();
}

void PlaylistEditorWidget::refresh()
{
    const qint64 previouslySelectedPlaylist = selectedPlaylistId();

    m_tree->clear();

    QHash<qint64, QTreeWidgetItem*> folderItems;
    const auto folders = PlaylistFolderRepository::allFolders();
    for (const auto& folder : folders) {
        auto* item = new QTreeWidgetItem(m_tree, { folder.name });
        item->setData(0, Qt::UserRole, static_cast<int>(ItemKind::Folder));
        item->setData(0, Qt::UserRole + 1, folder.id);
        QFont font = item->font(0);
        font.setBold(true);
        item->setFont(0, font);
        folderItems.insert(folder.id, item);
    }

    const auto playlists = PlaylistRepository::listPlaylists();
    QTreeWidgetItem* itemToReselect = nullptr;
    for (const auto& playlist : playlists) {
        const QString label = QStringLiteral("%1 (%2)").arg(playlist.name).arg(playlist.trackCount);
        QTreeWidgetItem* parent = folderItems.value(playlist.folderId, nullptr);
        auto* item = parent ? new QTreeWidgetItem(parent, { label }) : new QTreeWidgetItem(m_tree, { label });
        item->setData(0, Qt::UserRole, static_cast<int>(ItemKind::Playlist));
        item->setData(0, Qt::UserRole + 1, playlist.id);
        if (playlist.id == previouslySelectedPlaylist)
            itemToReselect = item;
    }

    m_tree->expandAll();
    if (itemToReselect)
        m_tree->setCurrentItem(itemToReselect);

    refreshTrackList();
    updateButtonStates();
}

void PlaylistEditorWidget::onTreeSelectionChanged()
{
    refreshTrackList();
    updateButtonStates();
}

void PlaylistEditorWidget::refreshTrackList()
{
    m_trackList->clear();
    const qint64 playlistId = selectedPlaylistId();
    if (playlistId < 0)
        return;

    const auto items = PlaylistRepository::itemsForPlaylist(playlistId);
    for (const auto& item : items) {
        const QString label = formatTrackLabel(item.artist, item.title);
        auto* listItem = new QListWidgetItem(label, m_trackList);
        listItem->setData(PlaylistTrackListWidget::kTrackIdRole, item.trackId);
        listItem->setData(PlaylistTrackListWidget::kPlaylistItemIdRole, item.playlistItemId);
    }
}

void PlaylistEditorWidget::updateButtonStates()
{
    const bool somethingSelected = selectedId() >= 0;
    const bool playlistSelected = selectedPlaylistId() >= 0;
    m_renameButton->setEnabled(somethingSelected);
    m_deleteButton->setEnabled(somethingSelected);
    m_importFolderButton->setEnabled(playlistSelected);
}

PlaylistEditorWidget::ItemKind PlaylistEditorWidget::selectedKind() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return static_cast<ItemKind>(item ? item->data(0, Qt::UserRole).toInt() : 0);
}

qint64 PlaylistEditorWidget::selectedId() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    return item ? item->data(0, Qt::UserRole + 1).toLongLong() : -1;
}

qint64 PlaylistEditorWidget::selectedPlaylistId() const
{
    QTreeWidgetItem* item = m_tree->currentItem();
    if (!item || static_cast<ItemKind>(item->data(0, Qt::UserRole).toInt()) != ItemKind::Playlist)
        return -1;
    return item->data(0, Qt::UserRole + 1).toLongLong();
}

void PlaylistEditorWidget::onNewPlaylistClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Playlist"), QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    // Joins the currently-selected folder, if any — otherwise lands at the top level.
    const qint64 folderId = (selectedId() >= 0 && selectedKind() == ItemKind::Folder) ? selectedId() : -1;
    PlaylistRepository::createPlaylist(name.trimmed(), folderId);
    refresh();
}

void PlaylistEditorWidget::onNewFolderClicked()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QStringLiteral("New Folder"), QStringLiteral("Name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    PlaylistFolderRepository::createFolder(name.trimmed());
    refresh();
}

void PlaylistEditorWidget::onRenameClicked()
{
    const qint64 id = selectedId();
    if (id < 0)
        return;

    QString promptDefault = m_tree->currentItem()->text(0);
    if (selectedKind() == ItemKind::Playlist) {
        // Strip the trailing " (N)" track-count suffix added in refresh().
        const int parenIndex = promptDefault.lastIndexOf(QStringLiteral(" ("));
        if (parenIndex > 0)
            promptDefault = promptDefault.left(parenIndex);
    }

    bool ok = false;
    const QString newName
        = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Name:"), QLineEdit::Normal, promptDefault, &ok);
    if (!ok || newName.trimmed().isEmpty())
        return;

    if (selectedKind() == ItemKind::Folder)
        PlaylistFolderRepository::renameFolder(id, newName.trimmed());
    else
        PlaylistRepository::renamePlaylist(id, newName.trimmed());
    refresh();
}

void PlaylistEditorWidget::onDeleteClicked()
{
    const qint64 id = selectedId();
    if (id < 0)
        return;

    if (selectedKind() == ItemKind::Folder) {
        if (QMessageBox::question(this, QStringLiteral("Delete Folder"),
                QStringLiteral("Delete this folder? Playlists inside it are kept, just ungrouped."))
            != QMessageBox::Yes)
            return;
        PlaylistFolderRepository::deleteFolder(id);
    } else {
        if (QMessageBox::question(
                this, QStringLiteral("Delete Playlist"), QStringLiteral("Delete this playlist? This cannot be undone."))
            != QMessageBox::Yes)
            return;
        PlaylistRepository::deletePlaylist(id);
    }
    refresh();
}

void PlaylistEditorWidget::onImportFolderClicked()
{
    const qint64 playlistId = selectedPlaylistId();
    if (playlistId < 0)
        return;

    if (auto* importer = ImportDialog::runImport(this)) {
        m_importTargetPlaylistId = playlistId;
        m_pendingImportTrackIds.clear();
        connect(importer, &ImportDialog::trackImported, this, &PlaylistEditorWidget::onTrackImported);
        connect(importer, &ImportDialog::importFinished, this, &PlaylistEditorWidget::onImportFinished);
    }
}

void PlaylistEditorWidget::onRemoveTrackClicked()
{
    const auto selected = m_trackList->selectedItems();
    for (auto* item : selected)
        PlaylistRepository::removeItem(item->data(PlaylistTrackListWidget::kPlaylistItemIdRole).toLongLong());
    refresh(); // also refreshes the tree's "(N)" track-count label
}

void PlaylistEditorWidget::onTrackImported(qint64 trackId)
{
    m_pendingImportTrackIds.append(trackId);
}

void PlaylistEditorWidget::onImportFinished()
{
    for (qint64 trackId : std::as_const(m_pendingImportTrackIds))
        PlaylistRepository::addTrackToPlaylist(m_importTargetPlaylistId, trackId, QStringLiteral("import"));
    m_pendingImportTrackIds.clear();
    m_importTargetPlaylistId = -1;
    refresh();
}

void PlaylistEditorWidget::onAudioObjectDropped(const AudioObjectDragPayload& payload)
{
    const qint64 playlistId = selectedPlaylistId();
    if (playlistId < 0)
        return; // nothing selected to add to
    PlaylistRepository::addTrackToPlaylist(playlistId, payload.trackId, QStringLiteral("manual"));
    refresh();
}

} // namespace radio::ui
