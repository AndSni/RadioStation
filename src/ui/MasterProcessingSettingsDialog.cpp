#include "MasterProcessingSettingsDialog.h"

#include "audio/AudioEngine.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QSettings>
#include <QVBoxLayout>

namespace radio::ui {

using radio::audio::AudioEngine;

namespace {
const QString kSettingsPrefix = QStringLiteral("compressor/");
const char* const kBandKeys[3] = { "low", "mid", "high" };
constexpr double kDefaultThresholdDb[3] = { -20.0, -18.0, -24.0 }; // matches MixEngine.h's own defaults
constexpr double kDefaultRatio[3] = { 2.0, 2.0, 4.0 };
}

MasterProcessingSettingsDialog::MasterProcessingSettingsDialog(AudioEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Master Processing"));

    auto* outer = new QVBoxLayout(this);

    m_enabledCheck = new QCheckBox(QStringLiteral("Multiband Compressor Enabled"), this);
    connect(m_enabledCheck, &QCheckBox::toggled, this, [this](bool checked) {
        m_engine->setCompressorEnabled(checked);
        QSettings().setValue(kSettingsPrefix + QStringLiteral("enabled"), checked);
    });
    outer->addWidget(m_enabledCheck);

    outer->addWidget(buildBandGroup(0, QStringLiteral("Low (< 200 Hz)"), QString()));
    outer->addWidget(buildBandGroup(1, QStringLiteral("Mid (200 Hz - 3 kHz)"), QString()));
    outer->addWidget(buildBandGroup(2, QStringLiteral("High (> 3 kHz) / De-esser"),
        QStringLiteral("Tuned faster/tighter than Low/Mid internally (see MixEngine's kDeEsserAttackSeconds etc.) "
                        "— this is also where sibilance control lives, not a separate control.")));

    setLayout(outer);

    loadSettings(); // applies the loaded (or default) values to the engine immediately
}

QWidget* MasterProcessingSettingsDialog::buildBandGroup(int band, const QString& label, const QString& tooltip)
{
    auto* group = new QGroupBox(label, this);
    if (!tooltip.isEmpty())
        group->setToolTip(tooltip);

    auto* thresholdSpin = new QDoubleSpinBox(group);
    thresholdSpin->setRange(-40.0, 0.0);
    thresholdSpin->setSingleStep(0.5);
    thresholdSpin->setSuffix(QStringLiteral(" dB"));
    m_thresholdSpins[band] = thresholdSpin;
    connect(thresholdSpin, &QDoubleSpinBox::valueChanged, this, [this, band](double value) {
        m_engine->setBandThresholdDb(band, value);
        QSettings().setValue(kSettingsPrefix + QStringLiteral("%1Threshold").arg(kBandKeys[band]), value);
    });

    auto* ratioSpin = new QDoubleSpinBox(group);
    ratioSpin->setRange(1.0, 20.0);
    ratioSpin->setSingleStep(0.5);
    ratioSpin->setSuffix(QStringLiteral(":1"));
    m_ratioSpins[band] = ratioSpin;
    connect(ratioSpin, &QDoubleSpinBox::valueChanged, this, [this, band](double value) {
        m_engine->setBandRatio(band, value);
        QSettings().setValue(kSettingsPrefix + QStringLiteral("%1Ratio").arg(kBandKeys[band]), value);
    });

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Threshold"), thresholdSpin);
    form->addRow(QStringLiteral("Ratio"), ratioSpin);
    group->setLayout(form);
    return group;
}

void MasterProcessingSettingsDialog::loadSettings()
{
    QSettings settings;
    m_enabledCheck->setChecked(settings.value(kSettingsPrefix + QStringLiteral("enabled"), true).toBool());

    // setValue() below triggers each spin box's valueChanged (fires even
    // for a programmatic change), which is exactly what applies these to
    // the live engine and re-persists them — no separate "apply" step
    // needed, same as CrossfadeSettingsDialog::loadSettings().
    for (int band = 0; band < 3; ++band) {
        m_thresholdSpins[band]->setValue(
            settings.value(kSettingsPrefix + QStringLiteral("%1Threshold").arg(kBandKeys[band]), kDefaultThresholdDb[band])
                .toDouble());
        m_ratioSpins[band]->setValue(
            settings.value(kSettingsPrefix + QStringLiteral("%1Ratio").arg(kBandKeys[band]), kDefaultRatio[band])
                .toDouble());
    }
}

} // namespace radio::ui
