#pragma once

#include <QDialog>

class QCheckBox;
class QDoubleSpinBox;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

// Non-modal (show(), not exec()) — same pattern as CrossfadeSettingsDialog:
// each change applies to the live MixEngine immediately (via AudioEngine's
// passthroughs) and is persisted via QSettings, no separate "apply" step.
// One group per band (Low/Mid/High — see MixEngine::setBandThresholdDb()'s
// doc comment for the 3-band LR4 split and why the High band's tuning
// doubles as the de-esser, not a separate control).
class MasterProcessingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit MasterProcessingSettingsDialog(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

private:
    void loadSettings();
    QWidget* buildBandGroup(int band, const QString& label, const QString& tooltip);

    radio::audio::AudioEngine* m_engine;
    QCheckBox* m_enabledCheck = nullptr;
    QDoubleSpinBox* m_thresholdSpins[3] = {};
    QDoubleSpinBox* m_ratioSpins[3] = {};
};

} // namespace radio::ui
