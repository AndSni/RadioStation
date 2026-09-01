#pragma once

#include <QDialog>

class QLineEdit;
class QCheckBox;
class QSpinBox;
class QPushButton;
class QTableWidget;
class QTimer;
class QShowEvent;
class QHideEvent;
class QDoubleSpinBox;

namespace radio::ui {

// Non-modal (show(), not exec()), same shape as CrossfadeSettingsDialog —
// every field on the General tab applies (to QSettings only;
// RadioStatisticsPanel re-reads these on its own polling tick rather than
// needing a change signal) the instant it's changed, no OK/Cancel. Covers
// everything RadioStatisticsPanel renders: the station name, its font
// sizing (auto vs. manual), and its three glow/text colors (On Air, On
// Air's dim/off state, and the clock). The Hotkeys tab is read-only — a
// quick-reference list of every currently-assigned cart hotkey, since
// otherwise the only way to find out "what's bound to F3" is to open each
// cart's own edit dialog one at a time. The Failover tab configures the
// "safe source" both the Panic button and dead-air auto-failover (see
// AudioEngine::deadAirDetected()) trigger.
class StationSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit StationSettingsDialog(QWidget* parent = nullptr);

signals:
    // Fired whenever a General-tab field that RadioStatisticsPanel renders
    // (station name, font sizing, or any of the three colors) actually
    // changes -- lets that panel apply the new value immediately instead of
    // re-reading QSettings on every 1s clock tick regardless of whether
    // anything changed (see RadioStatisticsPanel::onTick()'s own comment).
    void appearanceSettingsChanged();

    // Fired when any Deck Display tab field changes -- lets every deck's
    // DotMatrixDisplay re-read its size metrics immediately (MainWindow
    // wires this to DotMatrixDisplay::reloadMetricsFromSettings()).
    void deckDisplaySettingsChanged();

protected:
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private slots:
    void onRadioNameChanged(const QString& text);
    void onAutoSizeFontsToggled(bool checked);
    void onSmallFontSizeChanged(int value);
    void onClockFontSizeChanged(int value);
    void onPickOnAirColorClicked();
    void onPickOnAirOffColorClicked();
    void onPickClockColorClicked();
    void onEmergencyCartBrowseClicked();
    void onEmergencyCartPathChanged(const QString& text);
    void onDeadAirThresholdChanged(double seconds);
    void onAutoFailoverToggled(bool checked);
    void onDeckDisplayMetricChanged();

private:
    void loadSettings();
    void updateColorButtonStyle(QPushButton* button, const QString& colorName);
    void pickColor(QPushButton* button, const QString& settingsKey, const QString& dialogTitle);
    void refreshHotkeyTable();

    QLineEdit* m_radioNameEdit;
    QCheckBox* m_autoSizeFontsCheck;
    QSpinBox* m_smallFontSizeSpin;
    QSpinBox* m_clockFontSizeSpin;
    QPushButton* m_onAirColorButton;
    QPushButton* m_onAirOffColorButton;
    QPushButton* m_clockColorButton;

    QLineEdit* m_emergencyCartPathEdit;
    QPushButton* m_emergencyCartBrowseButton;
    QDoubleSpinBox* m_deadAirThresholdSpin;
    QCheckBox* m_autoFailoverCheck;

    // Deck Display tab -- live-tunable dot-matrix line font sizes (see
    // DotMatrixDisplay).
    QSpinBox* m_deckLine1FontSpin;
    QSpinBox* m_deckLine2FontSpin;
    QCheckBox* m_deckColourByStateCheck;

    // No change signal exists for cart assignment (CartRepository is a
    // plain static DB wrapper, and CartGridWidget/CartEditDialog can be
    // edited independently of this dialog) -- polled on a timer, same
    // idiom already used throughout this app (CartGridWidget's own
    // playback-poll timer, RadioStatisticsPanel/AutoDjPanelWidget's 1s
    // schedule ticks) rather than wiring up a one-off signal for this alone.
    // Only runs while this dialog is actually visible.
    QTableWidget* m_hotkeyTable;
    QTimer* m_hotkeyRefreshTimer;
};

} // namespace radio::ui
