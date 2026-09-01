#pragma once

#include "AudioObjectMime.h"

#include <QVector>
#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

namespace radio::ui {

class PlaylistTrackListWidget;

// Manages playlists and playlist folders (create/rename/delete), and the
// track membership of whichever playlist is selected. A track can belong to
// several playlists at once (unrelated to a track's single rotation
// category — see PlaylistRepository's own doc comment). Tracks arrive via
// import-folder (reuses ImportDialog), Library's "Add to Playlist..."
// button, or a Library/Playlist drag dropped onto m_trackList. m_trackList
// is also a drag source itself (Playlist -> Deck, Playlist -> Queue).
// Hosted in a dockable QDockWidget in MainWindow, following the same
// pattern as every other panel.
class PlaylistEditorWidget : public QWidget {
    Q_OBJECT

public:
    explicit PlaylistEditorWidget(QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onTreeSelectionChanged();
    void onNewPlaylistClicked();
    void onNewFolderClicked();
    void onRenameClicked();
    void onDeleteClicked();
    void onImportFolderClicked();
    void onRemoveTrackClicked();
    void onTrackImported(qint64 trackId);
    void onImportFinished();
    void onAudioObjectDropped(const AudioObjectDragPayload& payload);

private:
    enum class ItemKind { Folder, Playlist };

    void refreshTrackList();
    void updateButtonStates();
    ItemKind selectedKind() const;
    qint64 selectedId() const; // folder or playlist id of the current tree selection, -1 if none
    qint64 selectedPlaylistId() const; // -1 unless the selection is specifically a playlist

    QTreeWidget* m_tree;
    PlaylistTrackListWidget* m_trackList;
    QPushButton* m_newPlaylistButton;
    QPushButton* m_newFolderButton;
    QPushButton* m_renameButton;
    QPushButton* m_deleteButton;
    QPushButton* m_importFolderButton;
    QPushButton* m_removeTrackButton;

    // Collected via onTrackImported() while an ImportDialog is running,
    // added to the target playlist in one batch once onImportFinished()
    // fires — mirrors how ImportDialog itself streams discoveries then
    // reports completion.
    QVector<qint64> m_pendingImportTrackIds;
    qint64 m_importTargetPlaylistId = -1;
};

} // namespace radio::ui
