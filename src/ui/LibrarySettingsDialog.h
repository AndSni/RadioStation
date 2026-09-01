#pragma once

#include <QDialog>

class QLineEdit;

namespace radio::ui {

// Non-modal (show(), not exec()), same apply-immediately shape as
// StationSettingsDialog. Exposes the single "main library folder" path that
// MainWindow's "Refresh Library" action rescans (see ImportDialog::
// runRescan) -- distinct from the per-import "Import Folder..." picker,
// which can point anywhere and never marks files missing.
class LibrarySettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit LibrarySettingsDialog(QWidget* parent = nullptr);

private slots:
    void onBrowseClicked();

private:
    void loadSettings();

    QLineEdit* m_rootPathEdit;
};

} // namespace radio::ui
