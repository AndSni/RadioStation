#include "ClocksPanelWidget.h"
#include "ClockEditorDialog.h"

#include "db/ClockRepository.h"

#include <QFont>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::ClockRepository;

namespace {
constexpr int kClockIdRole = Qt::UserRole;

QString formatClockSummary(const radio::db::ClockRecord& clock)
{
    const int elementCount = ClockRepository::elementsForClock(clock.id).size();
    return QStringLiteral("%1 (%2 element%3)").arg(clock.name).arg(elementCount).arg(elementCount == 1 ? QString() : QStringLiteral("s"));
}
}

ClocksPanelWidget::ClocksPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    auto* titleLabel = new QLabel(QStringLiteral("Clocks"), this);
    titleLabel->setObjectName(QStringLiteral("clocksTitleLabel"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    m_clockList = new QListWidget(this);
    m_clockList->setObjectName(QStringLiteral("clocksList"));
    layout->addWidget(m_clockList, 1);

    auto* buttonRow = new QHBoxLayout();
    m_addClockButton = new QPushButton(QStringLiteral("Add Clock"), this);
    m_editClockButton = new QPushButton(QStringLiteral("Edit Clock"), this);
    m_deleteClockButton = new QPushButton(QStringLiteral("Delete Clock"), this);
    buttonRow->addWidget(m_addClockButton);
    buttonRow->addWidget(m_editClockButton);
    buttonRow->addWidget(m_deleteClockButton);
    layout->addLayout(buttonRow);

    connect(m_clockList, &QListWidget::itemDoubleClicked, this, &ClocksPanelWidget::onEditClockClicked);
    connect(m_addClockButton, &QPushButton::clicked, this, &ClocksPanelWidget::onAddClockClicked);
    connect(m_editClockButton, &QPushButton::clicked, this, &ClocksPanelWidget::onEditClockClicked);
    connect(m_deleteClockButton, &QPushButton::clicked, this, &ClocksPanelWidget::onDeleteClockClicked);

    refresh();
}

qint64 ClocksPanelWidget::selectedClockId() const
{
    QListWidgetItem* item = m_clockList->currentItem();
    return item ? item->data(kClockIdRole).toLongLong() : -1;
}

void ClocksPanelWidget::refresh()
{
    const qint64 previouslySelected = selectedClockId();

    m_clockList->blockSignals(true);
    m_clockList->clear();

    QListWidgetItem* itemToReselect = nullptr;
    for (const auto& clock : ClockRepository::allClocks()) {
        auto* item = new QListWidgetItem(formatClockSummary(clock), m_clockList);
        item->setData(kClockIdRole, clock.id);
        if (clock.id == previouslySelected)
            itemToReselect = item;
    }

    m_clockList->blockSignals(false);
    if (itemToReselect)
        m_clockList->setCurrentItem(itemToReselect);

    emit clocksChanged();
}

void ClocksPanelWidget::onAddClockClicked()
{
    bool ok = false;
    const QString name
        = QInputDialog::getText(this, QStringLiteral("New Clock"), QStringLiteral("Clock name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    const qint64 clockId = ClockRepository::createClock(name.trimmed());
    if (clockId < 0) {
        QMessageBox::warning(this, QStringLiteral("New Clock"), QStringLiteral("Failed to create the clock."));
        return;
    }
    refresh();
    openClockEditor(clockId);
}

void ClocksPanelWidget::onEditClockClicked()
{
    const qint64 clockId = selectedClockId();
    if (clockId < 0)
        return;
    openClockEditor(clockId);
}

void ClocksPanelWidget::openClockEditor(qint64 clockId)
{
    if (m_openEditor) {
        m_openEditor->raise();
        m_openEditor->activateWindow();
        return;
    }

    auto* dialog = new ClockEditorDialog(clockId, this);
    m_openEditor = dialog;
    connect(dialog, &ClockEditorDialog::clockChanged, this, &ClocksPanelWidget::refresh);
    connect(dialog, &ClockEditorDialog::finished, dialog, &QObject::deleteLater);
    dialog->show();
}

void ClocksPanelWidget::onDeleteClockClicked()
{
    const qint64 clockId = selectedClockId();
    if (clockId < 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete Clock"),
            QStringLiteral("Delete this clock? Any schedule block using it will fall back to its legacy playlist "
                           "target (or stop producing content if it has none)."))
        != QMessageBox::Yes)
        return;
    ClockRepository::deleteClock(clockId);
    refresh();
}

} // namespace radio::ui
