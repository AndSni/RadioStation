#pragma once

#include <QWidget>

class QLabel;
class QTimer;
class QGraphicsDropShadowEffect;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

class OnAirLabel;

// At-a-glance "what's the station doing right now" strip: an animated
// ON AIR indicator (see OnAirLabel), the customizable station name, a live
// clock (custom LCD-style font, static red glow), and the active schedule
// block's name + time remaining. Font sizing and every color are
// user-configurable via StationSettingsDialog (see StationSettings.h for
// the shared settings keys/defaults) — checked every poll tick AND pushed
// immediately via StationSettingsDialog::appearanceSettingsChanged() (see
// MainWindow's connection), but only actually reapplied (setFont/
// setStyleSheet/drop-shadow color, real work) when a value has genuinely
// changed since last applied -- see refreshAppearanceIfChanged(). The tick-
// based check stays (cheap: a handful of QSettings reads + string/int
// compares) so a value changed by any means other than the dialog is still
// picked up within a second, without paying for a full font/color
// reapplication on every one of those ticks regardless of whether anything
// changed. Polls the same
// BlockTimeResolver/ScheduleBlockRepository data AutoDjPanelWidget already
// polls for its own, different purpose (the block-list highlight) —
// deliberately a second, independent 1s timer rather than a shared
// abstraction between the two widgets; each needs a different slice of the
// same cheap query. Only depends on AudioEngine (for on-air state, "is
// either deck playing") — no CrossfadeController needed.
class RadioStatisticsPanel : public QWidget {
    Q_OBJECT

public:
    explicit RadioStatisticsPanel(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

public slots:
    // Connected to StationSettingsDialog::appearanceSettingsChanged() (see
    // MainWindow) -- applies a change the instant it's made rather than
    // waiting for the next 1s tick. Safe/idempotent to call any time (e.g.
    // redundantly right before the next tick's own check) -- see
    // refreshAppearanceIfChanged()'s doc comment.
    void refreshAppearanceIfChanged();

protected:
    // Triggers an immediate font re-size when auto-sizing is on, so
    // resizing the panel doesn't wait for the next 1s poll tick — see
    // applyFonts() for the exact proportions/manual-override handling.
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void onTick();

private:
    void applyFonts();
    void applyColors();

    radio::audio::AudioEngine* m_engine;

    OnAirLabel* m_onAirLabel = nullptr;
    QLabel* m_radioNameLabel = nullptr;
    QLabel* m_clockLabel = nullptr;
    QLabel* m_blockNameLabel = nullptr;
    QLabel* m_blockRemainingLabel = nullptr;
    QGraphicsDropShadowEffect* m_clockGlow = nullptr;

    QTimer* m_timer = nullptr;

    // Last-applied values compared against on every refreshAppearanceIfChanged()
    // call -- see that method and this class's own doc comment. Sentinels
    // (empty string / false / -1) deliberately differ from every real
    // default in StationSettings.h, so the very first call always applies.
    QString m_lastRadioName;
    bool m_lastAutoSizeFonts = false;
    int m_lastSmallFontPt = -1;
    int m_lastClockFontPt = -1;
    QString m_lastOnAirColor;
    QString m_lastOnAirOffColor;
    QString m_lastClockColor;
};

} // namespace radio::ui
