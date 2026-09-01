#pragma once

#include "db/TrackRecord.h"

#include <QDialog>

class QLineEdit;
class QPushButton;
class QTimeEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;

namespace radio::ui {

// Modal editor for one schedule block's full field set (name, days, time
// range, playlist, track-selection logic) — mirrors TrackEditDialog's
// pattern: caller reads result() after exec() == Accepted, then calls
// ScheduleBlockRepository::createBlock()/updateBlock() itself. Passing a
// block with id == -1 means "new block"; otherwise its id (and any field
// this dialog has no widget for, e.g. position/enabled) is preserved in
// result() so the caller can updateBlock() directly.
class ScheduleBlockEditorDialog : public QDialog {
    Q_OBJECT

public:
    ScheduleBlockEditorDialog(const radio::db::ScheduleBlockRecord& block, QWidget* parent = nullptr);

    radio::db::ScheduleBlockRecord result() const;

private slots:
    void onAllDayToggled(bool checked);
    void onCartModeChanged();
    void onPickCartColorClicked();
    void onQueueModeChanged();
    void onTargetTypeChanged();

private:
    void updateCartColorButtonStyle();

    radio::db::ScheduleBlockRecord m_original;
    QLineEdit* m_nameEdit;
    QPushButton* m_dayButtons[7]; // index 0 = Monday .. 6 = Sunday, matching days_mask bit order
    QTimeEdit* m_startTimeEdit;
    QTimeEdit* m_endTimeEdit;
    QCheckBox* m_allDayCheck;
    QComboBox* m_targetTypeCombo; // "Static Playlist" | "Smart Playlist" | "Clock" -- which of the next three combos is live
    QComboBox* m_playlistCombo;
    QComboBox* m_smartPlaylistCombo;
    QComboBox* m_clockCombo;
    QComboBox* m_selectionLogicCombo;
    QComboBox* m_queueModeCombo;
    QSpinBox* m_queueSizeSpin;
    QSpinBox* m_cartFrequencySpin;
    QComboBox* m_cartModeCombo;
    QPushButton* m_cartColorButton;
    QSpinBox* m_cartOffsetSpin;
    QComboBox* m_cartPlaybackTypeCombo;
    QString m_cartColor; // not directly editable as text — set via QColorDialog, shown as the button's swatch
};

} // namespace radio::ui
