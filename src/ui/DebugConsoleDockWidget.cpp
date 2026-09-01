#include "DebugConsoleDockWidget.h"
#include "LogFilterProxyModel.h"
#include "LogTableModel.h"

#include "core/Logging.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QTableView>
#include <QTextStream>
#include <QVBoxLayout>
#include <QWidget>

namespace radio::ui {

using radio::core::LogEntry;
using radio::core::LogLevel;
using radio::core::Logger;

namespace {
const QStringList kKnownCategories = {
    QStringLiteral("app"),
    QStringLiteral("ui"),
    QStringLiteral("audio.pipeline"),
    QStringLiteral("audio.crossfade"),
    QStringLiteral("audio.cartautomation"),
    QStringLiteral("library.scan"),
    QStringLiteral("library.playlist"),
    QStringLiteral("streaming.encoder"),
    QStringLiteral("scheduler.autodj"),
};
}

DebugConsoleDockWidget::DebugConsoleDockWidget(QWidget* parent)
    : QDockWidget(QStringLiteral("Debug Console"), parent)
{
    setObjectName(QStringLiteral("DebugConsoleDockWidget"));

    m_model = new LogTableModel(this);
    m_proxy = new LogFilterProxyModel(this);
    m_proxy->setSourceModel(m_model);

    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(4, 4, 4, 4);

    auto* toolbar = new QHBoxLayout();

    m_levelCombo = new QComboBox(container);
    m_levelCombo->addItem(QStringLiteral("Debug"), static_cast<int>(LogLevel::Debug));
    m_levelCombo->addItem(QStringLiteral("Info"), static_cast<int>(LogLevel::Info));
    m_levelCombo->addItem(QStringLiteral("Warning"), static_cast<int>(LogLevel::Warning));
    m_levelCombo->addItem(QStringLiteral("Error"), static_cast<int>(LogLevel::Error));
    toolbar->addWidget(m_levelCombo);

    m_categoryCombo = new QComboBox(container);
    m_categoryCombo->addItem(QStringLiteral("All categories"), QString());
    for (const QString& category : kKnownCategories)
        m_categoryCombo->addItem(category, category);
    toolbar->addWidget(m_categoryCombo);

    m_searchEdit = new QLineEdit(container);
    m_searchEdit->setPlaceholderText(QStringLiteral("Search..."));
    toolbar->addWidget(m_searchEdit, 1);

    m_autoscrollCheck = new QCheckBox(QStringLiteral("Autoscroll"), container);
    m_autoscrollCheck->setChecked(true);
    toolbar->addWidget(m_autoscrollCheck);

    auto* testLogButton = new QPushButton(QStringLiteral("Test Log"), container);
    toolbar->addWidget(testLogButton);

    auto* clearButton = new QPushButton(QStringLiteral("Clear"), container);
    toolbar->addWidget(clearButton);

    auto* exportButton = new QPushButton(QStringLiteral("Export..."), container);
    toolbar->addWidget(exportButton);

    layout->addLayout(toolbar);

    m_view = new QTableView(container);
    m_view->setModel(m_proxy);
    m_view->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_view->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_view->horizontalHeader()->setStretchLastSection(true);
    m_view->verticalHeader()->setVisible(false);
    layout->addWidget(m_view, 1);

    container->setLayout(layout);
    setWidget(container);

    connect(m_levelCombo, &QComboBox::currentIndexChanged, this, &DebugConsoleDockWidget::onLevelChanged);
    connect(m_categoryCombo, &QComboBox::currentIndexChanged, this, &DebugConsoleDockWidget::onCategoryChanged);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &DebugConsoleDockWidget::onSearchTextChanged);
    connect(clearButton, &QPushButton::clicked, this, &DebugConsoleDockWidget::onClearClicked);
    connect(exportButton, &QPushButton::clicked, this, &DebugConsoleDockWidget::onExportClicked);
    connect(testLogButton, &QPushButton::clicked, this, &DebugConsoleDockWidget::onTestLogClicked);

    connect(&Logger::instance(), &Logger::entryAdded, m_model, &LogTableModel::appendEntry);
    connect(&Logger::instance(), &Logger::cleared, m_model, &LogTableModel::clearEntries);
    connect(m_model, &LogTableModel::rowsInserted, this, &DebugConsoleDockWidget::onRowsInserted);

    onLevelChanged(m_levelCombo->currentIndex());
    onCategoryChanged(m_categoryCombo->currentIndex());
}

void DebugConsoleDockWidget::onLevelChanged(int index)
{
    m_proxy->setMinLevel(static_cast<LogLevel>(m_levelCombo->itemData(index).toInt()));
}

void DebugConsoleDockWidget::onCategoryChanged(int index)
{
    m_proxy->setCategoryFilter(m_categoryCombo->itemData(index).toString());
}

void DebugConsoleDockWidget::onSearchTextChanged(const QString& text)
{
    m_proxy->setTextFilter(text);
}

void DebugConsoleDockWidget::onClearClicked()
{
    Logger::instance().clear();
}

void DebugConsoleDockWidget::onExportClicked()
{
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Log"),
        QStringLiteral("radiostation-log.txt"), QStringLiteral("Text Files (*.txt)"));
    if (path.isEmpty())
        return;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return;

    QTextStream out(&file);
    for (int row = 0; row < m_proxy->rowCount(); ++row) {
        const LogEntry entry = m_proxy->index(row, LogTableModel::MessageColumn).data(Qt::UserRole).value<LogEntry>();
        out << entry.timestamp.toString(Qt::ISODateWithMs) << " ["
            << radio::core::logLevelToString(entry.level) << "] " << entry.category << ": " << entry.message
            << '\n';
    }
}

void DebugConsoleDockWidget::onTestLogClicked()
{
    RS_LOG_DEBUG("ui", QStringLiteral("Test log entry (debug)"));
    RS_LOG_INFO("ui", QStringLiteral("Test log entry (info)"));
    RS_LOG_WARN("ui", QStringLiteral("Test log entry (warning)"));
    RS_LOG_ERROR("ui", QStringLiteral("Test log entry (error)"));
    for (const QString& category : kKnownCategories)
        RS_LOG_INFO(category, QStringLiteral("Test log entry for category '%1'").arg(category));
}

void DebugConsoleDockWidget::onRowsInserted()
{
    if (m_autoscrollCheck->isChecked())
        m_view->scrollToBottom();
}

} // namespace radio::ui
