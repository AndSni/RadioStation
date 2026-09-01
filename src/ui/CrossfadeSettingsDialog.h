#pragma once

#include <QDialog>

class QDoubleSpinBox;

namespace radio::audio {
class CrossfadeController;
}

namespace radio::ui {

// Non-modal (show(), not exec()) so the user can keep an eye on the
// crossfader/deck state while adjusting timing. Unlike
// StreamingSettingsDialog's fields (only take effect on Connect), these
// have no "activation" step — each change applies to the live
// CrossfadeController immediately and is persisted via QSettings so it
// survives restarts.
class CrossfadeSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit CrossfadeSettingsDialog(radio::audio::CrossfadeController* controller, QWidget* parent = nullptr);

private slots:
    void onLeadSecondsChanged(double seconds);
    void onFadeSecondsChanged(double seconds);

private:
    void loadSettings();

    radio::audio::CrossfadeController* m_controller;
    QDoubleSpinBox* m_leadSecondsSpin;
    QDoubleSpinBox* m_fadeSecondsSpin;
};

} // namespace radio::ui
