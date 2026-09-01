#pragma once

#include <QDockWidget>

class QComboBox;
class QLineEdit;
class QCheckBox;
class QTableView;

namespace radio::ui {

class LogTableModel;
class LogFilterProxyModel;

// Permanent, toggleable, UI-integrated view over radio::core::Logger.
// Always constructed (never compiled out); visibility is a runtime toggle
// owned by MainWindow.
class DebugConsoleDockWidget : public QDockWidget {
    Q_OBJECT

public:
    explicit DebugConsoleDockWidget(QWidget* parent = nullptr);

private slots:
    void onLevelChanged(int index);
    void onCategoryChanged(int index);
    void onSearchTextChanged(const QString& text);
    void onClearClicked();
    void onExportClicked();
    void onTestLogClicked();
    void onRowsInserted();

private:
    LogTableModel* m_model;
    LogFilterProxyModel* m_proxy;
    QTableView* m_view;
    QComboBox* m_levelCombo;
    QComboBox* m_categoryCombo;
    QLineEdit* m_searchEdit;
    QCheckBox* m_autoscrollCheck;
};

} // namespace radio::ui
