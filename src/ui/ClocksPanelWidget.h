#pragma once

#include <QPointer>
#include <QWidget>

class QListWidget;
class QPushButton;

namespace radio::ui {

class ClockEditorDialog;

// Dock panel listing every reusable clock (hour template) — add/edit/delete.
// "Add" prompts for a name, creates the clock, then opens its element editor
// immediately since a nameless clock with zero elements isn't useful on its
// own. "Edit" reopens that same element editor for an existing clock. No
// drag-reorder here (unlike AutoDjPanelWidget's block list) — clocks have no
// relative priority among themselves; they're just named templates a
// schedule block points at (see ScheduleBlockEditorDialog's Target combo).
// Modeled directly on AutoDjPanelWidget's list-panel shape.
class ClocksPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit ClocksPanelWidget(QWidget* parent = nullptr);

signals:
    // Emitted at the end of every refresh() -- lets AutoDjPanelWidget (whose
    // block-row summaries show an assigned clock's name) re-render whenever
    // clock data actually changed, without this panel needing to know
    // anything about AutoDjPanelWidget itself.
    void clocksChanged();

public slots:
    void refresh();

private slots:
    void onAddClockClicked();
    void onEditClockClicked();
    void onDeleteClockClicked();

private:
    qint64 selectedClockId() const;
    void openClockEditor(qint64 clockId);

    QListWidget* m_clockList = nullptr;
    QPushButton* m_addClockButton = nullptr;
    QPushButton* m_editClockButton = nullptr;
    QPushButton* m_deleteClockButton = nullptr;

    QPointer<ClockEditorDialog> m_openEditor;
};

} // namespace radio::ui
