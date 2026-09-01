#include "CrossfadeSettingsDialog.h"

#include "audio/CrossfadeController.h"

#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSettings>

namespace radio::ui {

using radio::audio::CrossfadeController;

namespace {
const QString kSettingsPrefix = QStringLiteral("crossfade/");
constexpr double kDefaultLeadSeconds = 5.0;
constexpr double kDefaultFadeSeconds = 4.0;
}

CrossfadeSettingsDialog::CrossfadeSettingsDialog(CrossfadeController* controller, QWidget* parent)
    : QDialog(parent)
    , m_controller(controller)
{
    setWindowTitle(QStringLiteral("Crossfade Settings"));

    m_leadSecondsSpin = new QDoubleSpinBox(this);
    m_leadSecondsSpin->setRange(0.5, 60.0);
    m_leadSecondsSpin->setSingleStep(0.5);
    m_leadSecondsSpin->setSuffix(QStringLiteral(" s"));
    m_leadSecondsSpin->setToolTip(
        QStringLiteral("How far from the end of the active track to start the automatic crossfade."));

    m_fadeSecondsSpin = new QDoubleSpinBox(this);
    m_fadeSecondsSpin->setRange(0.5, 30.0);
    m_fadeSecondsSpin->setSingleStep(0.5);
    m_fadeSecondsSpin->setSuffix(QStringLiteral(" s"));
    m_fadeSecondsSpin->setToolTip(QStringLiteral("How long the crossfade transition itself takes."));

    auto* form = new QFormLayout(this);
    form->addRow(QStringLiteral("Crossfade lead time"), m_leadSecondsSpin);
    form->addRow(QStringLiteral("Fade duration"), m_fadeSecondsSpin);
    setLayout(form);

    connect(m_leadSecondsSpin, &QDoubleSpinBox::valueChanged, this,
        &CrossfadeSettingsDialog::onLeadSecondsChanged);
    connect(m_fadeSecondsSpin, &QDoubleSpinBox::valueChanged, this,
        &CrossfadeSettingsDialog::onFadeSecondsChanged);

    loadSettings(); // applies the loaded (or default) values to the controller immediately
}

void CrossfadeSettingsDialog::loadSettings()
{
    QSettings settings;
    const double leadSeconds = settings.value(kSettingsPrefix + "leadSeconds", kDefaultLeadSeconds).toDouble();
    const double fadeSeconds = settings.value(kSettingsPrefix + "fadeSeconds", kDefaultFadeSeconds).toDouble();

    // setValue() below triggers onLeadSecondsChanged/onFadeSecondsChanged
    // (valueChanged fires even for a programmatic change), which is exactly
    // what applies these to the live controller and re-persists them — no
    // separate "apply" step needed.
    m_leadSecondsSpin->setValue(leadSeconds);
    m_fadeSecondsSpin->setValue(fadeSeconds);
}

void CrossfadeSettingsDialog::onLeadSecondsChanged(double seconds)
{
    m_controller->setCrossfadeLeadMs(static_cast<qint64>(seconds * 1000));
    QSettings settings;
    settings.setValue(kSettingsPrefix + "leadSeconds", seconds);
}

void CrossfadeSettingsDialog::onFadeSecondsChanged(double seconds)
{
    m_controller->setFadeDurationMs(static_cast<qint64>(seconds * 1000));
    QSettings settings;
    settings.setValue(kSettingsPrefix + "fadeSeconds", seconds);
}

} // namespace radio::ui
