#pragma once

#include "core/Logging.h"

#include <QAbstractTableModel>
#include <QVector>

namespace radio::ui {

class LogTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { TimestampColumn = 0, LevelColumn, CategoryColumn, ThreadColumn, MessageColumn, ColumnCount };

    explicit LogTableModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override;

public slots:
    void appendEntry(const radio::core::LogEntry& entry);
    void clearEntries();

private:
    static constexpr int kMaxRows = 10000;

    QVector<radio::core::LogEntry> m_entries;
};

} // namespace radio::ui
