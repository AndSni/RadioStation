#pragma once

#include "db/TrackRecord.h"

#include <QAbstractTableModel>
#include <QVector>

class QMimeData;

namespace radio::ui {

class TrackTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column {
        TitleColumn = 0,
        ArtistColumn,
        AlbumColumn,
        GenreColumn,
        DurationColumn,
        CategoryColumn,
        RatingColumn,
        PlayCountColumn,
        ColumnCount
    };

    explicit TrackTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool setData(const QModelIndex& index, const QVariant& value, int role) override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

    // Drag source only (Library never accepts drops) — carries an
    // AudioObjectMime payload (fromQueue = false) built from the first
    // selected row, alongside whatever Qt's own default handling adds.
    QMimeData* mimeData(const QModelIndexList& indexes) const override;

    void setTracks(QVector<radio::db::TrackRecord> tracks);
    void setCategoryNames(QHash<qint64, QString> namesById);

    const radio::db::TrackRecord& trackAt(int row) const { return m_tracks.at(row); }

private:
    static QString formatDuration(qint64 ms);

    QVector<radio::db::TrackRecord> m_tracks;
    QHash<qint64, QString> m_categoryNames;
};

} // namespace radio::ui
