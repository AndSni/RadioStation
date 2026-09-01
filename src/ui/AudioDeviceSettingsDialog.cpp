#include "AudioDeviceSettingsDialog.h"
#include "AudioDeviceSettings.h"

#include "audio/AudioEngine.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

namespace radio::ui {

using namespace radio::ui::audio_device_settings;
using radio::audio::AudioDeviceInfo;
using radio::audio::AudioEngine;

AudioDeviceSettingsDialog::AudioDeviceSettingsDialog(AudioEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Audio Device Settings"));

    m_airDeviceCombo = new QComboBox(this);
    m_airDeviceCombo->setToolTip(QStringLiteral("The main mix output — what's actually going out on air/stream."));
    m_monitorDeviceCombo = new QComboBox(this);
    m_monitorDeviceCombo->setToolTip(
        QStringLiteral("Optional second device mirroring the exact same mix — e.g. headphones vs. speakers. Not a separate cue/pre-listen bus."));
    m_micDeviceCombo = new QComboBox(this);
    m_micDeviceCombo->setToolTip(QStringLiteral(
        "Which device to use when the mic is enabled (see the Mixer panel's mic strip) — selecting a device here "
        "does not itself turn the mic on."));
    m_refreshButton = new QPushButton(QStringLiteral("Refresh Device List"), this);
    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
    m_infoLabel->setWordWrap(true);
    m_infoLabel->hide();

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Air Output"), m_airDeviceCombo);
    form->addRow(QStringLiteral("Monitor Output"), m_monitorDeviceCombo);
    form->addRow(QStringLiteral("Microphone Input"), m_micDeviceCombo);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_refreshButton);
    layout->addWidget(m_infoLabel);
    setLayout(layout);

    connect(m_refreshButton, &QPushButton::clicked, this, &AudioDeviceSettingsDialog::onRefreshClicked);

    // populateCombos() itself blocks signals while it mutates the combos
    // (see its own body), so connecting these before it's ever run is safe
    // — restoring the saved selection there still can't trigger a live
    // device switch. Actually populating is deferred to first show() —
    // see showEvent().
    connect(m_airDeviceCombo, &QComboBox::currentIndexChanged, this, &AudioDeviceSettingsDialog::onAirDeviceChanged);
    connect(
        m_monitorDeviceCombo, &QComboBox::currentIndexChanged, this, &AudioDeviceSettingsDialog::onMonitorDeviceChanged);
    connect(m_micDeviceCombo, &QComboBox::currentIndexChanged, this, &AudioDeviceSettingsDialog::onMicDeviceChanged);
}

void AudioDeviceSettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    if (!m_combosPopulated) {
        m_combosPopulated = true;
        populateCombos();
    }
}

void AudioDeviceSettingsDialog::onRefreshClicked()
{
    populateCombos();
}

void AudioDeviceSettingsDialog::populateCombos()
{
    const auto playbackDevices = AudioEngine::enumeratePlaybackDevices();
    const auto captureDevices = AudioEngine::enumerateCaptureDevices();

    // Preserve whatever's currently selected across a manual refresh (not
    // meaningful on the very first populate — count() == 0 then, so this
    // just yields empty QByteArrays, harmlessly falling through to the
    // QSettings-restore path below instead).
    const QByteArray currentAir = m_airDeviceCombo->currentData().toByteArray();
    const QByteArray currentMonitor = m_monitorDeviceCombo->currentData().toByteArray();
    const QByteArray currentMic = m_micDeviceCombo->currentData().toByteArray();
    const bool firstPopulate = m_airDeviceCombo->count() == 0;

    m_airDeviceCombo->blockSignals(true);
    m_monitorDeviceCombo->blockSignals(true);
    m_micDeviceCombo->blockSignals(true);

    m_airDeviceCombo->clear();
    m_monitorDeviceCombo->clear();
    m_micDeviceCombo->clear();
    m_monitorDeviceCombo->addItem(QStringLiteral("None"), QByteArray());

    for (const AudioDeviceInfo& device : playbackDevices) {
        const QString label = device.isDefault ? QStringLiteral("%1 (default)").arg(device.name) : device.name;
        m_airDeviceCombo->addItem(label, device.id);
        m_monitorDeviceCombo->addItem(label, device.id);
    }
    for (const AudioDeviceInfo& device : captureDevices) {
        const QString label = device.isDefault ? QStringLiteral("%1 (default)").arg(device.name) : device.name;
        m_micDeviceCombo->addItem(label, device.id);
    }

    QByteArray wantedAir = currentAir;
    QByteArray wantedMonitor = currentMonitor;
    QByteArray wantedMic = currentMic;
    if (firstPopulate) {
        QSettings settings;
        wantedAir = QByteArray::fromHex(settings.value(kAirDeviceId).toByteArray());
        wantedMonitor = QByteArray::fromHex(settings.value(kMonitorDeviceId).toByteArray());
        wantedMic = QByteArray::fromHex(settings.value(kMicDeviceId).toByteArray());
    }

    const int airIndex = m_airDeviceCombo->findData(wantedAir);
    m_airDeviceCombo->setCurrentIndex(airIndex >= 0 ? airIndex : 0); // falls back to whatever enumerated first (typically the default) if the saved/previous device is gone
    const int monitorIndex = m_monitorDeviceCombo->findData(wantedMonitor);
    m_monitorDeviceCombo->setCurrentIndex(monitorIndex >= 0 ? monitorIndex : 0); // 0 = "None"
    const int micIndex = m_micDeviceCombo->findData(wantedMic);
    m_micDeviceCombo->setCurrentIndex(micIndex >= 0 ? micIndex : 0);

    m_airDeviceCombo->blockSignals(false);
    m_micDeviceCombo->blockSignals(false);
    m_monitorDeviceCombo->blockSignals(false);
}

void AudioDeviceSettingsDialog::onAirDeviceChanged(int index)
{
    const QByteArray id = m_airDeviceCombo->itemData(index).toByteArray();
    QSettings().setValue(kAirDeviceId, id.toHex());

    if (m_engine->switchPlaybackDevice(id)) {
        m_infoLabel->hide();
    } else {
        m_infoLabel->setText(QStringLiteral("Failed to switch to that device — check it's still connected."));
        m_infoLabel->show();
    }
}

void AudioDeviceSettingsDialog::onMonitorDeviceChanged(int index)
{
    const QByteArray id = m_monitorDeviceCombo->itemData(index).toByteArray();
    QSettings().setValue(kMonitorDeviceId, id.toHex());

    if (index == 0) { // "None"
        m_engine->stopMonitorDevice();
        m_infoLabel->hide();
        return;
    }

    if (m_engine->startMonitorDevice(id)) {
        m_infoLabel->hide();
    } else {
        m_infoLabel->setText(QStringLiteral("Failed to start monitor device — check it's still connected."));
        m_infoLabel->show();
    }
}

void AudioDeviceSettingsDialog::onMicDeviceChanged(int index)
{
    const QByteArray id = m_micDeviceCombo->itemData(index).toByteArray();
    QSettings().setValue(kMicDeviceId, id.toHex());

    // A preference only (see this dialog's own doc comment) -- only
    // live-switches if the mic is already running; never starts it.
    if (!m_engine->isMicInputRunning())
        return;

    if (m_engine->startMicInput(id)) {
        m_infoLabel->hide();
    } else {
        m_infoLabel->setText(QStringLiteral("Failed to switch microphone device — check it's still connected."));
        m_infoLabel->show();
    }
}

} // namespace radio::ui
