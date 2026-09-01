#include "StationSettingsDialog.h"
#include "StationSettings.h"

#include "db/CartRepository.h"

#include <QAbstractItemView>
#include <QCheckBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeySequence>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <algorithm>

namespace radio::ui {

using namespace radio::ui::station_settings;
using radio::db::CartClipRecord;
using radio::db::CartRepository;

namespace {
constexpr int kHotkeyRefreshIntervalMs = 1000; // matches AutoDjPanelWidget's/RadioStatisticsPanel's own 1s poll cadence
}

StationSettingsDialog::StationSettingsDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Station Settings"));

    m_radioNameEdit = new QLineEdit(this);
    m_radioNameEdit->setToolTip(QStringLiteral("Shown in the Radio Statistics panel."));

    m_autoSizeFontsCheck = new QCheckBox(QStringLiteral("Auto-size fonts to panel height"), this);

    m_smallFontSizeSpin = new QSpinBox(this);
    m_smallFontSizeSpin->setRange(6, 72);
    m_smallFontSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_smallFontSizeSpin->setToolTip(QStringLiteral("On Air / Radio Name / Block row text — only used when auto-size is off."));

    m_clockFontSizeSpin = new QSpinBox(this);
    m_clockFontSizeSpin->setRange(6, 200);
    m_clockFontSizeSpin->setSuffix(QStringLiteral(" pt"));
    m_clockFontSizeSpin->setToolTip(QStringLiteral("Clock text — only used when auto-size is off."));

    // Fixed size (from placeholder text, before any state-dependent
    // stylesheet is applied) plus an always-applied stylesheet in every
    // state — a styled-vs-unstyled QPushButton computes a different box
    // model in Qt, the same fix applied to ScheduleBlockEditorDialog's Pick
    // Color button and AutoDjPanelWidget's toggle button this session.
    m_onAirColorButton = new QPushButton(QStringLiteral("Pick Color..."), this);
    m_onAirColorButton->setMinimumWidth(110);
    m_onAirOffColorButton = new QPushButton(QStringLiteral("Pick Color..."), this);
    m_onAirOffColorButton->setMinimumWidth(110);
    m_clockColorButton = new QPushButton(QStringLiteral("Pick Color..."), this);
    m_clockColorButton->setMinimumWidth(110);

    auto* generalTab = new QWidget(this);
    auto* form = new QFormLayout(generalTab);
    form->addRow(QStringLiteral("Radio Name"), m_radioNameEdit);
    form->addRow(m_autoSizeFontsCheck);
    form->addRow(QStringLiteral("Small Text Size"), m_smallFontSizeSpin);
    form->addRow(QStringLiteral("Clock Text Size"), m_clockFontSizeSpin);
    form->addRow(QStringLiteral("On Air Color"), m_onAirColorButton);
    form->addRow(QStringLiteral("On Air Off Color"), m_onAirOffColorButton);
    form->addRow(QStringLiteral("Clock Color"), m_clockColorButton);

    auto* hotkeysTab = new QWidget(this);
    m_hotkeyTable = new QTableWidget(0, 2, hotkeysTab);
    m_hotkeyTable->setHorizontalHeaderLabels({ QStringLiteral("Hotkey"), QStringLiteral("Cart") });
    m_hotkeyTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_hotkeyTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_hotkeyTable->verticalHeader()->setVisible(false);
    m_hotkeyTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_hotkeyTable->setSelectionMode(QAbstractItemView::NoSelection);
    auto* hotkeysLayout = new QVBoxLayout(hotkeysTab);
    hotkeysLayout->addWidget(m_hotkeyTable);

    m_emergencyCartPathEdit = new QLineEdit(this);
    m_emergencyCartPathEdit->setToolTip(
        QStringLiteral("The audio file the Panic button and (if enabled below) dead-air auto-failover play."));
    m_emergencyCartBrowseButton = new QPushButton(QStringLiteral("Browse..."), this);
    auto* emergencyCartRow = new QHBoxLayout();
    emergencyCartRow->addWidget(m_emergencyCartPathEdit, 1);
    emergencyCartRow->addWidget(m_emergencyCartBrowseButton);

    m_deadAirThresholdSpin = new QDoubleSpinBox(this);
    m_deadAirThresholdSpin->setRange(1.0, 120.0);
    m_deadAirThresholdSpin->setSuffix(QStringLiteral(" s"));
    m_deadAirThresholdSpin->setToolTip(QStringLiteral("How long the master output can sit silent before it's treated as dead air."));

    m_autoFailoverCheck = new QCheckBox(QStringLiteral("Automatically trigger emergency cart on dead air"), this);

    auto* failoverTab = new QWidget(this);
    auto* failoverForm = new QFormLayout(failoverTab);
    failoverForm->addRow(QStringLiteral("Emergency Cart/Track"), emergencyCartRow);
    failoverForm->addRow(QStringLiteral("Dead-Air Alert After"), m_deadAirThresholdSpin);
    failoverForm->addRow(m_autoFailoverCheck);

    // Deck Display -- live tuning of the deck dot-matrix readout so its size
    // can be dialled in against real screens without a rebuild.
    m_deckLine1FontSpin = new QSpinBox(this);
    m_deckLine1FontSpin->setRange(2, 10);
    m_deckLine1FontSpin->setSuffix(QStringLiteral(" px/dot"));
    m_deckLine1FontSpin->setToolTip(QStringLiteral("Track line (Artist — Title): pixels per LCD grid dot."));
    m_deckLine2FontSpin = new QSpinBox(this);
    m_deckLine2FontSpin->setRange(2, 10);
    m_deckLine2FontSpin->setSuffix(QStringLiteral(" px/dot"));
    m_deckLine2FontSpin->setToolTip(QStringLiteral("Detail line (bitrate / BPM / gain): pixels per LCD grid dot."));
    m_deckColourByStateCheck
        = new QCheckBox(QStringLiteral("Colour by state (red = playing, yellow = cued)"), this);
    m_deckColourByStateCheck->setToolTip(
        QStringLiteral("Tint each deck's display so it's obvious which deck is on air."));

    auto* deckDisplayTab = new QWidget(this);
    auto* deckDisplayForm = new QFormLayout(deckDisplayTab);
    deckDisplayForm->addRow(QStringLiteral("Track Line Dot Size"), m_deckLine1FontSpin);
    deckDisplayForm->addRow(QStringLiteral("Detail Line Dot Size"), m_deckLine2FontSpin);
    deckDisplayForm->addRow(m_deckColourByStateCheck);

    auto* tabs = new QTabWidget(this);
    tabs->addTab(generalTab, QStringLiteral("General"));
    tabs->addTab(deckDisplayTab, QStringLiteral("Deck Display"));
    tabs->addTab(hotkeysTab, QStringLiteral("Hotkeys"));
    tabs->addTab(failoverTab, QStringLiteral("Failover"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    setLayout(layout);

    // Populate every widget from its persisted value BEFORE any "changed"
    // signal is connected below -- several of those handlers (most notably
    // onDeckDisplayMetricChanged(), which writes three fields together from
    // three separate widgets' CURRENT states) write straight back to
    // QSettings. Connecting first and loading after means a widget's
    // setValue()/setChecked() call here would itself fire that handler
    // while sibling widgets on the same handler haven't been populated
    // yet, silently persisting THEIR still-default (pre-load) state over
    // whatever was actually saved -- confirmed as the cause of
    // kDeckDisplayColourByState getting stuck at false (the checkbox
    // defaults unchecked at construction; the deck-display font spinboxes
    // are populated first and, if their persisted value differs from a
    // freshly-constructed spinbox's own default, fire valueChanged and
    // capture the checkbox's not-yet-loaded false).
    loadSettings();

    m_hotkeyRefreshTimer = new QTimer(this);
    m_hotkeyRefreshTimer->setInterval(kHotkeyRefreshIntervalMs);
    connect(m_hotkeyRefreshTimer, &QTimer::timeout, this, &StationSettingsDialog::refreshHotkeyTable);

    connect(m_radioNameEdit, &QLineEdit::textChanged, this, &StationSettingsDialog::onRadioNameChanged);
    connect(m_autoSizeFontsCheck, &QCheckBox::toggled, this, &StationSettingsDialog::onAutoSizeFontsToggled);
    connect(m_smallFontSizeSpin, &QSpinBox::valueChanged, this, &StationSettingsDialog::onSmallFontSizeChanged);
    connect(m_clockFontSizeSpin, &QSpinBox::valueChanged, this, &StationSettingsDialog::onClockFontSizeChanged);
    connect(m_onAirColorButton, &QPushButton::clicked, this, &StationSettingsDialog::onPickOnAirColorClicked);
    connect(m_onAirOffColorButton, &QPushButton::clicked, this, &StationSettingsDialog::onPickOnAirOffColorClicked);
    connect(m_clockColorButton, &QPushButton::clicked, this, &StationSettingsDialog::onPickClockColorClicked);
    connect(m_emergencyCartBrowseButton, &QPushButton::clicked, this, &StationSettingsDialog::onEmergencyCartBrowseClicked);
    connect(m_emergencyCartPathEdit, &QLineEdit::textChanged, this, &StationSettingsDialog::onEmergencyCartPathChanged);
    connect(m_deadAirThresholdSpin, &QDoubleSpinBox::valueChanged, this, &StationSettingsDialog::onDeadAirThresholdChanged);
    connect(m_autoFailoverCheck, &QCheckBox::toggled, this, &StationSettingsDialog::onAutoFailoverToggled);

    connect(m_deckLine1FontSpin, &QSpinBox::valueChanged, this, &StationSettingsDialog::onDeckDisplayMetricChanged);
    connect(m_deckLine2FontSpin, &QSpinBox::valueChanged, this, &StationSettingsDialog::onDeckDisplayMetricChanged);
    connect(m_deckColourByStateCheck, &QCheckBox::toggled, this, &StationSettingsDialog::onDeckDisplayMetricChanged);
}

void StationSettingsDialog::showEvent(QShowEvent* event)
{
    QDialog::showEvent(event);
    refreshHotkeyTable();
    m_hotkeyRefreshTimer->start();
}

void StationSettingsDialog::hideEvent(QHideEvent* event)
{
    QDialog::hideEvent(event);
    m_hotkeyRefreshTimer->stop();
}

void StationSettingsDialog::refreshHotkeyTable()
{
    QVector<CartClipRecord> assigned;
    for (const CartClipRecord& clip : CartRepository::allClips()) {
        if (!clip.hotkey.isEmpty())
            assigned.append(clip);
    }
    std::sort(assigned.begin(), assigned.end(), [](const CartClipRecord& a, const CartClipRecord& b) {
        return QKeySequence(a.hotkey).toString() < QKeySequence(b.hotkey).toString();
    });

    m_hotkeyTable->setRowCount(assigned.size());
    for (int row = 0; row < assigned.size(); ++row) {
        const CartClipRecord& clip = assigned.at(row);

        auto* hotkeyItem = new QTableWidgetItem(QKeySequence(clip.hotkey).toString(QKeySequence::NativeText));
        auto* labelItem = new QTableWidgetItem(clip.label.isEmpty() ? QStringLiteral("(untitled)") : clip.label);
        if (!clip.color.isEmpty()) {
            const QColor swatch(clip.color);
            hotkeyItem->setBackground(swatch);
            labelItem->setBackground(swatch);
            // Cart colors are picked freely (QColorDialog) and can be dark
            // — plain black text would be unreadable against them.
            const QColor textColor = swatch.lightnessF() < 0.5 ? QColor(Qt::white) : QColor(Qt::black);
            hotkeyItem->setForeground(textColor);
            labelItem->setForeground(textColor);
        }
        m_hotkeyTable->setItem(row, 0, hotkeyItem);
        m_hotkeyTable->setItem(row, 1, labelItem);
    }
}

void StationSettingsDialog::loadSettings()
{
    QSettings settings;
    m_radioNameEdit->setText(settings.value(kRadioName, kDefaultRadioName).toString());

    const bool autoSize = settings.value(kAutoSizeFonts, kDefaultAutoSizeFonts).toBool();
    m_autoSizeFontsCheck->setChecked(autoSize);
    m_smallFontSizeSpin->setValue(settings.value(kSmallFontPt, kDefaultSmallFontPt).toInt());
    m_clockFontSizeSpin->setValue(settings.value(kClockFontPt, kDefaultClockFontPt).toInt());
    m_smallFontSizeSpin->setEnabled(!autoSize);
    m_clockFontSizeSpin->setEnabled(!autoSize);

    updateColorButtonStyle(m_onAirColorButton, settings.value(kOnAirColor, kDefaultOnAirColor.name()).toString());
    updateColorButtonStyle(m_onAirOffColorButton, settings.value(kOnAirOffColor, kDefaultOnAirOffColor.name()).toString());
    updateColorButtonStyle(m_clockColorButton, settings.value(kClockColor, kDefaultClockColor.name()).toString());

    m_emergencyCartPathEdit->setText(settings.value(kEmergencyCartPath).toString());
    m_deadAirThresholdSpin->setValue(settings.value(kDeadAirThresholdSeconds, kDefaultDeadAirThresholdSeconds).toDouble());
    m_autoFailoverCheck->setChecked(settings.value(kAutoFailoverOnDeadAir, kDefaultAutoFailoverOnDeadAir).toBool());

    m_deckLine1FontSpin->setValue(settings.value(kDeckLine1FontPx, kDefaultDeckLine1FontPx).toInt());
    m_deckLine2FontSpin->setValue(settings.value(kDeckLine2FontPx, kDefaultDeckLine2FontPx).toInt());
    m_deckColourByStateCheck->setChecked(
        settings.value(kDeckDisplayColourByState, kDefaultDeckDisplayColourByState).toBool());
}

void StationSettingsDialog::onRadioNameChanged(const QString& text)
{
    QSettings().setValue(kRadioName, text);
    emit appearanceSettingsChanged();
}

void StationSettingsDialog::onAutoSizeFontsToggled(bool checked)
{
    QSettings().setValue(kAutoSizeFonts, checked);
    // Disabled rather than hidden when not relevant — avoids the form
    // resizing/jumping, same shape as ScheduleBlockEditorDialog's Queue
    // Size / Pick Color enable-toggling.
    m_smallFontSizeSpin->setEnabled(!checked);
    m_clockFontSizeSpin->setEnabled(!checked);
    emit appearanceSettingsChanged();
}

void StationSettingsDialog::onSmallFontSizeChanged(int value)
{
    QSettings().setValue(kSmallFontPt, value);
    emit appearanceSettingsChanged();
}

void StationSettingsDialog::onClockFontSizeChanged(int value)
{
    QSettings().setValue(kClockFontPt, value);
    emit appearanceSettingsChanged();
}

void StationSettingsDialog::onPickOnAirColorClicked()
{
    pickColor(m_onAirColorButton, kOnAirColor, QStringLiteral("On Air Color"));
}

void StationSettingsDialog::onPickOnAirOffColorClicked()
{
    pickColor(m_onAirOffColorButton, kOnAirOffColor, QStringLiteral("On Air Off Color"));
}

void StationSettingsDialog::onPickClockColorClicked()
{
    pickColor(m_clockColorButton, kClockColor, QStringLiteral("Clock Color"));
}

void StationSettingsDialog::onEmergencyCartBrowseClicked()
{
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Emergency Cart/Track"), QString(),
        QStringLiteral("Audio Files (*.mp3 *.wav *.flac *.ogg *.m4a);;All Files (*)"));
    if (!path.isEmpty())
        m_emergencyCartPathEdit->setText(path); // triggers onEmergencyCartPathChanged, which persists it
}

void StationSettingsDialog::onEmergencyCartPathChanged(const QString& text)
{
    QSettings().setValue(kEmergencyCartPath, text);
}

void StationSettingsDialog::onDeadAirThresholdChanged(double seconds)
{
    QSettings().setValue(kDeadAirThresholdSeconds, seconds);
}

void StationSettingsDialog::onAutoFailoverToggled(bool checked)
{
    QSettings().setValue(kAutoFailoverOnDeadAir, checked);
}

void StationSettingsDialog::onDeckDisplayMetricChanged()
{
    QSettings s;
    s.setValue(kDeckLine1FontPx, m_deckLine1FontSpin->value());
    s.setValue(kDeckLine2FontPx, m_deckLine2FontSpin->value());
    s.setValue(kDeckDisplayColourByState, m_deckColourByStateCheck->isChecked());
    emit deckDisplaySettingsChanged();
}

void StationSettingsDialog::pickColor(QPushButton* button, const QString& settingsKey, const QString& dialogTitle)
{
    QSettings settings;
    const QColor initial(settings.value(settingsKey, kDefaultOnAirColor.name()).toString());
    const QColor chosen = QColorDialog::getColor(initial, this, dialogTitle);
    if (!chosen.isValid())
        return;
    settings.setValue(settingsKey, chosen.name());
    updateColorButtonStyle(button, chosen.name());
    emit appearanceSettingsChanged();
}

void StationSettingsDialog::updateColorButtonStyle(QPushButton* button, const QString& colorName)
{
    button->setStyleSheet(QStringLiteral("background-color: %1;").arg(colorName));
}

} // namespace radio::ui
