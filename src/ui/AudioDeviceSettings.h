#pragma once

#include <QString>

namespace radio::ui::audio_device_settings {

// Shared between AudioDeviceSettingsDialog (writes these) and MainWindow
// (reads the air device once at startup, before AudioEngine::start(), and
// the monitor device once right after) — kept in one place so the two
// never drift apart on a key name. Values are hex-encoded AudioDeviceInfo::id
// bytes (QByteArray::toHex()/fromHex()); absent/empty means "system
// default" for the air device, "none" for the monitor device.

const QString kAirDeviceId = QStringLiteral("audioDevice/airId");
const QString kMonitorDeviceId = QStringLiteral("audioDevice/monitorId");
// Unlike air/monitor, this is a PREFERENCE only -- selecting a mic device
// here never itself starts capture (see AudioDeviceSettingsDialog's own
// comment); MixerPanelWidget's mic strip enable toggle is what actually
// calls AudioEngine::startMicInput() using whatever this key holds.
const QString kMicDeviceId = QStringLiteral("audioDevice/micId");

} // namespace radio::ui::audio_device_settings
