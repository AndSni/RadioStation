#include "RadioStatisticsPanel.h"
#include "ConsoleTheme.h"
#include "OnAirLabel.h"
#include "StationSettings.h"
#include "UiStyleHelpers.h"

#include "audio/AudioEngine.h"
#include "db/ScheduleBlockRepository.h"
#include "scheduler/BlockTimeResolver.h"

#include <QDateTime>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QSettings>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>

namespace radio::ui {

using namespace radio::ui::station_settings;
using radio::audio::AudioEngine;
using radio::audio::DeckState;
using radio::db::ScheduleBlockRepository;
using radio::scheduler::BlockTimeResolver;

namespace {
// Deliberately duplicated from AutoDjPanelWidget.cpp's own formatRemaining()
// rather than shared — see this class's header doc comment on why the two
// widgets each keep their own small poll instead of a shared abstraction.
QString formatRemaining(qint64 totalSeconds)
{
    const qint64 hours = totalSeconds / 3600;
    const qint64 minutes = (totalSeconds % 3600) / 60;
    const qint64 seconds = totalSeconds % 60;
    if (hours > 0) {
        return QStringLiteral("%1:%2:%3")
            .arg(hours)
            .arg(minutes, 2, 10, QLatin1Char('0'))
            .arg(seconds, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(minutes, 2, 10, QLatin1Char('0')).arg(seconds, 2, 10, QLatin1Char('0'));
}

}

RadioStatisticsPanel::RadioStatisticsPanel(AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    // Warm metal bezel around the whole panel, matching the Mixer/Deck
    // strips (see ConsoleTheme::consolePanelStyle()).
    auto* contentFrame = new QFrame(this);
    contentFrame->setObjectName(QStringLiteral("radioStatisticsFrame"));
    contentFrame->setStyleSheet(theme::consolePanelStyle(QStringLiteral("radioStatisticsFrame")));
    outerLayout->addWidget(contentFrame);

    auto* layout = new QVBoxLayout(contentFrame);
    layout->setContentsMargins(12, 10, 12, 10);

    auto* row1 = new QHBoxLayout();
    m_onAirLabel = new OnAirLabel(contentFrame);
    row1->addWidget(m_onAirLabel);
    row1->addStretch(1);
    m_radioNameLabel = new QLabel(kDefaultRadioName, contentFrame);
    m_radioNameLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row1->addWidget(m_radioNameLabel);
    layout->addLayout(row1);

    // The clock sits on a dark "LCD screen" frame, like every LcdReadout /
    // DotMatrixDisplay. The label itself stays transparent so its
    // drop-shadow glow hugs the digits rather than tracing a rectangle
    // (see the m_clockGlow note below).
    auto* clockFrame = new QFrame(contentFrame);
    clockFrame->setObjectName(QStringLiteral("radioStatisticsClockFrame"));
    clockFrame->setStyleSheet(theme::lcdScreenFrameStyle(QStringLiteral("radioStatisticsClockFrame")));
    auto* clockFrameLayout = new QVBoxLayout(clockFrame);
    clockFrameLayout->setContentsMargins(12, 8, 12, 8);

    m_clockLabel = new QLabel(QStringLiteral("00:00:00"), clockFrame);
    m_clockLabel->setObjectName(QStringLiteral("radioStatisticsClockLabel"));
    m_clockLabel->setAlignment(Qt::AlignCenter);
    QFont clockFont = m_clockLabel->font();
    const QString lcdFamily = lcdFontFamily();
    if (!lcdFamily.isEmpty())
        clockFont.setFamily(lcdFamily);
    m_clockLabel->setFont(clockFont);

    // Static glow (not pulsing like OnAirLabel's breathing animation) — the
    // clock is always "on", so a constant tube-glow is the right look, not
    // a signal of changing state the way ON AIR's pulse is. No border/
    // background here (unlike OnAirLabel) — an explicitly transparent
    // background matters for more than looks: QGraphicsDropShadowEffect
    // blurs whatever this widget actually paints, so if anything opaque
    // filled its rect the glow would trace THAT rectangle instead of
    // hugging the digits themselves.
    m_clockGlow = new QGraphicsDropShadowEffect(this);
    m_clockGlow->setOffset(0, 0);
    m_clockGlow->setBlurRadius(18);
    m_clockLabel->setGraphicsEffect(m_clockGlow);

    clockFrameLayout->addWidget(m_clockLabel);
    layout->addWidget(clockFrame);

    auto* row3 = new QHBoxLayout();
    m_blockNameLabel = new QLabel(contentFrame);
    row3->addWidget(m_blockNameLabel);
    row3->addStretch(1);
    m_blockRemainingLabel = new QLabel(QStringLiteral("No active block"), contentFrame);
    m_blockRemainingLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    row3->addWidget(m_blockRemainingLabel);
    layout->addLayout(row3);

    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &RadioStatisticsPanel::onTick);
    m_timer->start();

    refreshAppearanceIfChanged();
    onTick(); // reflect real state immediately, not after the first 1s tick
}

void RadioStatisticsPanel::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    applyFonts();
}

void RadioStatisticsPanel::applyFonts()
{
    QSettings settings;
    const bool autoSize = settings.value(kAutoSizeFonts, kDefaultAutoSizeFonts).toBool();

    qreal smallSize;
    qreal clockSize;
    if (autoSize) {
        const qreal h = static_cast<qreal>(height());
        smallSize = std::clamp(h * 0.07, 10.0, 28.0);
        clockSize = std::clamp(h * 0.14, 16.0, 56.0);
    } else {
        smallSize = settings.value(kSmallFontPt, kDefaultSmallFontPt).toInt();
        clockSize = settings.value(kClockFontPt, kDefaultClockFontPt).toInt();
    }

    for (QLabel* label : std::initializer_list<QLabel*>{ m_onAirLabel, m_radioNameLabel, m_blockNameLabel, m_blockRemainingLabel }) {
        QFont f = label->font();
        f.setPointSizeF(smallSize);
        label->setFont(f);
    }

    QFont clockFont = m_clockLabel->font();
    clockFont.setPointSizeF(clockSize);
    m_clockLabel->setFont(clockFont);
}

void RadioStatisticsPanel::applyColors()
{
    QSettings settings;
    m_onAirLabel->setColors(QColor(settings.value(kOnAirColor, kDefaultOnAirColor.name()).toString()),
        QColor(settings.value(kOnAirOffColor, kDefaultOnAirOffColor.name()).toString()));

    const QColor clockColor(settings.value(kClockColor, kDefaultClockColor.name()).toString());
    m_clockLabel->setStyleSheet(QStringLiteral("color: %1; background: transparent; border: none;").arg(clockColor.name()));
    m_clockGlow->setColor(clockColor);
}

void RadioStatisticsPanel::refreshAppearanceIfChanged()
{
    QSettings settings;

    const QString radioName = settings.value(kRadioName, kDefaultRadioName).toString();
    if (radioName != m_lastRadioName) {
        m_lastRadioName = radioName;
        m_radioNameLabel->setText(radioName);
    }

    const bool autoSize = settings.value(kAutoSizeFonts, kDefaultAutoSizeFonts).toBool();
    const int smallFontPt = settings.value(kSmallFontPt, kDefaultSmallFontPt).toInt();
    const int clockFontPt = settings.value(kClockFontPt, kDefaultClockFontPt).toInt();
    if (autoSize != m_lastAutoSizeFonts || smallFontPt != m_lastSmallFontPt || clockFontPt != m_lastClockFontPt) {
        m_lastAutoSizeFonts = autoSize;
        m_lastSmallFontPt = smallFontPt;
        m_lastClockFontPt = clockFontPt;
        applyFonts();
    }

    const QString onAirColor = settings.value(kOnAirColor, kDefaultOnAirColor.name()).toString();
    const QString onAirOffColor = settings.value(kOnAirOffColor, kDefaultOnAirOffColor.name()).toString();
    const QString clockColor = settings.value(kClockColor, kDefaultClockColor.name()).toString();
    if (onAirColor != m_lastOnAirColor || onAirOffColor != m_lastOnAirOffColor || clockColor != m_lastClockColor) {
        m_lastOnAirColor = onAirColor;
        m_lastOnAirOffColor = onAirOffColor;
        m_lastClockColor = clockColor;
        applyColors();
    }
}

void RadioStatisticsPanel::onTick()
{
    const QDateTime now = QDateTime::currentDateTime();
    m_clockLabel->setText(now.time().toString(QStringLiteral("HH:mm:ss")));

    refreshAppearanceIfChanged();

    const auto blocks = ScheduleBlockRepository::allBlocks();
    const qint64 activeId = BlockTimeResolver::resolveActiveBlockId(blocks, now);
    const auto it = activeId >= 0
        ? std::find_if(blocks.begin(), blocks.end(), [activeId](const auto& block) { return block.id == activeId; })
        : blocks.end();
    if (it == blocks.end()) {
        m_blockNameLabel->setText(QString());
        m_blockRemainingLabel->setText(QStringLiteral("No active block"));
    } else {
        m_blockNameLabel->setText(it->name);
        m_blockRemainingLabel->setText(
            QStringLiteral("%1 remaining").arg(formatRemaining(BlockTimeResolver::secondsRemainingInBlock(*it, now))));
    }

    const bool onAir
        = m_engine->state(QStringLiteral("A")) == DeckState::Playing || m_engine->state(QStringLiteral("B")) == DeckState::Playing;
    m_onAirLabel->setOnAir(onAir);
}

} // namespace radio::ui
