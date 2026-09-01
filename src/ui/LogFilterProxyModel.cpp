#include "LogFilterProxyModel.h"
#include "LogTableModel.h"

namespace radio::ui {

using radio::core::LogEntry;

LogFilterProxyModel::LogFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
}

void LogFilterProxyModel::setMinLevel(radio::core::LogLevel level)
{
    beginFilterChange();
    m_minLevel = level;
    endFilterChange();
}

void LogFilterProxyModel::setCategoryFilter(const QString& category)
{
    beginFilterChange();
    m_category = category;
    endFilterChange();
}

void LogFilterProxyModel::setTextFilter(const QString& text)
{
    beginFilterChange();
    m_text = text;
    endFilterChange();
}

bool LogFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const
{
    const QModelIndex idx = sourceModel()->index(sourceRow, LogTableModel::MessageColumn, sourceParent);
    const LogEntry entry = idx.data(Qt::UserRole).value<LogEntry>();

    if (entry.level < m_minLevel)
        return false;

    if (!m_category.isEmpty() && entry.category != m_category)
        return false;

    if (!m_text.isEmpty() && !entry.message.contains(m_text, Qt::CaseInsensitive)
        && !entry.category.contains(m_text, Qt::CaseInsensitive))
        return false;

    return true;
}

} // namespace radio::ui
