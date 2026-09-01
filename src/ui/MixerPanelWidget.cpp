#include "MixerPanelWidget.h"
#include "AudioDeviceSettings.h"
#include "ConsoleTheme.h"
#include "IndicatorButton.h"
#include "LcdReadout.h"
#include "LevelBarWidget.h"
#include "MixerSliderHelpers.h"
#include "RotaryKnob.h"
#include "RoundButton.h"
#include "VuMeterWidget.h"

#include "audio/AudioEngine.h"

#include <QGroupBox>
#include <QHBoxLayout>
#include <QSettings>
#include <QSlider>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <utility>
#include <vector>

namespace radio::ui {

using radio::audio::AudioEngine;

namespace {
// Each Deck strip's own tone control -- just Bass/Treble (a RotaryKnob
// pair in the button column, not a slider row); the real graphic EQ lives
// on the Master strip below. The Mic strip keeps the full 5-band slider
// EQ below (kEqBandLabels/kEqBandCount) instead. Must match MixEngine.cpp's
// kEqLowShelfFreq/kEqHighShelfFreq.
constexpr int kToneControlRangeDb = 12; // -12..+12 dB, same span the master EQ bands use

// The Mic strip's own full 5-band EQ (the Deck strips used to have this
// same shape before it was simplified to Bass/Treble knobs -- see
// kToneControlRangeDb's comment). Must match MixEngine.cpp's
// kEqLowShelfFreq/kEqBand2Freq/.../kEqHighShelfFreq.
const char* const kEqBandLabels[5] = { "60", "250", "1k", "4k", "12k" };
constexpr int kEqBandCount = 5;

// The Master strip's 6-band graphic EQ. Must match MixEngine.cpp's
// kEqLowShelfFreq/kMasterEqBand1Freq/.../kEqHighShelfFreq — band 0/5 are
// the same 60Hz/12kHz shelves each deck's own Bass/Treble knob uses.
const char* const kMasterEqBandLabels[6] = { "60", "150", "400", "1.2k", "3.5k", "12k" };
constexpr int kMasterEqBandCount = 6;
constexpr int kEqSliderRangeDb = 12; // -12..+12 dB per band
constexpr int kMasterVolumeRangeDb = 12; // -12..+12 dB around 0 = unity, same span as the EQ bands for consistency
constexpr int kAgcRefRangeDb = 24; // -24..0 dB, matches the old spin box's range
constexpr int kLimiterCeilingRangeDb = 3; // -3..0 dB
constexpr int kLevelerRangeMaxDb = 6; // 0..6 dB, maximum correction the leveler may apply in either direction

double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

// Shared LcdReadout digit formatting -- no unit suffix (the readout's own
// caption beneath already names the control, e.g. "60Hz"/"Vol"/"AGC Ref"),
// matching the sketch this rework is built from ("+6" over "20hz"). Works
// for every dB-ish slider in this panel, zero-centered or not: "+" is only
// ever prepended when the value is actually positive, so an AGC Ref/Ceiling
// slider (never positive) just shows its negative number unchanged.
QString formatSignedDb(int value)
{
    return QStringLiteral("%1%2").arg(value > 0 ? QStringLiteral("+") : QString()).arg(value);
}

// No "%" suffix -- the LCD font (built for a phone/clock LCD display) has
// no real glyph for it, which was rendering as a garbled "H"-like shape
// (confirmed against a real reference photo, not a sizing/clipping issue
// the earlier auto-shrink fix could address). Same "caption names the
// unit, digits stay bare" convention formatSignedDb() already uses.
QString formatPercent(int value)
{
    return QString::number(value);
}

// Light bezel pass on each channel strip's QGroupBox for a more physical-
// console frame -- a subtle vertical gradient plus a metallic-toned border,
// small radius (matching this codebase's only other border-radius
// precedents, e.g. OnAirLabel's 6px -- see RoundButton's own doc comment
// for why a full circle is painted rather than styled, unlike this, a small
// rectangular radius, which is exactly what those precedents already prove
// out fine). The QSS itself lives in ConsoleTheme so the eventual app-wide
// console restyle reuses the exact same frame (see docs/console-ui-proposal.md).
void applyConsoleBezelStyle(QGroupBox* box)
{
    box->setStyleSheet(theme::consoleBezelStyle());
}

// Stacks a strip's toggle buttons in one vertical column, positioned
// directly beside its VU meter within the same row (both are "whole
// channel", not per-band, controls) -- matches a real console channel's own
// fader+meter+button-stack unit (see this class's own header doc comment),
// rather than a separate row of buttons floating below the whole strip.
// `wrappedButtons` are already-built wrapWithCaption() results, one per
// button, top to bottom. The single leading stretch here (not one per
// button -- see wrapWithCaption()'s own comment on why) pushes the whole
// stack down to the sliders' baseline.
QWidget* makeButtonColumn(const std::vector<QWidget*>& wrappedButtons, QWidget* parent)
{
    auto* column = new QWidget(parent);
    column->setFixedWidth(kMixerColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);
    layout->addStretch(1);
    for (QWidget* button : wrappedButtons)
        layout->addWidget(button, 0, Qt::AlignHCenter);
    return column;
}

// Any control (a LevelBarWidget meter, a RotaryKnob) paired with an
// LcdReadout showing its exact value beneath it, in one fixed-width column
// -- same "control, then its caption/value" grammar as a slider column.
// `control` is already constructed by the caller (who keeps their own
// typed pointer to it) -- this only builds the readout + the column
// stacking them, so it works identically whether `control` is a meter (not
// draggable) or a knob (is).
struct ReadoutColumn {
    LcdReadout* readout;
    QWidget* container;
};

ReadoutColumn makeReadoutColumn(QWidget* control, const QString& caption, const QString& tooltip, QWidget* parent)
{
    control->setToolTip(tooltip);

    auto* readout = new LcdReadout(parent);
    readout->setCaption(caption);
    readout->setToolTip(tooltip);

    auto* column = new QWidget(parent);
    column->setFixedWidth(kMixerColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    layout->addWidget(control, 1, Qt::AlignHCenter);
    layout->addWidget(readout, 0, Qt::AlignHCenter);

    return { readout, column };
}

// Same "own QSettings key per control, written immediately on change"
// convention as CrossfadeSettingsDialog — no explicit Save button, no
// dependency on a closeEvent (this widget doesn't have one; it lives in a
// dock, not a top-level window).
QString deckEqKey(const QString& deckId, int band) { return QStringLiteral("mixer/deck%1/eq%2").arg(deckId).arg(band); } // band 0=Bass, 1=Treble
QString deckVolumeKey(const QString& deckId) { return QStringLiteral("mixer/deck%1/volume").arg(deckId); }
QString deckAutoCompKey(const QString& deckId) { return QStringLiteral("mixer/deck%1/autoComp").arg(deckId); }
QString masterEqKey(int band) { return QStringLiteral("mixer/master/eq%1").arg(band); }
// One-time migration fallback only, not written to going forward -- band 0/5
// of the unified 6-band masterEqKey() replace these now that Bass/Treble are
// no longer their own separate controls. See restoreSettings().
const QString kSettingsMasterBassBoostLegacy = QStringLiteral("mixer/master/bassBoost");
const QString kSettingsMasterTrebleLegacy = QStringLiteral("mixer/master/treble");
const QString kSettingsMasterVolume = QStringLiteral("mixer/master/volume");
const QString kSettingsMasterLoudness = QStringLiteral("mixer/master/loudness");
const QString kSettingsMasterAgcRef = QStringLiteral("mixer/master/agcRef");
const QString kSettingsMasterHighPass = QStringLiteral("mixer/master/highPass");
const QString kSettingsMasterLimiterCeiling = QStringLiteral("mixer/master/limiterCeiling");
const QString kSettingsMasterLeveler = QStringLiteral("mixer/master/leveler");
const QString kSettingsMasterLevelerRange = QStringLiteral("mixer/master/levelerRange");

constexpr int kDefaultLimiterCeilingDb = -1; // whole-dB slider granularity — was -0.3 as a spin box
constexpr int kDefaultLevelerRangeDb = 3;

QString micEqKey(int band) { return QStringLiteral("mixer/mic/eq%1").arg(band); }
const QString kSettingsMicEnabled = QStringLiteral("mixer/mic/enabled");
const QString kSettingsMicGain = QStringLiteral("mixer/mic/gain");
const QString kSettingsDuckingEnabled = QStringLiteral("mixer/mic/duckingEnabled");
const QString kSettingsDuckingAmountDb = QStringLiteral("mixer/mic/duckingAmountDb");
constexpr int kDefaultDuckingAmountDb = 12;
constexpr int kMaxDuckingAmountDb = 24;
}

MixerPanelWidget::MixerPanelWidget(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    // Deck/Mic strips size to their own content (stretch=0, sized to their
    // real content: a meter, a knob/button column, and — for the Mic strip
    // only — its own 5-band EQ) rather than each claiming an equal 1/4 of
    // the panel's width regardless of how little they actually need. Master
    // is the one strip with real width-hungry content (the 6-band EQ, the
    // AGC Ref/Ceiling/Leveler-Range sliders, and the LUFS/TP measurement
    // cluster) — stretch=1 makes it the sole strip that grows/shrinks with
    // the dock, absorbing whatever width the fixed-size strips don't need.
    for (int i = 0; i < 2; ++i)
        layout->addWidget(buildDeckStrip(i), 0);
    layout->addWidget(buildMasterStrip(), 1);
    layout->addWidget(buildMicStrip(), 0);

    restoreSettings(); // applies any previously-saved values to both the widgets and (via the same valueChanged/toggled lambdas used for user interaction) the engine

    m_meterTimer = new QTimer(this);
    m_meterTimer->setInterval(40);
    connect(m_meterTimer, &QTimer::timeout, this, &MixerPanelWidget::onMeterTimer);
    m_meterTimer->start();
}

QWidget* MixerPanelWidget::buildDeckStrip(int index)
{
    const QString deckId = m_deckIds[index];
    DeckStrip& strip = m_deckStrips[index];

    auto* box = new QGroupBox(QStringLiteral("Deck %1").arg(deckId), this);
    applyConsoleBezelStyle(box);
    auto* outer = new QVBoxLayout(box);

    // Row 1: VU meter and the button/knob stack together — one channel
    // strip, not split across panels (see this class's doc comment for
    // why). Volume/Bass/Treble all moved into the button-stack column
    // below -- no separate slider row on a deck strip (the real graphic EQ
    // lives only on the Master strip, see buildMasterStrip()).
    auto* row1 = new QHBoxLayout();
    row1->setSpacing(6); // tight, consistent gaps between every DISTINCT column group -- no per-column stretch (see below), so this is what actually sets the row's density

    strip.meter = new VuMeterWidget(box);
    row1->addWidget(makeScaledColumn(strip.meter, QStringLiteral("0dB"), QStringLiteral("-∞"), QStringLiteral("VU"), box));

    // Buttons AND the Volume knob stack in one column directly beside the
    // VU meter, in the same row -- see makeButtonColumn()'s own doc
    // comment for why (this is the "whole channel" group, not per-band,
    // matching a real console's fader+meter+button-stack unit). Volume is
    // a RotaryKnob here, not a linear fader -- it belongs with the other
    // "whole channel" controls, not the per-band EQ row.
    strip.volumeKnob = new RotaryKnob(0, 150, 100, box);
    strip.volumeKnob->setObjectName(QStringLiteral("deckVolumeKnob_%1").arg(deckId));
    {
        const auto readoutColumn = makeReadoutColumn(strip.volumeKnob, QStringLiteral("Vol"),
            QStringLiteral("100% (unchanged)"), box);
        strip.volumeReadout = readoutColumn.readout;
        connect(strip.volumeKnob, &RotaryKnob::valueChanged, this, [this, deckId, readout = strip.volumeReadout](int value) {
            m_engine->setDeckTrimVolume(deckId, value / 100.0);
            QSettings().setValue(deckVolumeKey(deckId), value);
            readout->setValue(formatPercent(value));
        });
        strip.volumeReadout->setValue(formatPercent(strip.volumeKnob->value()));

        // Bass/Treble: the deck strip's own tone control (see
        // DeckStrip::bassKnob's doc comment) -- band 0/4 of
        // setDeckEqBandGain() (the full per-source EQ still has 5 bands,
        // see setEqBandGain()'s doc comment; the Deck strip just only ever
        // drives the two end shelves), same knob-in-the-button-column
        // treatment as Volume above, not a slider row.
        strip.bassKnob = new RotaryKnob(-kToneControlRangeDb, kToneControlRangeDb, 0, box);
        strip.bassKnob->setObjectName(QStringLiteral("deckBassKnob_%1").arg(deckId));
        const auto bassColumn = makeReadoutColumn(strip.bassKnob, QStringLiteral("Bass"), QStringLiteral("60 Hz: 0 dB"), box);
        strip.bassReadout = bassColumn.readout;
        connect(strip.bassKnob, &RotaryKnob::valueChanged, this, [this, deckId, readout = strip.bassReadout](int value) {
            m_engine->setDeckEqBandGain(deckId, 0, value);
            QSettings().setValue(deckEqKey(deckId, 0), value);
            readout->setValue(formatSignedDb(value));
        });
        strip.bassReadout->setValue(formatSignedDb(strip.bassKnob->value()));

        strip.trebleKnob = new RotaryKnob(-kToneControlRangeDb, kToneControlRangeDb, 0, box);
        strip.trebleKnob->setObjectName(QStringLiteral("deckTrebleKnob_%1").arg(deckId));
        const auto trebleColumn
            = makeReadoutColumn(strip.trebleKnob, QStringLiteral("Treble"), QStringLiteral("12 kHz: 0 dB"), box);
        strip.trebleReadout = trebleColumn.readout;
        connect(strip.trebleKnob, &RotaryKnob::valueChanged, this, [this, deckId, readout = strip.trebleReadout](int value) {
            m_engine->setDeckEqBandGain(deckId, 4, value);
            QSettings().setValue(deckEqKey(deckId, 4), value);
            readout->setValue(formatSignedDb(value));
        });
        strip.trebleReadout->setValue(formatSignedDb(strip.trebleKnob->value()));

        strip.autoCompButton = new IndicatorButton(QString(), box);
        strip.autoCompButton->setCheckable(true);
        strip.autoCompButton->setObjectName(QStringLiteral("deckAutoCompButton_%1").arg(deckId));
        connect(strip.autoCompButton, &QPushButton::toggled, this, [this, deckId](bool checked) {
            m_engine->setDeckAutoGainCompensation(deckId, checked);
            QSettings().setValue(deckAutoCompKey(deckId), checked);
        });

        strip.flatButton = new RoundButton(QString(), box);
        // Captures [this, index] rather than a reference into m_deckStrips[index]
        // — indexing through the member array at click time avoids any question
        // about a captured reference's validity, at the cost of nothing (the
        // widget itself owns m_deckStrips for its whole lifetime either way).
        connect(strip.flatButton, &QPushButton::clicked, this, [this, index]() {
            // valueChanged already pushes each knob to the engine AND its readout.
            m_deckStrips[index].bassKnob->setValue(0);
            m_deckStrips[index].trebleKnob->setValue(0);
        });

        row1->addWidget(makeButtonColumn({ readoutColumn.container, bassColumn.container, trebleColumn.container,
            wrapWithCaption(strip.autoCompButton, QStringLiteral("AUTO COMP"), box),
            wrapWithCaption(strip.flatButton, QStringLiteral("FLAT"), box) },
            box));
    }
    row1->addStretch(1); // any extra dock width goes here, at the row's trailing edge -- not spread as gaps between columns (that's what disconnected the VU meter from its slider)

    outer->addLayout(row1, 1); // stretch=1: the slider/meter row is what expands vertically

    box->setLayout(outer);
    return box;
}

QWidget* MixerPanelWidget::buildMasterStrip()
{
    auto* box = new QGroupBox(QStringLiteral("Master"), this);
    applyConsoleBezelStyle(box);
    auto* outer = new QVBoxLayout(box);

    // Row 1: every master-bus shaping control as a slider, one channel
    // strip in the same spirit as the deck strips' EQ row — AGC Ref and
    // Limiter Ceiling lead (least "sound-shaping", most "reference/safety"),
    // then Bass/Treble (tone), then the live measurements and VU meter.
    // Master Volume itself is a RotaryKnob in the button stack, not a
    // slider here -- see its own construction below.
    auto* row1 = new QHBoxLayout();
    // Master is the one strip with real width-hungry content (see the
    // constructor's own comment on why it's the sole stretch=1 strip) --
    // an equal-weight stretch after EVERY individual column below (each EQ
    // band included, not grouped into its own tighter sub-row) means the
    // whole strip spreads out evenly to fill whatever width Master
    // actually has, instead of bunching on the left with all the extra
    // space dumped into one gap at the trailing edge.
    row1->setSpacing(6); // baseline gap; addStretch(1) after each column is what actually grows to fill the row

    // Deliberately worded to not be confused with the Volume fader below:
    // this doesn't change the mix's loudness by itself at all — it only
    // feeds each deck's OWN Auto dB Comp math (see DeckEngine::
    // recomputeTrim()) when that's turned on.
    m_masterTargetSlider = makeExpandingSlider(-kAgcRefRangeDb, 0, 0, box, /*zeroCenteredTicks=*/false);
    m_masterTargetSlider->setToolTip(QStringLiteral(
        "AGC Ref: 0 dB — NOT the master volume, only feeds each deck's own Auto dB Comp math, if that's turned on."));
    m_masterTargetSlider->setObjectName(QStringLiteral("masterAgcRefSlider"));
    {
        const auto scaled = makeScaledColumnWithReadout(
            m_masterTargetSlider, QStringLiteral("0"), QStringLiteral("-24"), QStringLiteral("AGC Ref"), box);
        m_masterTargetReadout = scaled.readout;
        auto* readout = scaled.readout;
        connect(m_masterTargetSlider, &QSlider::valueChanged, this, [this, slider = m_masterTargetSlider, readout](int value) {
            slider->setToolTip(QStringLiteral("AGC Ref: %1 dB").arg(value));
            m_engine->setMasterTargetDb(value);
            QSettings().setValue(kSettingsMasterAgcRef, value);
            readout->setValue(formatSignedDb(value));
        });
        readout->setValue(formatSignedDb(m_masterTargetSlider->value()));
        row1->addWidget(scaled.container);
    }
    row1->addStretch(1);

    // Always active (see MixEngine's own doc comment on why there's no
    // separate enable toggle: it's a protective device, not an optional
    // coloration) — the only user-facing knob is where the ceiling sits.
    m_limiterCeilingSlider = makeExpandingSlider(-kLimiterCeilingRangeDb, 0, kDefaultLimiterCeilingDb, box, /*zeroCenteredTicks=*/false);
    m_limiterCeilingSlider->setToolTip(QStringLiteral("Limiter Ceiling: %1 dB").arg(kDefaultLimiterCeilingDb));
    m_limiterCeilingSlider->setObjectName(QStringLiteral("masterLimiterCeilingSlider"));
    {
        const auto scaled = makeScaledColumnWithReadout(m_limiterCeilingSlider, QStringLiteral("0"), QStringLiteral("-3"),
            QStringLiteral("Ceiling"), box);
        m_limiterCeilingReadout = scaled.readout;
        auto* readout = scaled.readout;
        connect(m_limiterCeilingSlider, &QSlider::valueChanged, this, [this, slider = m_limiterCeilingSlider, readout](int value) {
            slider->setToolTip(QStringLiteral("Limiter Ceiling: %1 dB").arg(value));
            m_engine->setLimiterCeilingDb(value);
            QSettings().setValue(kSettingsMasterLimiterCeiling, value);
            readout->setValue(formatSignedDb(value));
        });
        readout->setValue(formatSignedDb(m_limiterCeilingSlider->value()));
        row1->addWidget(scaled.container);
    }
    row1->addStretch(1);

    // Maximum correction the leveler (see the "Leveler" button below) may
    // apply in either direction — a gentle top-up on what per-track
    // ReplayGain compensation already got most of the way there, not a
    // replacement for it.
    m_levelerRangeSlider = makeExpandingSlider(0, kLevelerRangeMaxDb, kDefaultLevelerRangeDb, box, /*zeroCenteredTicks=*/false);
    m_levelerRangeSlider->setToolTip(QStringLiteral("Leveler Range: +/-%1 dB").arg(kDefaultLevelerRangeDb));
    m_levelerRangeSlider->setObjectName(QStringLiteral("masterLevelerRangeSlider"));
    {
        const auto scaled = makeScaledColumnWithReadout(m_levelerRangeSlider, QString::number(kLevelerRangeMaxDb), QStringLiteral("0"),
            QStringLiteral("Leveler"), box);
        m_levelerRangeReadout = scaled.readout;
        auto* readout = scaled.readout;
        connect(m_levelerRangeSlider, &QSlider::valueChanged, this, [this, slider = m_levelerRangeSlider, readout](int value) {
            slider->setToolTip(QStringLiteral("Leveler Range: +/-%1 dB").arg(value));
            m_engine->setLevelerRangeDb(value);
            QSettings().setValue(kSettingsMasterLevelerRange, value);
            readout->setValue(formatSignedDb(value));
        });
        readout->setValue(formatSignedDb(m_levelerRangeSlider->value()));
        row1->addWidget(scaled.container);
    }
    row1->addStretch(1);

    // The real graphic EQ (see setMasterEqBandGain()'s doc comment) — 6
    // bands, each its own top-level row1 column with the same trailing
    // stretch every other column gets (see this function's own opening
    // comment): every column in this strip keeps equal spacing from its
    // neighbor, EQ bands included, rather than the bands packing into
    // their own tighter sub-group.
    for (int band = 0; band < kMasterEqBandCount; ++band) {
        auto* slider = makeExpandingSlider(-kEqSliderRangeDb, kEqSliderRangeDb, 0, box, /*zeroCenteredTicks=*/true);
        const QString bandLabel = QString::fromLatin1(kMasterEqBandLabels[band]);
        slider->setToolTip(QStringLiteral("%1 Hz: 0 dB").arg(bandLabel));
        slider->setObjectName(QStringLiteral("masterEqSlider_%1").arg(band));
        m_masterEqSliders[band] = slider;

        const auto scaled = makeScaledColumnWithReadout(
            slider, QStringLiteral("+12"), QStringLiteral("-12"), bandLabel, box);
        m_masterEqReadouts[band] = scaled.readout;
        auto* readout = scaled.readout;

        connect(slider, &QSlider::valueChanged, this, [this, slider, band, bandLabel, readout](int value) {
            slider->setToolTip(QStringLiteral("%1 Hz: %2%3 dB").arg(bandLabel,
                value > 0 ? QStringLiteral("+") : QString(), QString::number(value)));
            m_engine->setMasterEqBandGain(band, value);
            QSettings().setValue(masterEqKey(band), value);
            readout->setValue(formatSignedDb(value));
        });
        readout->setValue(formatSignedDb(slider->value())); // prime the display -- valueChanged doesn't fire for an unchanged value
        row1->addWidget(scaled.container);
        row1->addStretch(1);
    }

    // Live measurements — every one of them gets the same bar-meter-plus-
    // LcdReadout column treatment (LevelBarWidget, same visual language as
    // the VU meters, dB-scaled) so every column in this row, whichever
    // kind, keeps the same fixed width/height and the whole row reads as
    // one consistent grid, not some columns full-height bars and others
    // small bare digit-only widgets. GR's bar is fed the MAGNITUDE of the
    // reduction (0..12, not -12..0) so it reads empty at rest and fills UP
    // as the limiter engages harder -- the conventional direction for a
    // gain-reduction meter (see onMeterTimer(), which negates the sign
    // going into the bar but not into the LcdReadout beside it, that one
    // keeps showing the real signed dB value). Leveler's correction can go
    // either way, hence its own symmetric (not magnitude) range.
    m_gainReductionBar = new LevelBarWidget(0.0, 12.0, box);
    const auto gainReduction = makeReadoutColumn(m_gainReductionBar, QStringLiteral("GR"),
        QStringLiteral("Gain Reduction — how much the limiter is currently pulling the mix down to stay under "
                       "Ceiling. 0.0 dB most of the time; only moves when something actually exceeds it."),
        box);
    m_gainReductionReadout = gainReduction.readout;
    row1->addWidget(gainReduction.container);
    row1->addStretch(1);

    m_levelerBar = new LevelBarWidget(-kLevelerRangeMaxDb, kLevelerRangeMaxDb, box);
    const auto leveler = makeReadoutColumn(m_levelerBar, QStringLiteral("Leveler"),
        QStringLiteral("The Leveler toggle's own live correction value -- see the LEVELER button."), box);
    m_levelerReadout = leveler.readout;
    row1->addWidget(leveler.container);
    row1->addStretch(1);

    const QString loudnessTooltip = QStringLiteral(
        "EBU R128 loudness (LUFS): Momentary (400ms) / Short-term (3s) / Integrated (since the last streaming "
        "connect, or app start) — and TP, the true (inter-sample-aware) peak of the final output (dBFS).");
    {
        m_momentaryLoudnessBar = new LevelBarWidget(-40.0, 0.0, box);
        const auto momentary = makeReadoutColumn(m_momentaryLoudnessBar, QStringLiteral("M"), loudnessTooltip, box);
        m_momentaryLoudnessReadout = momentary.readout;
        row1->addWidget(momentary.container);
        row1->addStretch(1);

        m_shortTermLoudnessBar = new LevelBarWidget(-40.0, 0.0, box);
        const auto shortTerm = makeReadoutColumn(m_shortTermLoudnessBar, QStringLiteral("S"), loudnessTooltip, box);
        m_shortTermLoudnessReadout = shortTerm.readout;
        row1->addWidget(shortTerm.container);
        row1->addStretch(1);

        m_integratedLoudnessBar = new LevelBarWidget(-40.0, 0.0, box);
        const auto integrated = makeReadoutColumn(m_integratedLoudnessBar, QStringLiteral("I"), loudnessTooltip, box);
        m_integratedLoudnessReadout = integrated.readout;
        row1->addWidget(integrated.container);
        row1->addStretch(1);

        m_truePeakBar = new LevelBarWidget(-20.0, 0.0, box);
        const auto truePeak = makeReadoutColumn(m_truePeakBar, QStringLiteral("TP"), loudnessTooltip, box);
        m_truePeakReadout = truePeak.readout;
        row1->addWidget(truePeak.container);
    }
    row1->addStretch(1);

    m_masterMeter = new VuMeterWidget(box);
    row1->addWidget(makeScaledColumn(m_masterMeter, QStringLiteral("0dB"), QStringLiteral("-∞"), QStringLiteral("VU"), box));
    row1->addStretch(1);

    // Master Volume is a RotaryKnob in the button stack now, same as each
    // deck's own Volume knob -- see buildDeckStrip()'s comment on why these
    // "whole channel" controls live beside the meter/buttons rather than in
    // the per-band slider row above. Still centered on 0 dB = unity (the
    // mix's own natural level, untouched): straight up on the knob is the
    // range midpoint, matching a real mixer's master fader where "optimal"
    // sits in the middle, with +dB clockwise and -dB counter-clockwise.
    m_masterVolumeKnob = new RotaryKnob(-kMasterVolumeRangeDb, kMasterVolumeRangeDb, 0, box);
    m_masterVolumeKnob->setObjectName(QStringLiteral("masterVolumeKnob"));
    QWidget* masterVolumeColumn = nullptr;
    {
        const auto readoutColumn = makeReadoutColumn(m_masterVolumeKnob, QStringLiteral("Vol"), QStringLiteral("0 dB (unity)"), box);
        m_masterVolumeReadout = readoutColumn.readout;
        masterVolumeColumn = readoutColumn.container;
        connect(m_masterVolumeKnob, &RotaryKnob::valueChanged, this, [this, readout = m_masterVolumeReadout](int value) {
            m_engine->setMasterVolume(dbToLinear(value));
            QSettings().setValue(kSettingsMasterVolume, value);
            readout->setValue(formatSignedDb(value));
        });
        m_masterVolumeReadout->setValue(formatSignedDb(m_masterVolumeKnob->value()));
    }

    // Toggle buttons stack in a column beside the VU meter, same shape/size
    // as each other -- see makeButtonColumn()'s own doc comment. The
    // Leveler toggle lives here; its own live-correction value is the
    // separate m_levelerReadout above, not crammed onto this button, so
    // every button in this stack stays visually uniform.
    m_loudnessButton = new IndicatorButton(QString(), box);
    m_loudnessButton->setCheckable(true);
    m_loudnessButton->setObjectName(QStringLiteral("masterLoudnessButton"));
    connect(m_loudnessButton, &QPushButton::toggled, this, [this](bool checked) {
        m_engine->setLoudnessEnabled(checked);
        QSettings().setValue(kSettingsMasterLoudness, checked);
    });

    // Defaults on (see MixEngine's own doc comment) — unlike Loudness/
    // High-Pass, this directly addresses an active, reported loudness-
    // consistency problem rather than being optional coloring, so it
    // starts engaged.
    m_levelerButton = new IndicatorButton(QString(), box);
    m_levelerButton->setCheckable(true);
    m_levelerButton->setChecked(true);
    m_levelerButton->setObjectName(QStringLiteral("masterLevelerButton"));
    m_levelerButton->setToolTip(QStringLiteral(
        "Gently, continuously corrects the ACTUAL output level toward a consistent target — complements "
        "per-track ReplayGain compensation (Auto dB Comp) rather than replacing it."));
    connect(m_levelerButton, &QPushButton::toggled, this, [this](bool checked) {
        m_engine->setLevelerEnabled(checked);
        QSettings().setValue(kSettingsMasterLeveler, checked);
        if (!checked) {
            m_levelerReadout->setValue(QString()); // blanks to "--" the instant it's switched off -- see onMeterTimer()
            m_levelerBar->setLevelDb(0.0); // no correction to show while disabled
        }
    });

    m_highPassButton = new IndicatorButton(QString(), box);
    m_highPassButton->setCheckable(true);
    m_highPassButton->setObjectName(QStringLiteral("masterHighPassButton"));
    m_highPassButton->setToolTip(QStringLiteral("30 Hz subsonic filter — removes rumble/handling noise below the audible bass range."));
    connect(m_highPassButton, &QPushButton::toggled, this, [this](bool checked) {
        m_engine->setMasterHighPassEnabled(checked);
        QSettings().setValue(kSettingsMasterHighPass, checked);
    });

    row1->addWidget(makeButtonColumn({ masterVolumeColumn,
                                          wrapWithCaption(m_loudnessButton, QStringLiteral("LOUDNESS"), box),
                                          wrapWithCaption(m_levelerButton, QStringLiteral("LEVELER"), box),
                                          wrapWithCaption(m_highPassButton, QStringLiteral("HIGH-PASS"), box) },
        box));
    // No trailing stretch here -- the button/meter cluster is the "whole
    // channel" group and sits flush at the strip's right edge, same as a
    // real console's master fader/knob cluster; every OTHER gap above
    // already distributes the strip's extra width evenly between groups.

    outer->addLayout(row1, 1); // stretch=1: the slider/meter row is what expands vertically

    box->setLayout(outer);
    return box;
}

QWidget* MixerPanelWidget::buildMicStrip()
{
    auto* box = new QGroupBox(QStringLiteral("Mic"), this);
    applyConsoleBezelStyle(box);
    auto* outer = new QVBoxLayout(box);

    auto* row1 = new QHBoxLayout();
    row1->setSpacing(6); // tight, consistent gaps between every DISTINCT column group -- no per-column stretch (see below), so this is what actually sets the row's density

    // Full 5-band EQ, same per-band slider wiring as the Deck strips used
    // to have — MixEngine's per-source processing (see setEqBandGain()'s
    // doc comment) is already generic over any attached source, mic
    // included, and the mic keeps the full graphic EQ rather than being
    // simplified to a Bass/Treble knob pair (see this class's own doc
    // comment for why). Own tighter sub-row, same reasoning as
    // buildMasterStrip()'s masterEqRow.
    auto* eqRow = new QHBoxLayout();
    eqRow->setSpacing(3);
    for (int band = 0; band < kEqBandCount; ++band) {
        auto* slider = makeExpandingSlider(-kEqSliderRangeDb, kEqSliderRangeDb, 0, box, /*zeroCenteredTicks=*/true);
        const QString bandLabel = QString::fromLatin1(kEqBandLabels[band]);
        slider->setToolTip(QStringLiteral("%1 Hz: 0 dB").arg(bandLabel));
        slider->setObjectName(QStringLiteral("micEqSlider_%1").arg(band));
        m_micEqSliders[band] = slider;

        const auto scaled = makeScaledColumnWithReadout(slider, QStringLiteral("+12"), QStringLiteral("-12"), bandLabel, box);
        m_micEqReadouts[band] = scaled.readout;
        auto* readout = scaled.readout;
        connect(slider, &QSlider::valueChanged, this, [this, slider, band, bandLabel, readout](int value) {
            slider->setToolTip(QStringLiteral("%1 Hz: %2%3 dB").arg(bandLabel,
                value > 0 ? QStringLiteral("+") : QString(), QString::number(value)));
            m_engine->setDeckEqBandGain(QStringLiteral("mic"), band, value);
            QSettings().setValue(micEqKey(band), value);
            readout->setValue(formatSignedDb(value));
        });
        readout->setValue(formatSignedDb(slider->value()));
        eqRow->addWidget(scaled.container);
    }
    row1->addLayout(eqRow);

    m_micMeter = new VuMeterWidget(box);
    row1->addWidget(makeScaledColumn(m_micMeter, QStringLiteral("0dB"), QStringLiteral("-∞"), QStringLiteral("VU"), box));

    // Gain and Ducking Amount are RotaryKnobs in the button stack now, same
    // as each deck's own Volume knob -- see buildDeckStrip()'s comment on
    // why these "whole channel" controls live beside the meter/buttons
    // rather than in the per-band slider row.
    m_micGainKnob = new RotaryKnob(0, 150, 100, box);
    m_micGainKnob->setObjectName(QStringLiteral("micGainKnob"));
    QWidget* gainColumn = nullptr;
    {
        const auto readoutColumn = makeReadoutColumn(m_micGainKnob, QStringLiteral("Gain"), QStringLiteral("100% (unchanged)"), box);
        m_micGainReadout = readoutColumn.readout;
        gainColumn = readoutColumn.container;
        connect(m_micGainKnob, &RotaryKnob::valueChanged, this, [this, readout = m_micGainReadout](int value) {
            m_engine->setMicGain(value / 100.0);
            QSettings().setValue(kSettingsMicGain, value);
            readout->setValue(formatPercent(value));
        });
        m_micGainReadout->setValue(formatPercent(m_micGainKnob->value()));
    }

    // Same 0..kMaxDuckingAmountDb range as before, now a knob.
    m_duckingAmountKnob = new RotaryKnob(0, kMaxDuckingAmountDb, kDefaultDuckingAmountDb, box);
    m_duckingAmountKnob->setObjectName(QStringLiteral("duckingAmountKnob"));
    QWidget* duckColumn = nullptr;
    {
        const auto readoutColumn = makeReadoutColumn(m_duckingAmountKnob, QStringLiteral("Duck"),
            QStringLiteral("%1 dB").arg(kDefaultDuckingAmountDb), box);
        m_duckingAmountReadout = readoutColumn.readout;
        duckColumn = readoutColumn.container;
        connect(m_duckingAmountKnob, &RotaryKnob::valueChanged, this, [this, readout = m_duckingAmountReadout](int value) {
            m_engine->setDuckingAmountDb(value);
            QSettings().setValue(kSettingsDuckingAmountDb, value);
            readout->setValue(formatSignedDb(value));
        });
        m_duckingAmountReadout->setValue(formatSignedDb(m_duckingAmountKnob->value()));
    }

    // Toggle buttons AND the two knobs above stack in one column beside the
    // VU meter, same shape as the other strips -- see makeButtonColumn()'s
    // own doc comment.
    m_micEnabledButton = new IndicatorButton(QString(), box);
    m_micEnabledButton->setCheckable(true);
    m_micEnabledButton->setObjectName(QStringLiteral("micEnabledButton"));
    connect(m_micEnabledButton, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            const QByteArray micId
                = QByteArray::fromHex(QSettings().value(audio_device_settings::kMicDeviceId).toByteArray());
            if (m_engine->startMicInput(micId)) {
                // attachSource() resets a freshly-started source to its
                // defaults (unity gain, flat EQ) -- re-apply whatever this
                // strip's controls already show so re-enabling the mic
                // doesn't silently reset a previously-tuned gain/EQ.
                m_engine->setMicGain(m_micGainKnob->value() / 100.0);
                for (int band = 0; band < kEqBandCount; ++band)
                    m_engine->setDeckEqBandGain(QStringLiteral("mic"), band, m_micEqSliders[band]->value());
            } else {
                m_micEnabledButton->setChecked(false); // re-entrant, but toggled() only fires again on an actual change
            }
        } else {
            m_engine->stopMicInput();
        }
        QSettings().setValue(kSettingsMicEnabled, checked);
    });

    m_duckingEnabledButton = new IndicatorButton(QString(), box);
    m_duckingEnabledButton->setCheckable(true);
    m_duckingEnabledButton->setObjectName(QStringLiteral("duckingEnabledButton"));
    m_duckingEnabledButton->setToolTip(
        QStringLiteral("Automatically pulls both decks down while the mic is active, and restores them once it "
                       "goes quiet again."));
    connect(m_duckingEnabledButton, &QPushButton::toggled, this, [this](bool checked) {
        m_engine->setDuckingEnabled(checked);
        QSettings().setValue(kSettingsDuckingEnabled, checked);
    });

    row1->addWidget(makeButtonColumn({ gainColumn, duckColumn,
        wrapWithCaption(m_micEnabledButton, QStringLiteral("MIC"), box),
        wrapWithCaption(m_duckingEnabledButton, QStringLiteral("DUCKING"), box) },
        box));
    row1->addStretch(1); // see buildDeckStrip()'s identical comment on this line

    outer->addLayout(row1, 1);

    box->setLayout(outer);
    return box;
}

void MixerPanelWidget::restoreSettings()
{
    QSettings settings;
    for (int i = 0; i < 2; ++i) {
        const QString& deckId = m_deckIds[i];
        DeckStrip& strip = m_deckStrips[i];
        strip.bassKnob->setValue(settings.value(deckEqKey(deckId, 0), 0).toInt());
        strip.trebleKnob->setValue(settings.value(deckEqKey(deckId, 4), 0).toInt());
        strip.volumeKnob->setValue(settings.value(deckVolumeKey(deckId), 100).toInt());
        strip.autoCompButton->setChecked(settings.value(deckAutoCompKey(deckId), false).toBool());
    }

    // Bands 0/5 fall back to the pre-6-band-EQ Bass/Treble settings the
    // first time this runs after the upgrade (masterEqKey(0)/(5) won't
    // exist yet); every other band, and every run after that first one,
    // just uses masterEqKey() directly like any other band.
    m_masterEqSliders[0]->setValue(
        settings.value(masterEqKey(0), settings.value(kSettingsMasterBassBoostLegacy, 0)).toInt());
    m_masterEqSliders[5]->setValue(
        settings.value(masterEqKey(5), settings.value(kSettingsMasterTrebleLegacy, 0)).toInt());
    for (int band = 1; band < kMasterEqBandCount - 1; ++band)
        m_masterEqSliders[band]->setValue(settings.value(masterEqKey(band), 0).toInt());
    m_masterVolumeKnob->setValue(settings.value(kSettingsMasterVolume, 0).toInt());
    m_loudnessButton->setChecked(settings.value(kSettingsMasterLoudness, false).toBool());
    m_masterTargetSlider->setValue(settings.value(kSettingsMasterAgcRef, 0).toInt());
    m_highPassButton->setChecked(settings.value(kSettingsMasterHighPass, false).toBool());
    m_limiterCeilingSlider->setValue(settings.value(kSettingsMasterLimiterCeiling, kDefaultLimiterCeilingDb).toInt());
    m_levelerButton->setChecked(settings.value(kSettingsMasterLeveler, true).toBool());
    m_levelerRangeSlider->setValue(settings.value(kSettingsMasterLevelerRange, kDefaultLevelerRangeDb).toInt());

    for (int band = 0; band < kEqBandCount; ++band)
        m_micEqSliders[band]->setValue(settings.value(micEqKey(band), 0).toInt());
    m_micGainKnob->setValue(settings.value(kSettingsMicGain, 100).toInt());
    m_duckingAmountKnob->setValue(settings.value(kSettingsDuckingAmountDb, kDefaultDuckingAmountDb).toInt());
    // Set LAST: both toggled() handlers above read the sliders restored
    // just above to decide what to (re-)apply, and both actions
    // (starting mic capture, engaging ducking) are only meaningful once
    // everything else is already in its restored state.
    m_micEnabledButton->setChecked(settings.value(kSettingsMicEnabled, false).toBool());
    m_duckingEnabledButton->setChecked(settings.value(kSettingsDuckingEnabled, false).toBool());
}

void MixerPanelWidget::onMeterTimer()
{
    for (int i = 0; i < 2; ++i) {
        float left = 0.0f;
        float right = 0.0f;
        m_engine->deckPeakLevel(m_deckIds[i], left, right);
        m_deckStrips[i].meter->setLevel(left, right);
    }

    float masterLeft = 0.0f;
    float masterRight = 0.0f;
    m_engine->masterPeakLevel(masterLeft, masterRight);
    m_masterMeter->setLevel(masterLeft, masterRight);

    float micLeft = 0.0f;
    float micRight = 0.0f;
    m_engine->micPeakLevel(micLeft, micRight);
    m_micMeter->setLevel(micLeft, micRight);

    // LcdReadout::setValue() no-ops internally when the formatted string is
    // unchanged, so these are called unconditionally every tick -- no
    // m_last*Text guard members needed here anymore (see this class's own
    // header doc comment).
    const double gainReductionDb = m_engine->limiterGainReductionDb();
    m_gainReductionBar->setLevelDb(-gainReductionDb); // magnitude, not signed dB -- see m_gainReductionBar's doc comment
    m_gainReductionReadout->setValue(QStringLiteral("%1").arg(gainReductionDb, 0, 'f', 1));

    if (m_levelerButton->isChecked()) {
        const double levelerDb = m_engine->levelerGainDb();
        m_levelerBar->setLevelDb(levelerDb);
        m_levelerReadout->setValue(
            QStringLiteral("%1%2").arg(levelerDb >= 0.0 ? QStringLiteral("+") : QString()).arg(levelerDb, 0, 'f', 1));
    }

    // Momentary/short-term are plain atomic reads (cheap every tick, same
    // as the meters above) -- integratedLoudnessLufs() is real work (a
    // gating pass over up to hours of stored history, see its own doc
    // comment in MixEngine.h), so it's only recomputed every
    // kIntegratedLufsRefreshTicks ticks (~1s at this timer's 40ms
    // interval) rather than on every single tick forever.
    constexpr int kIntegratedLufsRefreshTicks = 25;
    if (m_meterTickCount % kIntegratedLufsRefreshTicks == 0)
        m_lastIntegratedLufs = m_engine->integratedLoudnessLufs();
    ++m_meterTickCount;

    const double momentaryLufs = m_engine->momentaryLoudnessLufs();
    m_momentaryLoudnessBar->setLevelDb(momentaryLufs);
    m_momentaryLoudnessReadout->setValue(QStringLiteral("%1").arg(momentaryLufs, 0, 'f', 1));

    const double shortTermLufs = m_engine->shortTermLoudnessLufs();
    m_shortTermLoudnessBar->setLevelDb(shortTermLufs);
    m_shortTermLoudnessReadout->setValue(QStringLiteral("%1").arg(shortTermLufs, 0, 'f', 1));

    m_integratedLoudnessBar->setLevelDb(m_lastIntegratedLufs);
    m_integratedLoudnessReadout->setValue(QStringLiteral("%1").arg(m_lastIntegratedLufs, 0, 'f', 1));

    const double truePeakDb = m_engine->outputTruePeakDb();
    m_truePeakBar->setLevelDb(truePeakDb);
    m_truePeakReadout->setValue(QStringLiteral("%1").arg(truePeakDb, 0, 'f', 1));
}

} // namespace radio::ui
