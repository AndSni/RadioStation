#pragma once

#include <QPointer>
#include <QWidget>

class QLabel;
class QPushButton;
class QListWidget;
class QTimer;

namespace radio::db {
struct ScheduleBlockRecord;
}

namespace radio::ui {

class ScheduleBlockEditorDialog;

// Home for playback-queue/rotation control: the schedule block list
// AutoDjEngine picks from (see BlockTimeResolver/ScheduleBlockRepository) —
// add/edit/delete/reorder (drag, same InternalMove pattern as QueueWidget;
// topmost = highest priority on overlap). The master Auto DJ on/off switch
// that used to live here moved to MainWindow's always-visible toolbar (see
// MainWindow::onAutoDjToggleClicked()) — a station-wide control like that
// has to stay reachable regardless of which docks are shown/hidden, the
// same reasoning Panic's toolbar placement already established; this panel
// no longer needs a CrossfadeController dependency at all as a result.
class AutoDjPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit AutoDjPanelWidget(QWidget* parent = nullptr);

public slots:
    // Re-renders the block list's summary text (e.g. after a clock this
    // panel doesn't own itself was renamed/edited elsewhere — see
    // ClocksPanelWidget::clocksChanged()). Selection-preserving, same as
    // every other refresh in this codebase.
    void refreshBlockList();

private slots:
    void onAddBlockClicked();
    void onEditBlockClicked();
    void onDeleteBlockClicked();
    void onBlockRowsMoved();
    void onScheduleTick();

private:
    qint64 selectedBlockId() const;
    // Shared by onAddBlockClicked()/onEditBlockClicked() -- newBlock.id < 0
    // means "add"; only one editor at a time (a second add/edit request,
    // including a stray double-click, while one is already open just
    // raises it instead of opening a duplicate).
    void openBlockEditor(const radio::db::ScheduleBlockRecord& block);

    QListWidget* m_blockList = nullptr;
    QPushButton* m_addBlockButton = nullptr;
    QPushButton* m_editBlockButton = nullptr;
    QPushButton* m_deleteBlockButton = nullptr;

    // Ticks once a second to re-resolve which block is currently active
    // (for the block list's highlight — see the delegate installed on
    // m_blockList in the .cpp; the clock/remaining-time display itself
    // moved to RadioStatisticsPanel, a separate independent poll) —
    // schedule blocks are day/time-of-day driven, so "active" can change
    // purely from time passing, with no other event to react to.
    QTimer* m_scheduleTimer = nullptr;
    qint64 m_activeBlockId = -1;

    QPointer<ScheduleBlockEditorDialog> m_openEditor;
};

} // namespace radio::ui
