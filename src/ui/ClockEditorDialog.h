#pragma once

#include <QDialog>
#include <QPointer>

class QLineEdit;
class QPushButton;
class QListWidget;

namespace radio::db {
struct ClockElementRecord;
}

namespace radio::ui {

class ClockElementEditorDialog;

// Non-modal editor for one clock's name and its ordered element list —
// add/edit/delete/reorder (drag, same InternalMove pattern as
// AutoDjPanelWidget's block list/QueueWidget), each CRUD action applied
// immediately against ClockRepository rather than deferred to an
// accept/cancel result, same shape as AutoDjPanelWidget's own block-editor
// flow. Opened from ClocksPanelWidget for an already-created clock (a new
// clock's id/name are created there first, via a single name prompt, before
// this dialog ever opens).
class ClockEditorDialog : public QDialog {
    Q_OBJECT

public:
    explicit ClockEditorDialog(qint64 clockId, QWidget* parent = nullptr);

signals:
    // The clock was renamed or its element list changed — ClocksPanelWidget
    // refreshes its own summary row for this clock.
    void clockChanged();

private slots:
    void onNameEditingFinished();
    void onAddElementClicked();
    void onEditElementClicked();
    void onDeleteElementClicked();
    void onElementRowsMoved();

private:
    void refreshElementList();
    qint64 selectedElementId() const;
    // Shared by onAddElementClicked()/onEditElementClicked() -- element.id <
    // 0 means "add"; only one editor at a time (mirrors
    // AutoDjPanelWidget::openBlockEditor()).
    void openElementEditor(const radio::db::ClockElementRecord& element);

    qint64 m_clockId;
    QLineEdit* m_nameEdit = nullptr;
    QListWidget* m_elementList = nullptr;
    QPushButton* m_addElementButton = nullptr;
    QPushButton* m_editElementButton = nullptr;
    QPushButton* m_deleteElementButton = nullptr;

    QPointer<ClockElementEditorDialog> m_openEditor;
};

} // namespace radio::ui
