#include "LibrarySettingsDialog.h"
#include "LibrarySettings.h"

#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>

namespace radio::ui {

using namespace radio::ui::library_settings;

LibrarySettingsDialog::LibrarySettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Library Settings"));

    m_rootPathEdit = new QLineEdit(this);
    m_rootPathEdit->setReadOnly(true);
    m_rootPathEdit->setPlaceholderText(QStringLiteral("Not set -- Refresh Library will prompt for one"));

    auto* browseButton = new QPushButton(QStringLiteral("Browse..."), this);
    connect(browseButton, &QPushButton::clicked, this, &LibrarySettingsDialog::onBrowseClicked);

    auto* pathRow = new QHBoxLayout;
    pathRow->addWidget(m_rootPathEdit, 1);
    pathRow->addWidget(browseButton);

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Main Library Folder"), pathRow);
    setLayout(form);

    loadSettings();
}

void LibrarySettingsDialog::loadSettings()
{
    m_rootPathEdit->setText(QSettings().value(kRootPath).toString());
}

void LibrarySettingsDialog::onBrowseClicked()
{
    const QString startDir = m_rootPathEdit->text().isEmpty() ? QString() : m_rootPathEdit->text();
    const QString folder = QFileDialog::getExistingDirectory(this, QStringLiteral("Main Library Folder"), startDir);
    if (folder.isEmpty())
        return;

    m_rootPathEdit->setText(folder);
    QSettings().setValue(kRootPath, folder);
}

} // namespace radio::ui
