#pragma once

#include <QDialog>

class QComboBox;
class QPushButton;
class QLabel;
class QShowEvent;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

// Non-modal (show(), not exec()), same shape as StreamingSettingsDialog —
// two playback device pickers (Air: the main mix output; Monitor: an
// OPTIONAL second device mirroring the exact same final mix, e.g.
// headphones vs. speakers — not a separate cue/PFL bus, which this app has
// no concept of at all) plus a Refresh button, since devices can change
// while the dialog's open. Persists the chosen device ids via QSettings
// (hex-encoded) and applies each change immediately via AudioEngine.
//
// The third, Microphone Input, picker is different: it's a PREFERENCE only
// (see AudioDeviceSettings::kMicDeviceId) — changing it live-switches the
// mic ONLY if it's already running (started elsewhere, via MixerPanelWidget's
// mic strip enable toggle); it never itself starts capture. A hot mic
// appearing just because the user browsed a device list would be
// surprising in a way an output device switch isn't.
class AudioDeviceSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit AudioDeviceSettingsDialog(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

protected:
    // First show only (see m_combosPopulated) -- enumerating playback/
    // capture devices is two full ma_context_init/enumerate/uninit cycles
    // (see populateCombos()), real work nobody should pay for at
    // construction (MainWindow's constructor, before first paint) just to
    // fill a combo box nobody has opened yet.
    void showEvent(QShowEvent* event) override;

private slots:
    void onRefreshClicked();
    void onAirDeviceChanged(int index);
    void onMonitorDeviceChanged(int index);
    void onMicDeviceChanged(int index);

private:
    void populateCombos();

    radio::audio::AudioEngine* m_engine;
    QComboBox* m_airDeviceCombo;
    QComboBox* m_monitorDeviceCombo;
    QComboBox* m_micDeviceCombo;
    QPushButton* m_refreshButton;
    QLabel* m_infoLabel;
    bool m_combosPopulated = false;
};

} // namespace radio::ui
