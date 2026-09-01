#pragma once

#include <QColor>
#include <QString>

namespace radio::ui::station_settings {

// Shared between StationSettingsDialog (writes these) and
// RadioStatisticsPanel (reads these every 1s poll tick) — kept in one place
// so the two never drift apart on a key name or default value.

const QString kRadioName = QStringLiteral("station/radioName");
const QString kAutoSizeFonts = QStringLiteral("station/autoSizeFonts");
const QString kSmallFontPt = QStringLiteral("station/smallFontPt");
const QString kClockFontPt = QStringLiteral("station/clockFontPt");
const QString kOnAirColor = QStringLiteral("station/onAirColor");
const QString kOnAirOffColor = QStringLiteral("station/onAirOffColor");
const QString kClockColor = QStringLiteral("station/clockColor");

const QString kDefaultRadioName = QStringLiteral("RadioStation");
constexpr bool kDefaultAutoSizeFonts = true;
constexpr int kDefaultSmallFontPt = 14;
constexpr int kDefaultClockFontPt = 32;
const QColor kDefaultOnAirColor(0xff, 0x3b, 0x3b);
const QColor kDefaultOnAirOffColor(0x5c, 0x1c, 0x1c);
const QColor kDefaultClockColor(0xff, 0x3b, 0x3b);

// Failover (Phase 2: "survive unattended") -- shared between
// StationSettingsDialog's Failover tab (writes these) and MainWindow
// (reads them for the Panic button and the dead-air auto-failover
// response). The emergency track is stored as a plain absolute file path,
// same "just point at a file" convention cart_clips already uses -- no DB
// dependency.
const QString kEmergencyCartPath = QStringLiteral("failover/emergencyCartPath");
const QString kDeadAirThresholdSeconds = QStringLiteral("failover/deadAirThresholdSeconds");
const QString kAutoFailoverOnDeadAir = QStringLiteral("failover/autoFailoverOnDeadAir");

constexpr double kDefaultDeadAirThresholdSeconds = 8.0;
constexpr bool kDefaultAutoFailoverOnDeadAir = false;

// Deck character-LCD display tuning -- shared between StationSettingsDialog's
// "Deck Display" tab (writes these) and DotMatrixDisplay
// (reloadMetricsFromSettings() reads them). The two values are the pixels
// per grid dot for each line (the whole readout is a fixed 5x7 dot grid).
// The key names keep "FontPx" for settings-file back-compat only.
const QString kDeckLine1FontPx = QStringLiteral("deck/display/line1FontPx");
const QString kDeckLine2FontPx = QStringLiteral("deck/display/line2FontPx");

// When true, a deck's display glows yellow while the deck is cued (loaded
// but not yet played) and red while it's playing, so it's obvious at a
// glance which deck is on air. Read by DeckWidget.
const QString kDeckDisplayColourByState = QStringLiteral("deck/display/colourByState");

constexpr int kDefaultDeckLine1FontPx = 5;
constexpr int kDefaultDeckLine2FontPx = 3;
constexpr bool kDefaultDeckDisplayColourByState = true;

} // namespace radio::ui::station_settings
