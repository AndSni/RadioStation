#include "SmartPlaylistFilterProxyModel.h"
#include "TrackTableModel.h"

#include "db/SmartPlaylistFilter.h"
#include "db/TrackRecord.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace radio::ui {

using radio::db::TrackRecord;

SmartPlaylistFilterProxyModel::SmartPlaylistFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void SmartPlaylistFilterProxyModel::setGenreFilter(const QString& genre)
{
    beginFilterChange();
    if (genre.isEmpty())
        m_filter.remove(QStringLiteral("genre"));
    else
        m_filter[QStringLiteral("genre")] = genre;
    endFilterChange();
}

void SmartPlaylistFilterProxyModel::setCategoryIds(const QSet<qint64>& categoryIds)
{
    beginFilterChange();
    if (categoryIds.isEmpty()) {
        m_filter.remove(QStringLiteral("categoryIds"));
    } else {
        QJsonArray array;
        for (const qint64 id : categoryIds)
            array.append(id);
        m_filter[QStringLiteral("categoryIds")] = array;
    }
    endFilterChange();
}

void SmartPlaylistFilterProxyModel::setYearRange(std::optional<int> min, std::optional<int> max)
{
    beginFilterChange();
    if (min)
        m_filter[QStringLiteral("yearMin")] = *min;
    else
        m_filter.remove(QStringLiteral("yearMin"));
    if (max)
        m_filter[QStringLiteral("yearMax")] = *max;
    else
        m_filter.remove(QStringLiteral("yearMax"));
    endFilterChange();
}

void SmartPlaylistFilterProxyModel::setEnergyRange(std::optional<double> min, std::optional<double> max)
{
    beginFilterChange();
    if (min)
        m_filter[QStringLiteral("energyMin")] = *min;
    else
        m_filter.remove(QStringLiteral("energyMin"));
    if (max)
        m_filter[QStringLiteral("energyMax")] = *max;
    else
        m_filter.remove(QStringLiteral("energyMax"));
    endFilterChange();
}

void SmartPlaylistFilterProxyModel::setBpmRange(std::optional<double> min, std::optional<double> max)
{
    beginFilterChange();
    if (min)
        m_filter[QStringLiteral("bpmMin")] = *min;
    else
        m_filter.remove(QStringLiteral("bpmMin"));
    if (max)
        m_filter[QStringLiteral("bpmMax")] = *max;
    else
        m_filter.remove(QStringLiteral("bpmMax"));
    endFilterChange();
}

QString SmartPlaylistFilterProxyModel::toFilterJson() const
{
    return QString::fromUtf8(QJsonDocument(m_filter).toJson(QJsonDocument::Compact));
}

void SmartPlaylistFilterProxyModel::setFromFilterJson(const QString& json)
{
    beginFilterChange();
    m_filter = QJsonDocument::fromJson(json.toUtf8()).object();
    endFilterChange();
}

bool SmartPlaylistFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, TrackTableModel::TitleColumn, sourceParent);
    const TrackRecord track = idx.data(Qt::UserRole).value<TrackRecord>();
    return radio::db::matchesSmartPlaylistFilter(track, m_filter);
}

} // namespace radio::ui
