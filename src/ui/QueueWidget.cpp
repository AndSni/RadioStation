#include "QueueWidget.h"
#include "AudioPillDelegate.h"
#include "QueueListWidget.h"
#include "TrackLabel.h"

#include "db/ClockRepository.h"
#include "db/PlaylistRepository.h"
#include "scheduler/AutoDjEngine.h"

#include <QPushButton>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::ClockRepository;
using radio::db::PlaylistRepository;
using radio::scheduler::AutoDjEngine;

namespace {
const QString kClockSourcePrefix = QStringLiteral("clock:");

// item.source is "clock:<elementId>" for a track ClockEngine queued from a
// wheel slot (see QueueItemRecord::source's doc comment) -- resolves that
// back to a human label so the queue shows which slot a track came from,
// same way it's just visible text elsewhere in this app, no interaction.
QString clockSourceSuffix(const QString& source)
{
    if (!source.startsWith(kClockSourcePrefix))
        return QString();
    bool ok = false;
    const qint64 elementId = source.mid(kClockSourcePrefix.size()).toLongLong(&ok);
    if (!ok)
        return QString();
    const auto element = ClockRepository::elementById(elementId);
    if (element.id < 0)
        return QString();
    return QStringLiteral(" [%1]").arg(element.label.isEmpty() ? QStringLiteral("clock") : element.label);
}
}

QueueWidget::QueueWidget(AutoDjEngine* autoDj, QWidget* parent)
    : QWidget(parent)
{
    m_list = new QueueListWidget(this);
    // DragDrop (not InternalMove) so an item can also be dragged OUT to a
    // Deck — see QueueListWidget's own doc comment for how self-reordering
    // still works under this mode. setDefaultDropAction(MoveAction) is what
    // makes a self-drop resolve to a reorder rather than Qt guessing Copy.
    m_list->setDragDropMode(QAbstractItemView::DragDrop);
    m_list->setDefaultDropAction(Qt::MoveAction);
    m_list->setItemDelegate(new AudioPillDelegate(m_list)); // no colour provider -> every row is the neutral pill colour

    auto* removeButton = new QPushButton(QStringLiteral("Remove Selected"), this);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(m_list, 1);
    layout->addWidget(removeButton);
    setLayout(layout);

    connect(m_list->model(), &QAbstractItemModel::rowsMoved, this, &QueueWidget::onRowsMoved);
    connect(removeButton, &QPushButton::clicked, this, &QueueWidget::onRemoveClicked);
    connect(m_list, &QueueListWidget::audioObjectDropped, this, &QueueWidget::onAudioObjectDropped);

    if (autoDj)
        connect(autoDj, &AutoDjEngine::queueUpdated, this, &QueueWidget::refresh);

    refresh();
}

void QueueWidget::refresh()
{
    m_list->blockSignals(true);
    m_list->clear();

    const auto items = PlaylistRepository::queueItems();
    for (const auto& item : items) {
        const QString label = formatTrackLabel(item.artist, item.title) + clockSourceSuffix(item.source);
        auto* listItem = new QListWidgetItem(label, m_list);
        listItem->setData(QueueListWidget::kTrackIdRole, item.trackId);
        listItem->setData(QueueListWidget::kPlaylistItemIdRole, item.playlistItemId);
    }

    m_list->blockSignals(false);
}

void QueueWidget::onRowsMoved()
{
    QVector<qint64> order;
    order.reserve(m_list->count());
    for (int i = 0; i < m_list->count(); ++i)
        order.append(m_list->item(i)->data(QueueListWidget::kPlaylistItemIdRole).toLongLong());
    PlaylistRepository::setItemOrder(order);
}

void QueueWidget::onRemoveClicked()
{
    const auto selected = m_list->selectedItems();
    for (auto* item : selected)
        PlaylistRepository::removeItem(item->data(QueueListWidget::kPlaylistItemIdRole).toLongLong());
    refresh();
}

void QueueWidget::onAudioObjectDropped(const AudioObjectDragPayload& payload)
{
    // fromQueue shouldn't reach here at all — a self-drag is a reorder,
    // handled entirely inside QueueListWidget::dropEvent() without ever
    // emitting this signal — but guard rather than trust the source blindly.
    if (payload.fromQueue)
        return;
    PlaylistRepository::appendToQueue(payload.trackId, QStringLiteral("manual"));
    refresh();
}

} // namespace radio::ui
