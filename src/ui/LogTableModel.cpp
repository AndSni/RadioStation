#include "LogTableModel.h"

#include <QColor>

namespace radio::ui {

using radio::core::LogEntry;
using radio::core::LogLevel;
using radio::core::logLevelToString;

LogTableModel::LogTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_entries.reserve(kMaxRows);
}

int LogTableModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return m_entries.size();
}

int LogTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant LogTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_entries.size())
        return {};

    const LogEntry& entry = m_entries.at(index.row());

    if (role == Qt::DisplayRole) {
        switch (index.column()) {
        case TimestampColumn:
            return entry.timestamp.toString(QStringLiteral("HH:mm:ss.zzz"));
        case LevelColumn:
            return logLevelToString(entry.level);
        case CategoryColumn:
            return entry.category;
        case ThreadColumn:
            return entry.threadName;
        case MessageColumn:
            return entry.message;
        default:
            return {};
        }
    }

    if (role == Qt::ForegroundRole && index.column() == LevelColumn) {
        switch (entry.level) {
        case LogLevel::Error:
            return QColor(Qt::red);
        case LogLevel::Warning:
            return QColor(0xd0, 0x90, 0x00);
        default:
            return {};
        }
    }

    if (role == Qt::UserRole)
        return QVariant::fromValue(entry);

    return {};
}

QVariant LogTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case TimestampColumn:
        return QStringLiteral("Time");
    case LevelColumn:
        return QStringLiteral("Level");
    case CategoryColumn:
        return QStringLiteral("Category");
    case ThreadColumn:
        return QStringLiteral("Thread");
    case MessageColumn:
        return QStringLiteral("Message");
    default:
        return {};
    }
}

void LogTableModel::appendEntry(const LogEntry& entry)
{
    const bool atCap = m_entries.size() >= kMaxRows;

    if (atCap) {
        beginRemoveRows(QModelIndex(), 0, 0);
        m_entries.removeFirst();
        endRemoveRows();
    }

    const int row = m_entries.size();
    beginInsertRows(QModelIndex(), row, row);
    m_entries.append(entry);
    endInsertRows();
}

void LogTableModel::clearEntries()
{
    if (m_entries.isEmpty())
        return;
    beginResetModel();
    m_entries.clear();
    endResetModel();
}

} // namespace radio::ui
