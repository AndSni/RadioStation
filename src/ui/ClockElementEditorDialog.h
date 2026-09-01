#pragma once

#include "db/TrackRecord.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QCheckBox;
class QComboBox;
class QSpinBox;

namespace radio::ui {

// Modal editor for one clock element's full field set — mirrors
// ScheduleBlockEditorDialog's pattern: caller reads result() after
// exec()/accepted(), then calls ClockRepository::createElement()/
// updateElement() itself. Passing an element with id == -1 means "new
// element in clockId"; otherwise its id/clockId/position (fields this
// dialog has no widget for) are preserved in result().
//
// The Element Type combo alone decides which target the element resolves
// against (Music - Playlist vs. Music - Smart Playlist vs. the three cart
// kinds) — unlike ScheduleBlockEditorDialog there's no separate "Target
// Type" combo layered on top, since the element type already carries that
// distinction. Fields irrelevant to the selected type are disabled, not
// hidden, matching this codebase's established "keep the dialog's size
// stable" convention.
class ClockElementEditorDialog : public QDialog {
    Q_OBJECT

public:
    ClockElementEditorDialog(qint64 clockId, const radio::db::ClockElementRecord& element, QWidget* parent = nullptr);

    radio::db::ClockElementRecord result() const;

private slots:
    void onElementTypeChanged();
    void onFlowToggled(bool checked);
    void onPickCartColorClicked();

private:
    void updateCartColorButtonStyle();

    radio::db::ClockElementRecord m_original;
    qint64 m_clockId;

    QComboBox* m_elementTypeCombo;
    QLineEdit* m_labelEdit;

    QCheckBox* m_flowCheck;
    QSpinBox* m_minuteSpin;
    QComboBox* m_timingModeCombo;

    QSpinBox* m_itemCountSpin;
    QComboBox* m_playlistCombo;
    QComboBox* m_smartPlaylistCombo;
    QComboBox* m_selectionLogicCombo;

    QPushButton* m_cartColorButton;
    QComboBox* m_cartClipCombo;
    QString m_cartColor;
};

} // namespace radio::ui
