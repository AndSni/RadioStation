#include "AutoDjPanelWidget.h"
#include "ScheduleBlockEditorDialog.h"

#include "db/ClockRepository.h"
#include "db/PlaylistRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "scheduler/BlockTimeResolver.h"
#include "scheduler/ScheduleBlockValidity.h"

#include <QBrush>
#include <QColor>
#include <QDateTime>
#include <QFont>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPushButton>
#include <QStyledItemDelegate>
#include <QTime>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace radio::ui {

using radio::db::ClockRepository;
using radio::db::PlaylistRepository;
using radio::db::ScheduleBlockRecord;
using radio::db::ScheduleBlockRepository;
using radio::db::SmartPlaylistRepository;
using radio::scheduler::BlockTimeResolver;
using radio::scheduler::ScheduleBlockHealth;
using radio::scheduler::ScheduleBlockValidity;

namespace {
constexpr int kBlockIdRole = Qt::UserRole;
const char* const kDayLabels[7] = { "M", "T", "W", "T", "F", "S", "Su" };

// Green rounded-rect outline around whichever block the injected predicate
// says is currently active — painted on top of the item's normal rendering
// (QStyledItemDelegate::paint()), not replacing it, so text/selection still
// look exactly as they otherwise would. No Q_OBJECT: this only overrides a
// plain virtual, no new signals/slots/properties needed.
class ActiveBlockDelegate : public QStyledItemDelegate {
public:
    ActiveBlockDelegate(QObject* parent, std::function<bool(const QModelIndex&)> isActivePredicate)
        : QStyledItemDelegate(parent)
        , m_isActivePredicate(std::move(isActivePredicate))
    {
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyledItemDelegate::paint(painter, option, index);
        if (!m_isActivePredicate || !m_isActivePredicate(index))
            return;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        QPen pen(QColor(QStringLiteral("#4ade80")));
        pen.setWidth(2);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        painter->drawRoundedRect(option.rect.adjusted(1, 1, -2, -2), 4, 4);
        painter->restore();
    }

private:
    std::function<bool(const QModelIndex&)> m_isActivePredicate;
};

QString formatBlockSummary(const ScheduleBlockRecord& block)
{
    QString days;
    for (int i = 0; i < 7; ++i) {
        if (block.daysMask & (1 << i))
            days += QString::fromLatin1(kDayLabels[i]);
    }
    if (days.isEmpty())
        days = QStringLiteral("(no days)");

    const QString timeRange = (block.startMinute == 0 && block.endMinute == 1440)
        ? QStringLiteral("All Day")
        : QStringLiteral("%1-%2")
              .arg(QTime(0, 0).addSecs(block.startMinute * 60).toString(QStringLiteral("HH:mm")),
                  QTime(0, 0).addSecs((block.endMinute % 1440) * 60).toString(QStringLiteral("HH:mm")));

    QString summary;
    if (block.clockId >= 0) {
        const QString clockName = ClockRepository::clockById(block.clockId).name;
        summary = QStringLiteral("%1 — %2 %3 · Clock: %4").arg(block.name, days, timeRange, clockName);
    } else {
        const QString queueSummary = block.queueMode == QStringLiteral("remaining")
            ? QStringLiteral("fill remaining")
            : (block.queueSize > 0 ? QString::number(block.queueSize) : QStringLiteral("default"));
        summary = QStringLiteral("%1 — %2 %3 · Queue: %4").arg(block.name, days, timeRange, queueSummary);
    }

    const QString warning = ScheduleBlockValidity::describe(ScheduleBlockValidity::evaluate(block));
    if (!warning.isEmpty())
        summary += QStringLiteral(" ⚠ %1").arg(warning);

    return summary;
}
}

AutoDjPanelWidget::AutoDjPanelWidget(QWidget* parent)
    : QWidget(parent)
{
    auto* layout = new QVBoxLayout(this);

    // The clock/remaining-time display that used to live here moved to
    // RadioStatisticsPanel; the ON/OFF toggle that used to share this row
    // moved to MainWindow's toolbar (see this class's own doc comment).
    auto* titleLabel = new QLabel(QStringLiteral("Auto DJ"), this);
    titleLabel->setObjectName(QStringLiteral("autoDjTitleLabel"));
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 4);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    layout->addWidget(titleLabel);

    m_blockList = new QListWidget(this);
    m_blockList->setObjectName(QStringLiteral("autoDjBlockList"));
    m_blockList->setDragDropMode(QAbstractItemView::InternalMove);
    m_blockList->setDefaultDropAction(Qt::MoveAction);
    m_blockList->setItemDelegate(
        new ActiveBlockDelegate(m_blockList, [this](const QModelIndex& index) {
            return m_activeBlockId >= 0 && index.data(kBlockIdRole).toLongLong() == m_activeBlockId;
        }));
    layout->addWidget(m_blockList, 1);

    auto* blockButtonRow = new QHBoxLayout();
    m_addBlockButton = new QPushButton(QStringLiteral("Add Block"), this);
    m_editBlockButton = new QPushButton(QStringLiteral("Edit Block"), this);
    m_deleteBlockButton = new QPushButton(QStringLiteral("Delete Block"), this);
    blockButtonRow->addWidget(m_addBlockButton);
    blockButtonRow->addWidget(m_editBlockButton);
    blockButtonRow->addWidget(m_deleteBlockButton);
    layout->addLayout(blockButtonRow);

    connect(m_blockList, &QListWidget::itemDoubleClicked, this, &AutoDjPanelWidget::onEditBlockClicked);
    connect(m_blockList->model(), &QAbstractItemModel::rowsMoved, this, &AutoDjPanelWidget::onBlockRowsMoved);
    connect(m_addBlockButton, &QPushButton::clicked, this, &AutoDjPanelWidget::onAddBlockClicked);
    connect(m_editBlockButton, &QPushButton::clicked, this, &AutoDjPanelWidget::onEditBlockClicked);
    connect(m_deleteBlockButton, &QPushButton::clicked, this, &AutoDjPanelWidget::onDeleteBlockClicked);

    m_scheduleTimer = new QTimer(this);
    m_scheduleTimer->setInterval(1000);
    connect(m_scheduleTimer, &QTimer::timeout, this, &AutoDjPanelWidget::onScheduleTick);
    m_scheduleTimer->start();

    refreshBlockList();
    onScheduleTick(); // reflect the clock/active-block highlight immediately, not after the first 1s tick
}

qint64 AutoDjPanelWidget::selectedBlockId() const
{
    QListWidgetItem* item = m_blockList->currentItem();
    return item ? item->data(kBlockIdRole).toLongLong() : -1;
}

void AutoDjPanelWidget::refreshBlockList()
{
    const qint64 previouslySelected = selectedBlockId();

    m_blockList->blockSignals(true);
    m_blockList->clear();

    const auto blocks = ScheduleBlockRepository::allBlocks();
    QListWidgetItem* itemToReselect = nullptr;
    for (const auto& block : blocks) {
        auto* item = new QListWidgetItem(formatBlockSummary(block), m_blockList);
        item->setData(kBlockIdRole, block.id);
        if (ScheduleBlockValidity::evaluate(block) != ScheduleBlockHealth::Ok)
            item->setForeground(QBrush(QColor(0xdc, 0x26, 0x26))); // matches the streaming-error red elsewhere in this UI
        if (block.id == previouslySelected)
            itemToReselect = item;
    }

    m_blockList->blockSignals(false);
    if (itemToReselect)
        m_blockList->setCurrentItem(itemToReselect);
}

void AutoDjPanelWidget::onAddBlockClicked()
{
    if (PlaylistRepository::listPlaylists().isEmpty() && SmartPlaylistRepository::allSmartPlaylists().isEmpty()
        && ClockRepository::allClocks().isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Add Schedule Block"),
            QStringLiteral("No playlists, smart playlists, or clocks exist yet — create one in the Playlists, Smart "
                           "Playlists, or Clocks panel first."));
        return;
    }

    openBlockEditor(ScheduleBlockRecord());
}

void AutoDjPanelWidget::onEditBlockClicked()
{
    const qint64 blockId = selectedBlockId();
    if (blockId < 0)
        return;

    const auto blocks = ScheduleBlockRepository::allBlocks();
    const auto it = std::find_if(blocks.begin(), blocks.end(), [blockId](const auto& block) { return block.id == blockId; });
    if (it == blocks.end())
        return;

    openBlockEditor(*it);
}

void AutoDjPanelWidget::openBlockEditor(const ScheduleBlockRecord& block)
{
    if (m_openEditor) {
        m_openEditor->raise();
        m_openEditor->activateWindow();
        return;
    }

    // Heap-allocated + non-modal (show(), not exec()) so a live schedule
    // tweak doesn't freeze the whole console the way a stack-local exec()'d
    // dialog does -- matches the non-modal pattern already used correctly
    // elsewhere in this app (CrossfadeSettingsDialog, StreamingSettingsDialog,
    // etc). dialog->result() is read from inside the accepted() lambda,
    // while the dialog is still alive.
    auto* dialog = new ScheduleBlockEditorDialog(block, this);
    m_openEditor = dialog;
    connect(dialog, &ScheduleBlockEditorDialog::accepted, this, [this, dialog, isNew = block.id < 0]() {
        if (isNew)
            ScheduleBlockRepository::createBlock(dialog->result());
        else
            ScheduleBlockRepository::updateBlock(dialog->result());
        refreshBlockList();
    });
    // WA_DeleteOnClose (if set) only covers the window-manager close path --
    // QDialog::done(), which accept()/reject() both funnel through, calls
    // hide() rather than close(), so finished() is what reliably cleans up
    // every path (accept, cancel, and the X button alike).
    connect(dialog, &ScheduleBlockEditorDialog::finished, dialog, &QObject::deleteLater);
    dialog->show();
}

void AutoDjPanelWidget::onDeleteBlockClicked()
{
    const qint64 blockId = selectedBlockId();
    if (blockId < 0)
        return;
    if (QMessageBox::question(this, QStringLiteral("Delete Schedule Block"), QStringLiteral("Delete this schedule block?"))
        != QMessageBox::Yes)
        return;
    ScheduleBlockRepository::deleteBlock(blockId);
    refreshBlockList();
}

void AutoDjPanelWidget::onBlockRowsMoved()
{
    QVector<qint64> order;
    order.reserve(m_blockList->count());
    for (int i = 0; i < m_blockList->count(); ++i)
        order.append(m_blockList->item(i)->data(kBlockIdRole).toLongLong());
    ScheduleBlockRepository::setBlockOrder(order);
}

void AutoDjPanelWidget::onScheduleTick()
{
    const QDateTime now = QDateTime::currentDateTime();
    const auto blocks = ScheduleBlockRepository::allBlocks();
    const qint64 activeId = BlockTimeResolver::resolveActiveBlockId(blocks, now);

    if (activeId != m_activeBlockId) {
        m_activeBlockId = activeId;
        m_blockList->viewport()->update(); // force ActiveBlockDelegate to repaint with the new highlight
    }
}

} // namespace radio::ui
