#pragma once

#include "audio/MixEngine.h" // RampCurve

#include <QWidget>

class QSlider;
class QLabel;
class QPushButton;
class QButtonGroup;
class QVariantAnimation;

namespace radio::audio {
class CrossfadeController;
}

namespace radio::ui {

class ConsoleButton;
class LedIndicator;

class CrossfaderWidget : public QWidget {
    Q_OBJECT

public:
    explicit CrossfaderWidget(radio::audio::CrossfadeController* controller, QWidget* parent = nullptr);

private slots:
    void onSliderChanged(int value);
    void onAutoAdvanceToggled(bool checked);
    void onAutoCrossfadeStarted(const QString& fromDeck, const QString& toDeck);
    void onAutoCrossfadeFinished(const QString& newActiveDeck);
    void onManualFadeAvailabilityChanged(bool available);
    void onFadeNowClicked();

private:
    // Moves the slider without emitting valueChanged (which would otherwise
    // call onSliderChanged -> CrossfadeController::setManualPosition() and
    // fight the real audio ramp already in progress — see m_sliderAnimation
    // below).
    void setSliderValueSilently(int value);

    // Lights the A/B deck LED for whichever is activeDeck, dims the other.
    // Called at construction and whenever a crossfade finishes (the moment
    // the active deck actually changes).
    void updateActiveDeckLeds(const QString& activeDeck);

    // "Fade Now" is enabled only when BOTH a fade is currently possible
    // (m_manualFadeAvailable, driven by CrossfadeController::
    // manualFadeAvailabilityChanged) AND one isn't already in progress
    // (m_crossfading, driven by autoCrossfadeStarted/Finished) — two
    // independent reasons it can be disabled, combined here.
    void updateFadeNowEnabled();

    radio::audio::CrossfadeController* m_controller;
    QSlider* m_slider; // a ConsoleFader, centre-detented
    QLabel* m_deckALabel; // plain "A" silkscreen text at the left end
    QLabel* m_deckBLabel; // plain "B" silkscreen text at the right end
    LedIndicator* m_deckALed; // lit when deck A is the active deck
    LedIndicator* m_deckBLed;
    ConsoleButton* m_autoAdvanceButton; // was a QCheckBox; now a console toggle cap + a separate caption
    ConsoleButton* m_fadeNowButton;
    QPushButton* m_curveEqualPowerButton;
    QPushButton* m_curveLinearButton;
    QPushButton* m_curveSlowDownButton;
    QPushButton* m_curveSpeedUpButton;
    QButtonGroup* m_curveButtonGroup;
    bool m_manualFadeAvailable = false;
    bool m_crossfading = false;

    // Drives the slider visually during an AUTOMATIC crossfade. Without
    // this, the slider only ever reflects the user's own manual drags —
    // an auto-crossfade changes the real audio balance via
    // MixEngine::setGain() ramps directly, entirely bypassing the slider,
    // so it silently goes stale (confirmed via a real user report: active
    // deck correctly showed "B" in the status label while the slider
    // stayed frozen on "A"). A plain QVariantAnimation (not
    // QPropertyAnimation bound to the slider's "value" property) is used
    // deliberately: driving it through setSliderValueSilently() on every
    // tick avoids re-entering onSliderChanged(), which would otherwise
    // call setManualPosition() and race the real ramp already running in
    // MixEngine with a second, independently-curved one.
    QVariantAnimation* m_sliderAnimation;
};

} // namespace radio::ui
