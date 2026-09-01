#pragma once

#include <QJsonObject>
#include <QSet>
#include <QSortFilterProxyModel>
#include <QString>

#include <optional>

namespace radio::ui {

// Wraps a TrackTableModel (same relationship LogFilterProxyModel has to
// LogTableModel) with a live Type/Genre/Era/Energy/BPM filter. Reuses
// SmartPlaylistFilter::matchesSmartPlaylistFilter() -- the same predicate
// TrackRepository::candidatesForSmartPlaylist() applies at the DB layer --
// so a saved smart playlist's preview here and its actual AutoDjEngine
// candidate pool can never drift apart. toFilterJson()/setFromFilterJson()
// share that same JSON shape, so a filter built here round-trips straight
// into SmartPlaylistRepository::create()/update().
class SmartPlaylistFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit SmartPlaylistFilterProxyModel(QObject* parent = nullptr);

    void setGenreFilter(const QString& genre); // empty == no constraint
    void setCategoryIds(const QSet<qint64>& categoryIds); // empty == no constraint
    void setYearRange(std::optional<int> min, std::optional<int> max);
    void setEnergyRange(std::optional<double> min, std::optional<double> max);
    void setBpmRange(std::optional<double> min, std::optional<double> max);

    QString toFilterJson() const;
    void setFromFilterJson(const QString& json);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    QJsonObject m_filter;
};

} // namespace radio::ui
