#pragma once

#include <QWidget>

class QLineEdit;
class QTableView;
class QPushButton;
class QPoint;
class QTimer;

namespace radio::ui {

class TrackTableModel;

class LibraryBrowserWidget : public QWidget {
    Q_OBJECT

public:
    explicit LibraryBrowserWidget(QWidget* parent = nullptr);

signals:
    void trackQueued();
    void trackAddedToPlaylist();

public slots:
    void refresh();

private slots:
    void onSearchTextChanged(const QString& text);
    void onSearchDebounceTimeout();
    void onImportClicked();
    void onEditRequested(const QModelIndex& index);
    void onAddToQueueClicked();
    void onAddToPlaylistClicked();
    void onRatingEdited(qint64 trackId, int newRating);
    void onHeaderContextMenuRequested(const QPoint& pos);

private:
    void reloadCategoryNames();
    void restoreColumnVisibility();

    TrackTableModel* m_model;
    QLineEdit* m_searchEdit;
    QTimer* m_searchDebounceTimer;
    QTableView* m_view;
    QPushButton* m_importButton;
    QPushButton* m_addToQueueButton;
    QPushButton* m_addToPlaylistButton;
};

} // namespace radio::ui
