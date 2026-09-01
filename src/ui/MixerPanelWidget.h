#pragma once

#include <QString>
#include <QWidget>

class QSlider;
class QPushButton;
class QTimer;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

class VuMeterWidget;
class LcdReadout;
class LevelBarWidget;
class RotaryKnob;

// Compact "sound engineer's board": a channel strip per deck (A/B) plus a
// master strip, each with its volume fader, EQ, VU meter, and related
// buttons all living together in one box — volume and EQ shaping belong to
// the same channel strip, not split across separate panels (confirmed via
// direct feedback after trying that split). Only depends on AudioEngine
// (not CrossfadeController or MixEngine directly), matching DeckWidget's
// existing decoupling convention. Hosted in a dockable QDockWidget in
// MainWindow (movable/rearrangeable/hideable via the View menu, same as
// Library/Queue/Cart Wall/Debug Console) rather than the fixed central
// layout.
//
// Styled as an analog studio console, modeled directly on a real hardware
// channel strip's own layout (fader + meter + a stacked button/knob column,
// packed tight, not spread thin): every slider's live value shows on a
// small glowing LcdReadout beneath it (replacing the plain name caption a
// plain slider used to have — see MixerSliderHelpers::makeScaledColumnWithReadout()),
// every toggle is a round IndicatorButton with its own LED (lit red while
// engaged) instead of a rectangular checkable QPushButton, and every
// strip's toggle buttons -- plus a RotaryKnob or two for each strip's own
// "whole channel" volume/gain control (deck Volume, Mic Gain, Ducking
// Amount, Master Volume) -- stack in one column directly beside that
// strip's VU meter (see makeButtonColumn()) rather than floating in a row
// below the sliders — the "whole channel" controls (meter + buttons + the
// volume knob), not the per-band ones, belong together. Each Deck strip's
// own EQ is deliberately just a Bass/Treble RotaryKnob pair (also living in
// that same button-stack column, not a slider row) rather than a full
// graphic EQ — a real console puts the multi-band shaping on the master
// bus, not duplicated per channel, so the actual 6-band graphic EQ (a
// slider row, like every other multi-band control) lives on the Master
// strip. The Mic strip keeps its own full 5-band slider EQ, same shape the
// Deck strips used to have — unlike a deck, the mic is a single always-on
// input with no per-track program material to simplify tone-shaping for,
// so its EQ wasn't part of this simplification. Live measurements that aren't
// sliders (Gain Reduction, Leveler's correction value, and the LUFS/true-
// peak row) sit just before the VU meter in the same row; the LUFS/TP row
// in particular renders as real bar meters (LevelBarWidget), not digit
// readouts, since that's exactly the kind of value a bar communicates
// better than a number alone. See RoundButton/IndicatorButton/LevelBarWidget/
// RotaryKnob's own doc comments for why each is built the way it is. Every
// checkable-toggle member below stays declared at its existing QPushButton*
// type deliberately: IndicatorButton/RoundButton are genuine QPushButton
// subclasses with no new public API, so nothing here needs to know or care
// which concrete class actually constructed the pointer it holds.
class MixerPanelWidget : public QWidget {
    Q_OBJECT

public:
    explicit MixerPanelWidget(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

private slots:
    void onMeterTimer(); // ~40ms — faster than DeckWidget's 200ms position timer, VU meters need to read as "live"

private:
    struct DeckStrip {
        // Rotary, not a linear fader -- lives in the button stack beside
        // the VU meter, not the slider row (see buildDeckStrip()'s own
        // comment on why Volume/Gain/Ducking specifically moved there).
        RotaryKnob* volumeKnob = nullptr;
        LcdReadout* volumeReadout = nullptr;
        QPushButton* autoCompButton = nullptr;
        // Just a Bass/Treble tone control -- the real graphic EQ lives on
        // the Master strip (see buildMasterStrip()'s 6-band EQ row), not
        // duplicated per deck.
        RotaryKnob* bassKnob = nullptr;
        LcdReadout* bassReadout = nullptr;
        RotaryKnob* trebleKnob = nullptr;
        LcdReadout* trebleReadout = nullptr;
        QPushButton* flatButton = nullptr;
        VuMeterWidget* meter = nullptr;
    };

    QWidget* buildDeckStrip(int index);
    QWidget* buildMasterStrip();
    QWidget* buildMicStrip();
    void restoreSettings(); // applies any previously-saved control values to both the widgets and the engine, matching CrossfadeSettingsDialog's own load-on-construct convention

    radio::audio::AudioEngine* m_engine;
    QString m_deckIds[2] = { QStringLiteral("A"), QStringLiteral("B") };
    DeckStrip m_deckStrips[2];

    // --- Mic strip (Phase 3). Not a DeckStrip -- it has its own enable
    // toggle (mic input isn't always running the way a deck always
    // "exists"), a gain slider instead of a plain trim slider, and ducking
    // controls neither deck strip needs.
    QPushButton* m_micEnabledButton = nullptr;
    RotaryKnob* m_micGainKnob = nullptr; // button-stack knob, not a slider -- see buildDeckStrip()'s comment on Volume/Gain/Ducking
    LcdReadout* m_micGainReadout = nullptr;
    // Full 5-band EQ, same slider row as the Deck strips used to have --
    // unlike Deck/Master, the mic strip keeps the full per-source graphic
    // EQ (see setEqBandGain()'s doc comment) rather than a Bass/Treble-only
    // knob pair.
    QSlider* m_micEqSliders[5] = {};
    LcdReadout* m_micEqReadouts[5] = {};
    VuMeterWidget* m_micMeter = nullptr;
    QPushButton* m_duckingEnabledButton = nullptr;
    RotaryKnob* m_duckingAmountKnob = nullptr; // button-stack knob, not a slider
    LcdReadout* m_duckingAmountReadout = nullptr;

    RotaryKnob* m_masterVolumeKnob = nullptr; // button-stack knob, not a slider -- see buildDeckStrip()'s comment on Volume/Gain/Ducking; the master bus fader moved here too per direct feedback
    LcdReadout* m_masterVolumeReadout = nullptr;
    QPushButton* m_loudnessButton = nullptr;
    QPushButton* m_highPassButton = nullptr;
    QPushButton* m_levelerButton = nullptr;
    // Live correction value, separate from the toggle itself -- see
    // onMeterTimer() -- as a real bar meter (LevelBarWidget, same visual
    // language as the M/S/I/TP row below) plus its own LcdReadout, not a
    // bare digit readout: every other live measurement in this strip gets
    // the bar+digit column treatment, and a lone undersized bare widget
    // here broke the row's uniform column rhythm (each column, whichever
    // kind, keeps the same fixed width/height so the whole row reads as
    // one consistent grid).
    LevelBarWidget* m_levelerBar = nullptr;
    LcdReadout* m_levelerReadout = nullptr;
    // The real 6-band graphic EQ (see MixEngine::setMasterEqBandGain()) --
    // band 0 = 60Hz shelf ("Bass"), band 5 = 12kHz shelf ("Treble"), same
    // frequencies each deck's own 2-knob tone control uses, bands 1-4 new.
    QSlider* m_masterEqSliders[6] = {};
    LcdReadout* m_masterEqReadouts[6] = {};
    QSlider* m_masterTargetSlider = nullptr;
    LcdReadout* m_masterTargetReadout = nullptr;
    QSlider* m_limiterCeilingSlider = nullptr;
    LcdReadout* m_limiterCeilingReadout = nullptr;
    QSlider* m_levelerRangeSlider = nullptr;
    LcdReadout* m_levelerRangeReadout = nullptr;
    // Same bar-meter-plus-readout column treatment as m_levelerBar above --
    // see its doc comment.
    LevelBarWidget* m_gainReductionBar = nullptr;
    LcdReadout* m_gainReductionReadout = nullptr;
    VuMeterWidget* m_masterMeter = nullptr;

    // EBU R128/LUFS + true-peak readout -- four LevelBarWidgets (a real bar
    // meter each, same visual language as the VU meters, dB-scaled instead
    // of linear-peak-scaled) paired with an LcdReadout showing the exact
    // number beneath each bar, arranged before the main VU meter, updated
    // on the same onMeterTimer() tick as the VU meters/Gain Reduction above.
    LevelBarWidget* m_momentaryLoudnessBar = nullptr;
    LcdReadout* m_momentaryLoudnessReadout = nullptr;
    LevelBarWidget* m_shortTermLoudnessBar = nullptr;
    LcdReadout* m_shortTermLoudnessReadout = nullptr;
    LevelBarWidget* m_integratedLoudnessBar = nullptr;
    LcdReadout* m_integratedLoudnessReadout = nullptr;
    LevelBarWidget* m_truePeakBar = nullptr;
    LcdReadout* m_truePeakReadout = nullptr;

    QTimer* m_meterTimer = nullptr;
    int m_meterTickCount = 0; // throttles integratedLoudnessLufs() -- see onMeterTimer()
    double m_lastIntegratedLufs = 0.0; // redisplayed every tick, recomputed only every kIntegratedLufsRefreshTicks
};

} // namespace radio::ui
