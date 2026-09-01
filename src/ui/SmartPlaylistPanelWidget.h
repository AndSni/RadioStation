#pragma once

#include <QWidget>

class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QSpinBox;
class QDoubleSpinBox;
class QTableView;
class QComboBox;
class QPushButton;

namespace radio::ui {

class TrackTableModel;
class SmartPlaylistFilterProxyModel;

// Live Type/Genre/Era/Energy/BPM query builder over the library, backed by
// SmartPlaylistFilterProxyModel (which shares its predicate with
// TrackRepository::candidatesForSmartPlaylist(), so what's previewed here is
// exactly what AutoDjEngine would pick from). Two ways to act on the
// results: "Save as Smart Playlist..." persists the filter itself for
// AutoDjEngine to re-evaluate live via a schedule block (see
// SmartPlaylistRepository), while "Create Playlist from these results"
// snapshots the current matches into an ordinary static playlist (see
// PlaylistRepository::createPlaylistFromTracks) -- a one-shot copy, not a
// standing query. Hosted in its own dockable panel in MainWindow, same
// pattern as every other panel.
class SmartPlaylistPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit SmartPlaylistPanelWidget(QWidget* parent = nullptr);

signals:
    void playlistCreated(); // "Create Playlist from these results" succeeded -- refresh PlaylistEditorWidget

public slots:
    void refresh(); // reload tracks, category checklist, and the saved-smart-playlist combo

private slots:
    void onFilterControlChanged();
    void onCategoryItemChanged(QListWidgetItem* item);
    void onSaveAsSmartPlaylistClicked();
    void onDeleteSmartPlaylistClicked();
    void onCreatePlaylistFromResultsClicked();
    void onSavedSmartPlaylistChanged(int index);

private:
    void applyFiltersFromUi();
    void loadFilterIntoUi(const QString& filterJson);
    void resetFiltersToDefaults();
    void reloadCategoryList();
    void reloadSavedSmartPlaylistCombo();

    TrackTableModel* m_sourceModel;
    SmartPlaylistFilterProxyModel* m_proxyModel;

    QLineEdit* m_genreEdit;
    QListWidget* m_categoryList;

    QCheckBox* m_yearEnableCheck;
    QSpinBox* m_yearMinSpin;
    QSpinBox* m_yearMaxSpin;

    QCheckBox* m_energyEnableCheck;
    QDoubleSpinBox* m_energyMinSpin;
    QDoubleSpinBox* m_energyMaxSpin;

    QCheckBox* m_bpmEnableCheck;
    QSpinBox* m_bpmMinSpin;
    QSpinBox* m_bpmMaxSpin;

    QTableView* m_view;
    QComboBox* m_savedSmartPlaylistCombo;
    QPushButton* m_saveAsSmartPlaylistButton;
    QPushButton* m_deleteSmartPlaylistButton;
    QPushButton* m_createPlaylistButton;

    // -1 == the current filter is unsaved/new; >=0 == editing this saved
    // smart playlist ("Save as Smart Playlist..." updates it in place
    // instead of creating a new one).
    qint64 m_loadedSmartPlaylistId = -1;

    // Guards against onFilterControlChanged()/onCategoryItemChanged()
    // re-applying (and clobbering m_loadedSmartPlaylistId) while
    // loadFilterIntoUi()/resetFiltersToDefaults() are themselves just
    // programmatically setting control values, not reacting to the user.
    bool m_loadingFilter = false;
};

} // namespace radio::ui
