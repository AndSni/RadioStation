#include "CrossfaderWidget.h"
#include "ConsoleButton.h"
#include "ConsoleFader.h"
#include "ConsoleTheme.h"
#include "LedIndicator.h"

#include "audio/CrossfadeController.h"

#include <QButtonGroup>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QVariantAnimation>

#include <algorithm>

namespace radio::ui {

using radio::audio::CrossfadeController;
using radio::audio::RampCurve;

namespace {
// The A / B deck labels are plain silkscreen text at each end of the fader;
// which deck is live is shown by an LedIndicator beside each (same "engaged
// = lit red" language as the Mixer's IndicatorButton LEDs).
const QString kDeckEndLabelStyle = QStringLiteral("color: #cfc6b4; font-weight: bold; font-size: 13px;");
}

CrossfaderWidget::CrossfaderWidget(CrossfadeController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    auto* box = new QGroupBox(QStringLiteral("Crossfader"), this);
    box->setStyleSheet(theme::consoleBezelStyle());
    auto* layout = new QVBoxLayout(box);
    layout->setContentsMargins(20, 10, 20, 12); // breathing room from the bezel edges
    layout->setSpacing(4);

    // --- Fader on top; then ONE row: A LED/label pinned far-left, B
    //     LED/label far-right, fade controls centred between them ----------
    auto* fader = new ConsoleFader(Qt::Horizontal, box);
    fader->setRange(0, 100);
    fader->setValue(0);
    fader->setCenterDetent(true); // centre = both decks equal
    m_slider = fader;
    layout->addWidget(m_slider);

    m_deckALed = new LedIndicator(box);
    m_deckALed->setObjectName(QStringLiteral("deckALed"));
    m_deckBLed = new LedIndicator(box);
    m_deckBLed->setObjectName(QStringLiteral("deckBLed"));
    m_deckALabel = new QLabel(QStringLiteral("A"), box);
    m_deckBLabel = new QLabel(QStringLiteral("B"), box);
    for (auto* label : { m_deckALabel, m_deckBLabel }) {
        label->setStyleSheet(kDeckEndLabelStyle);
        label->setAlignment(Qt::AlignHCenter);
    }

    auto* leftEnd = new QVBoxLayout();
    leftEnd->setSpacing(1);
    leftEnd->addWidget(m_deckALed, 0, Qt::AlignHCenter);
    leftEnd->addWidget(m_deckALabel, 0, Qt::AlignHCenter);
    leftEnd->addStretch(1); // LED + letter stay at the top of the (taller) row
    auto* rightEnd = new QVBoxLayout();
    rightEnd->setSpacing(1);
    rightEnd->addWidget(m_deckBLed, 0, Qt::AlignHCenter);
    rightEnd->addWidget(m_deckBLabel, 0, Qt::AlignHCenter);
    rightEnd->addStretch(1);

    m_fadeNowButton = new ConsoleButton(QStringLiteral("Fade Now"), box);

    // Ramp curve, radio-button style: exactly one of these four is ever
    // checked, via the QButtonGroup below (not native QRadioButtons — kept
    // as checkable ConsoleButtons so they look consistent with every other
    // console toggle). Equal Power is the default (see CrossfadeController.h's
    // m_fadeCurve) — the other three are amplitude-linear and dip in
    // perceived loudness at the transition's midpoint, kept selectable for
    // anyone who wants that deliberate asymmetric feel.
    m_curveEqualPowerButton = new ConsoleButton(QStringLiteral("Equal Power"), box);
    m_curveLinearButton = new ConsoleButton(QStringLiteral("Linear"), box);
    m_curveSlowDownButton = new ConsoleButton(QStringLiteral("Slow Down"), box);
    m_curveSpeedUpButton = new ConsoleButton(QStringLiteral("Speed Up"), box);
    m_curveButtonGroup = new QButtonGroup(this);
    m_curveButtonGroup->setExclusive(true);
    m_curveButtonGroup->addButton(m_curveEqualPowerButton, static_cast<int>(RampCurve::EqualPower));
    m_curveButtonGroup->addButton(m_curveLinearButton, static_cast<int>(RampCurve::Linear));
    m_curveButtonGroup->addButton(m_curveSlowDownButton, static_cast<int>(RampCurve::SlowDown));
    m_curveButtonGroup->addButton(m_curveSpeedUpButton, static_cast<int>(RampCurve::SpeedUp));
    for (auto* button : { m_curveEqualPowerButton, m_curveLinearButton, m_curveSlowDownButton, m_curveSpeedUpButton })
        button->setCheckable(true);

    // All five fade-control buttons share one fixed size — sized to fit the
    // widest label ("Equal Power") — rather than each auto-sizing to its
    // own text, and rather than a hardcoded pixel guess that could drift
    // out of sync with the actual font/theme.
    int fadeControlsButtonWidth = 0;
    int fadeControlsButtonHeight = 0;
    const std::initializer_list<QPushButton*> fadeControlButtons = { static_cast<QPushButton*>(m_fadeNowButton),
        m_curveEqualPowerButton, m_curveLinearButton, m_curveSlowDownButton, m_curveSpeedUpButton };
    for (QPushButton* button : fadeControlButtons) {
        const QSize hint = button->sizeHint();
        fadeControlsButtonWidth = std::max(fadeControlsButtonWidth, hint.width());
        fadeControlsButtonHeight = std::max(fadeControlsButtonHeight, hint.height());
    }
    for (QPushButton* button : fadeControlButtons)
        button->setFixedSize(fadeControlsButtonWidth, fadeControlsButtonHeight);

    switch (m_controller->fadeCurve()) {
    case RampCurve::SlowDown:
        m_curveSlowDownButton->setChecked(true);
        break;
    case RampCurve::SpeedUp:
        m_curveSpeedUpButton->setChecked(true);
        break;
    case RampCurve::Linear:
        m_curveLinearButton->setChecked(true);
        break;
    case RampCurve::EqualPower:
    default:
        m_curveEqualPowerButton->setChecked(true);
        break;
    }

    // Auto-advance: a console toggle cap (unlabelled -- the function is
    // printed beside it, same grammar as the Mixer's switch caps) plus its
    // caption. Replaces the old plain QCheckBox.
    m_autoAdvanceButton = new ConsoleButton(QString(), box);
    m_autoAdvanceButton->setCheckable(true);
    m_autoAdvanceButton->setChecked(true);
    m_autoAdvanceButton->setFixedSize(46, fadeControlsButtonHeight);
    auto* autoAdvanceLabel = new QLabel(QStringLiteral("Auto-advance"), box);
    autoAdvanceLabel->setStyleSheet(QStringLiteral("color: #cfc6b4;"));

    // The four ramp-curve buttons in their own bordered "Fade Curve" box.
    auto* curveGroup = new QGroupBox(QStringLiteral("Fade Curve"), box);
    curveGroup->setStyleSheet(QStringLiteral(
        "QGroupBox { border: 1px solid #4a453c; border-radius: 5px; margin-top: 12px; padding: 3px 8px 4px 8px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 6px; "
        "color: #cfc6b4; font-weight: 600; }"));
    auto* curveRow = new QHBoxLayout(curveGroup);
    curveRow->setContentsMargins(0, 0, 0, 0);
    curveRow->setSpacing(4);
    for (auto* button : { m_curveEqualPowerButton, m_curveLinearButton, m_curveSlowDownButton, m_curveSpeedUpButton })
        curveRow->addWidget(button);

    autoAdvanceLabel->setMinimumHeight(fadeControlsButtonHeight);
    autoAdvanceLabel->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);

    // One row: [A LED/label] .... [Fade Now | Fade Curve | Auto-advance] .... [B LED/label]
    // Fade Now + auto-advance are bottom-aligned so every button shares one
    // baseline with the curve buttons inside the group box (whose title
    // area makes the box taller). The LED/label columns pin to each end,
    // roughly under the fader's travel ends.
    auto* midRow = new QHBoxLayout();
    midRow->setContentsMargins(10, 0, 10, 0);
    midRow->setSpacing(0);
    midRow->addLayout(leftEnd);
    midRow->addStretch(1);
    midRow->addWidget(m_fadeNowButton, 0, Qt::AlignBottom);
    midRow->addSpacing(16);
    midRow->addWidget(curveGroup, 0, Qt::AlignBottom);
    midRow->addSpacing(16);
    midRow->addWidget(m_autoAdvanceButton, 0, Qt::AlignBottom);
    midRow->addSpacing(6);
    midRow->addWidget(autoAdvanceLabel, 0, Qt::AlignBottom);
    midRow->addStretch(1);
    midRow->addLayout(rightEnd);
    layout->addLayout(midRow);
    layout->addStretch(1);

    // NOTE: the Auto-DJ hands-off/manual-override status used to live here as
    // a warning label. It moved to the always-visible Controls toolbar in
    // MainWindow (docs/console-ui-proposal.md §6) — a forgotten override
    // stops crossfading station-wide, so it belongs somewhere that can't be
    // hidden with this dock.

    box->setLayout(layout);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(box);
    setLayout(outer);

    m_sliderAnimation = new QVariantAnimation(this);
    connect(m_sliderAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { setSliderValueSilently(value.toInt()); });

    connect(m_slider, &QSlider::valueChanged, this, &CrossfaderWidget::onSliderChanged);
    connect(m_autoAdvanceButton, &QAbstractButton::toggled, this, &CrossfaderWidget::onAutoAdvanceToggled);
    connect(m_fadeNowButton, &QPushButton::clicked, this, &CrossfaderWidget::onFadeNowClicked);
    connect(m_curveButtonGroup, &QButtonGroup::idClicked, this,
        [this](int id) { m_controller->setFadeCurve(static_cast<RampCurve>(id)); });
    connect(m_controller, &CrossfadeController::autoCrossfadeStarted, this,
        &CrossfaderWidget::onAutoCrossfadeStarted);
    connect(m_controller, &CrossfadeController::autoCrossfadeFinished, this,
        &CrossfaderWidget::onAutoCrossfadeFinished);
    connect(m_controller, &CrossfadeController::manualFadeAvailabilityChanged, this,
        &CrossfaderWidget::onManualFadeAvailabilityChanged);

    updateActiveDeckLeds(m_controller->activeDeck());
    m_fadeNowButton->setEnabled(false); // nothing loaded yet at construction time
}

void CrossfaderWidget::setSliderValueSilently(int value)
{
    const QSignalBlocker blocker(m_slider);
    m_slider->setValue(value);
}

void CrossfaderWidget::onSliderChanged(int value)
{
    m_controller->setManualPosition(value / 100.0);
}

void CrossfaderWidget::onAutoAdvanceToggled(bool checked)
{
    m_controller->setAutoAdvanceEnabled(checked);
}

void CrossfaderWidget::onAutoCrossfadeStarted(const QString& fromDeck, const QString& toDeck)
{
    // toDeck is always the OTHER deck from fromDeck (A<->B), so its target
    // slider position is unambiguous: fully toward whichever side "B" is.
    const int target = toDeck == QStringLiteral("B") ? 100 : 0;
    m_sliderAnimation->stop();
    m_sliderAnimation->setStartValue(m_slider->value());
    m_sliderAnimation->setEndValue(target);
    m_sliderAnimation->setDuration(static_cast<int>(m_controller->fadeDurationMs()));
    m_sliderAnimation->start();

    m_crossfading = true;
    updateFadeNowEnabled();
    m_fadeNowButton->setBlinking(true);
}

void CrossfaderWidget::onAutoCrossfadeFinished(const QString& newActiveDeck)
{
    updateActiveDeckLeds(newActiveDeck);

    // Snap to the exact final position — the animation above should already
    // have arrived here, but this guarantees it regardless of any timing
    // drift between the UI animation clock and MixEngine's real audio ramp.
    m_sliderAnimation->stop();
    setSliderValueSilently(newActiveDeck == QStringLiteral("B") ? 100 : 0);

    m_crossfading = false;
    m_fadeNowButton->setBlinking(false);
    updateFadeNowEnabled();
}

void CrossfaderWidget::onManualFadeAvailabilityChanged(bool available)
{
    m_manualFadeAvailable = available;
    updateFadeNowEnabled();
}

void CrossfaderWidget::onFadeNowClicked()
{
    m_controller->requestManualFade();
}

void CrossfaderWidget::updateActiveDeckLeds(const QString& activeDeck)
{
    m_deckALed->setOn(activeDeck == QStringLiteral("A"));
    m_deckBLed->setOn(activeDeck == QStringLiteral("B"));
}

void CrossfaderWidget::updateFadeNowEnabled()
{
    m_fadeNowButton->setEnabled(m_manualFadeAvailable && !m_crossfading);
}

} // namespace radio::ui
