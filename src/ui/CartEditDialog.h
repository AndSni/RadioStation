#pragma once

#include "db/CartRepository.h"

#include <QColor>
#include <QDialog>
#include <QVector>

class QLineEdit;
class QPushButton;
class QLabel;
class QCheckBox;
class QDialogButtonBox;

namespace radio::ui {

// Non-modal (show(), not exec()) editor for one cart clip's label/color/
// hotkey — used for both "assign to an empty slot" and "edit an existing
// clip". Deliberately consolidates what used to be a QInputDialog ->
// QColorDialog -> QInputDialog -> QMessageBox chain of application-modal
// dialogs (CartGridWidget::onAssignRequested/onEditRequested) into one
// non-modal form: that chain blocked every OTHER cart's
// Qt::ApplicationShortcut hotkey (see CartButton.cpp) for its entire
// duration, including a live retrigger of an already-loaded cart, for as
// long as the operator spent labeling/coloring/keying the one being edited.
// Hotkey conflicts are validated inline (see onHotkeyEdited()) rather than
// the old warn-then-clear-then-restart-the-whole-dialog flow.
class CartEditDialog : public QDialog {
    Q_OBJECT

public:
    // existingClips is used only for inline hotkey-conflict validation
    // (excluding clip.id itself) — passed in rather than queried live so
    // this dialog has no direct CartRepository dependency. clip.id < 0
    // means "new clip for an empty slot"; row/col/filePath/durationMs are
    // carried through to result() unchanged (this dialog has no widgets for
    // them).
    CartEditDialog(
        const radio::db::CartClipRecord& clip, QVector<radio::db::CartClipRecord> existingClips, QWidget* parent = nullptr);

    radio::db::CartClipRecord result() const;

private slots:
    void onPickColorClicked();
    void onHotkeyEdited(const QString& text);

private:
    void updateColorButtonStyle();

    radio::db::CartClipRecord m_clip;
    QVector<radio::db::CartClipRecord> m_existingClips;
    QColor m_color;

    QLineEdit* m_labelEdit;
    QPushButton* m_colorButton;
    QLineEdit* m_hotkeyEdit;
    QLabel* m_hotkeyConflictLabel;
    QCheckBox* m_inBetweenOnlyCheck;
    QDialogButtonBox* m_buttonBox;
};

} // namespace radio::ui
