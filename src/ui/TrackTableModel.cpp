#include "TrackTableModel.h"
#include "AudioObjectMime.h"
#include "TrackLabel.h"

#include <QMimeData>

namespace radio::ui {

using radio::db::TrackRecord;

TrackTableModel::TrackTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
}

int TrackTableModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_tracks.size();
}

int TrackTableModel::columnCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TrackTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return {};

    const TrackRecord& track = m_tracks.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TitleColumn:
            return formatTrackLabel(track.artist, track.title);
        case ArtistColumn:
            return track.artist;
        case AlbumColumn:
            return track.album;
        case GenreColumn:
            return track.genre;
        case DurationColumn:
            return formatDuration(track.durationMs);
        case CategoryColumn:
            return track.categoryId >= 0 ? m_categoryNames.value(track.categoryId, QStringLiteral("?"))
                                          : QStringLiteral("(uncategorized)");
        case RatingColumn:
            return track.rating;
        case PlayCountColumn:
            return track.playCount;
        default:
            return {};
        }
    }

    if (role == Qt::UserRole)
        return QVariant::fromValue(track);

    return {};
}

bool TrackTableModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_tracks.size())
        return false;
    if (index.column() != RatingColumn || role != Qt::EditRole)
        return false;

    // In-memory only, matching this model's existing pure view-model design
    // (see the class's own header) — persistence happens in the widget
    // layer, via RatingDelegate's ratingEdited signal.
    m_tracks[index.row()].rating = value.toInt();
    emit dataChanged(index, index, { Qt::DisplayRole, Qt::EditRole });
    return true;
}

Qt::ItemFlags TrackTableModel::flags(const QModelIndex& index) const
{
    Qt::ItemFlags result = QAbstractTableModel::flags(index);
    if (index.isValid() && index.column() == RatingColumn)
        result |= Qt::ItemIsEditable;
    if (index.isValid())
        result |= Qt::ItemIsDragEnabled;
    return result;
}

QMimeData* TrackTableModel::mimeData(const QModelIndexList& indexes) const
{
    QMimeData* mime = QAbstractTableModel::mimeData(indexes);
    if (indexes.isEmpty() || !mime)
        return mime;

    AudioObjectDragPayload payload;
    payload.trackId = m_tracks.at(indexes.first().row()).id;
    payload.fromQueue = false;
    AudioObjectMime::encode(mime, payload);

    return mime;
}

QVariant TrackTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case TitleColumn:
        return QStringLiteral("Title");
    case ArtistColumn:
        return QStringLiteral("Artist");
    case AlbumColumn:
        return QStringLiteral("Album");
    case GenreColumn:
        return QStringLiteral("Genre");
    case DurationColumn:
        return QStringLiteral("Duration");
    case CategoryColumn:
        return QStringLiteral("Category");
    case RatingColumn:
        return QStringLiteral("Rating");
    case PlayCountColumn:
        return QStringLiteral("Play Count");
    default:
        return {};
    }
}

void TrackTableModel::setTracks(QVector<TrackRecord> tracks)
{
    beginResetModel();
    m_tracks = std::move(tracks);
    endResetModel();
}

void TrackTableModel::setCategoryNames(QHash<qint64, QString> namesById)
{
    m_categoryNames = std::move(namesById);
    if (!m_tracks.isEmpty())
        emit dataChanged(index(0, CategoryColumn), index(m_tracks.size() - 1, CategoryColumn));
}

QString TrackTableModel::formatDuration(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("--:--");
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2").arg(totalSeconds / 60).arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace radio::ui
