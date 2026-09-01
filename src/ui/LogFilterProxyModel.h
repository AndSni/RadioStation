#pragma once

#include "core/Logging.h"

#include <QSortFilterProxyModel>

namespace radio::ui {

class LogFilterProxyModel : public QSortFilterProxyModel {
    Q_OBJECT

public:
    explicit LogFilterProxyModel(QObject* parent = nullptr);

    void setMinLevel(radio::core::LogLevel level);
    void setCategoryFilter(const QString& category); // empty string == any category
    void setTextFilter(const QString& text);

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const override;

private:
    radio::core::LogLevel m_minLevel = radio::core::LogLevel::Debug;
    QString m_category;
    QString m_text;
};

} // namespace radio::ui
