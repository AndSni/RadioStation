#include "ScheduleBlockEditorDialog.h"
#include "UiStyleHelpers.h"

#include "db/ClockRepository.h"
#include "db/PlaylistRepository.h"
#include "db/SmartPlaylistRepository.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTime>
#include <QTimeEdit>
#include <QVBoxLayout>

namespace radio::ui {

using radio::db::PlaylistRepository;
using radio::db::ScheduleBlockRecord;
using radio::db::SmartPlaylistRepository;

namespace {
const char* const kDayLabels[7] = { "M", "T", "W", "T", "F", "S", "Su" };

struct SelectionLogicOption {
    const char* label;
    const char* metric;
    const char* direction;
    const char* mode;
};
const SelectionLogicOption kSelectionLogicOptions[] = {
    { "Random", "none", "desc", "random_pool" },
    { "Highest rated (A-Z)", "rating", "desc", "deterministic" },
    { "Highest random", "rating", "desc", "random_pool" },
    { "Most played (A-Z)", "play_count", "desc", "deterministic" },
    { "Most played random", "play_count", "desc", "random_pool" },
    { "Least played (A-Z)", "play_count", "asc", "deterministic" },
    { "Least played random", "play_count", "asc", "random_pool" },
};
constexpr int kSelectionLogicOptionCount = 7;
}

ScheduleBlockEditorDialog::ScheduleBlockEditorDialog(const ScheduleBlockRecord& block, QWidget* parent)
    : QDialog(parent)
    , m_original(block)
    , m_cartColor(block.cartColor)
{
    setWindowTitle(block.id < 0 ? QStringLiteral("New Schedule Block") : QStringLiteral("Edit Schedule Block"));

    m_nameEdit = new QLineEdit(block.name, this);

    auto* dayRow = new QHBoxLayout();
    for (int i = 0; i < 7; ++i) {
        m_dayButtons[i] = new QPushButton(QString::fromLatin1(kDayLabels[i]), this);
        m_dayButtons[i]->setCheckable(true);
        m_dayButtons[i]->setChecked((block.daysMask & (1 << i)) != 0);
        m_dayButtons[i]->setStyleSheet(checkableActiveButtonStyle());
        // Fixed, not just sized-to-fit: checkableActiveButtonStyle() only
        // styles the :checked state, leaving :unchecked to native
        // rendering — Qt computes a genuinely different box model (and
        // sizeHint) for a styled vs. unstyled button, so a block with every
        // day checked would otherwise make this whole row (and the dialog
        // around it) a different natural size than one with none checked.
        // This is exactly why "Edit" (real, checked data) and "New" (empty,
        // unchecked defaults) could size differently even after locking the
        // dialog itself to SetFixedSize.
        m_dayButtons[i]->setFixedSize(36, 28);
        dayRow->addWidget(m_dayButtons[i]);
    }

    const bool isAllDay = block.startMinute == 0 && block.endMinute == 1440;

    m_allDayCheck = new QCheckBox(QStringLiteral("All Day"), this);
    m_allDayCheck->setChecked(isAllDay);

    m_startTimeEdit = new QTimeEdit(this);
    m_startTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    m_startTimeEdit->setTime(QTime(0, 0).addSecs((isAllDay ? 0 : block.startMinute) * 60));
    m_startTimeEdit->setEnabled(!isAllDay);

    m_endTimeEdit = new QTimeEdit(this);
    m_endTimeEdit->setDisplayFormat(QStringLiteral("HH:mm"));
    // QTimeEdit can't represent 24:00 - while All Day is checked the
    // on-screen value here is cosmetic only; result() forces (0, 1440)
    // regardless of what's displayed.
    m_endTimeEdit->setTime(QTime(0, 0).addSecs((isAllDay ? 0 : block.endMinute % 1440) * 60));
    m_endTimeEdit->setEnabled(!isAllDay);

    auto* timeRow = new QHBoxLayout();
    timeRow->addWidget(m_startTimeEdit);
    timeRow->addWidget(new QLabel(QStringLiteral("to"), this));
    timeRow->addWidget(m_endTimeEdit);
    timeRow->addWidget(m_allDayCheck);

    m_targetTypeCombo = new QComboBox(this);
    m_targetTypeCombo->addItem(QStringLiteral("Static Playlist"), 0);
    m_targetTypeCombo->addItem(QStringLiteral("Smart Playlist"), 1);
    m_targetTypeCombo->addItem(QStringLiteral("Clock"), 2);

    m_playlistCombo = new QComboBox(this);
    const auto playlists = PlaylistRepository::listPlaylists();
    for (const auto& playlist : playlists) {
        m_playlistCombo->addItem(playlist.name, playlist.id);
        if (playlist.id == block.playlistId)
            m_playlistCombo->setCurrentIndex(m_playlistCombo->count() - 1);
    }

    m_smartPlaylistCombo = new QComboBox(this);
    const auto smartPlaylists = SmartPlaylistRepository::allSmartPlaylists();
    for (const auto& smartPlaylist : smartPlaylists) {
        m_smartPlaylistCombo->addItem(smartPlaylist.name, smartPlaylist.id);
        if (smartPlaylist.id == block.smartPlaylistId)
            m_smartPlaylistCombo->setCurrentIndex(m_smartPlaylistCombo->count() - 1);
    }

    m_clockCombo = new QComboBox(this);
    const auto clocks = radio::db::ClockRepository::allClocks();
    for (const auto& clock : clocks) {
        m_clockCombo->addItem(clock.name, clock.id);
        if (clock.id == block.clockId)
            m_clockCombo->setCurrentIndex(m_clockCombo->count() - 1);
    }

    m_targetTypeCombo->setCurrentIndex(block.clockId >= 0 ? 2 : (block.smartPlaylistId >= 0 ? 1 : 0));

    auto* targetRow = new QHBoxLayout();
    targetRow->addWidget(m_targetTypeCombo);
    targetRow->addWidget(m_playlistCombo, 1);
    targetRow->addWidget(m_smartPlaylistCombo, 1);
    targetRow->addWidget(m_clockCombo, 1);

    m_selectionLogicCombo = new QComboBox(this);
    int selectedLogicIndex = 0;
    for (int i = 0; i < kSelectionLogicOptionCount; ++i) {
        const auto& option = kSelectionLogicOptions[i];
        m_selectionLogicCombo->addItem(QString::fromLatin1(option.label), i);
        if (block.selectionMetric == QString::fromLatin1(option.metric)
            && block.selectionDirection == QString::fromLatin1(option.direction)
            && block.selectionMode == QString::fromLatin1(option.mode))
            selectedLogicIndex = i;
    }
    m_selectionLogicCombo->setCurrentIndex(selectedLogicIndex);

    m_queueModeCombo = new QComboBox(this);
    m_queueModeCombo->addItem(QStringLiteral("Count"), QStringLiteral("count"));
    m_queueModeCombo->addItem(QStringLiteral("Queue Remaining"), QStringLiteral("remaining"));
    m_queueModeCombo->setItemData(
        0, QStringLiteral("Keep a fixed number of tracks queued ahead."), Qt::ToolTipRole);
    m_queueModeCombo->setItemData(1,
        QStringLiteral("Queue exactly enough tracks, by duration, to cover however much time is left in this block."),
        Qt::ToolTipRole);
    const int queueModeIndex = m_queueModeCombo->findData(block.queueMode);
    m_queueModeCombo->setCurrentIndex(queueModeIndex >= 0 ? queueModeIndex : 0);

    m_queueSizeSpin = new QSpinBox(this);
    m_queueSizeSpin->setRange(0, 50);
    m_queueSizeSpin->setValue(block.queueSize);
    m_queueSizeSpin->setToolTip(QStringLiteral("0 = use the app's default queue depth."));

    auto* queueModeRow = new QHBoxLayout();
    queueModeRow->addWidget(m_queueModeCombo);
    queueModeRow->addWidget(m_queueSizeSpin);

    m_cartFrequencySpin = new QSpinBox(this);
    m_cartFrequencySpin->setRange(0, 100);
    m_cartFrequencySpin->setValue(block.cartFrequency);
    m_cartFrequencySpin->setToolTip(QStringLiteral("0 = disabled. Otherwise, fire a cart after every Nth song played in this block."));

    m_cartModeCombo = new QComboBox(this);
    m_cartModeCombo->addItem(QStringLiteral("Random"), QStringLiteral("random"));
    m_cartModeCombo->addItem(QStringLiteral("Sequence"), QStringLiteral("sequence"));
    m_cartModeCombo->addItem(QStringLiteral("Color Sequence"), QStringLiteral("color_sequence"));
    const int cartModeIndex = m_cartModeCombo->findData(block.cartMode);
    m_cartModeCombo->setCurrentIndex(cartModeIndex >= 0 ? cartModeIndex : 0);

    m_cartColorButton = new QPushButton(QStringLiteral("Pick Color..."), this);
    // Fixed width for the same reason the day buttons are fixed-size above
    // — updateCartColorButtonStyle() always applies SOME stylesheet now,
    // but pin the width explicitly too rather than relying on that alone.
    m_cartColorButton->setMinimumWidth(110);
    updateCartColorButtonStyle();

    auto* cartModeRow = new QHBoxLayout();
    cartModeRow->addWidget(m_cartModeCombo);
    cartModeRow->addWidget(m_cartColorButton);

    m_cartOffsetSpin = new QSpinBox(this);
    m_cartOffsetSpin->setRange(0, 300);
    m_cartOffsetSpin->setSuffix(QStringLiteral(" s"));
    m_cartOffsetSpin->setValue(block.cartOffsetSeconds);
    m_cartOffsetSpin->setToolTip(QStringLiteral("How many seconds after a deck's automatic crossfade finishes to fire the cart."));

    m_cartPlaybackTypeCombo = new QComboBox(this);
    m_cartPlaybackTypeCombo->addItem(QStringLiteral("Overlay"), QStringLiteral("overlay"));
    m_cartPlaybackTypeCombo->addItem(QStringLiteral("In Between"), QStringLiteral("insert"));
    m_cartPlaybackTypeCombo->setItemData(0,
        QStringLiteral("The cart mixes in alongside whatever's already playing on the deck."), Qt::ToolTipRole);
    m_cartPlaybackTypeCombo->setItemData(1,
        QStringLiteral("The deck pauses, the cart plays by itself, then the deck resumes where it left off."),
        Qt::ToolTipRole);
    const int cartPlaybackTypeIndex = m_cartPlaybackTypeCombo->findData(block.cartPlaybackType);
    m_cartPlaybackTypeCombo->setCurrentIndex(cartPlaybackTypeIndex >= 0 ? cartPlaybackTypeIndex : 0);

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Name"), m_nameEdit);
    form->addRow(QStringLiteral("Days"), dayRow);
    form->addRow(QStringLiteral("Time"), timeRow);
    form->addRow(QStringLiteral("Target"), targetRow);
    form->addRow(QStringLiteral("Track Selection"), m_selectionLogicCombo);
    form->addRow(QStringLiteral("Queue Mode"), queueModeRow);
    form->addRow(QStringLiteral("Cart Frequency"), m_cartFrequencySpin);
    form->addRow(QStringLiteral("Cart Mode"), cartModeRow);
    form->addRow(QStringLiteral("Cart Type"), m_cartPlaybackTypeCombo);
    form->addRow(QStringLiteral("Cart Offset"), m_cartOffsetSpin);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_allDayCheck, &QCheckBox::toggled, this, &ScheduleBlockEditorDialog::onAllDayToggled);
    connect(m_cartModeCombo, &QComboBox::currentIndexChanged, this, &ScheduleBlockEditorDialog::onCartModeChanged);
    connect(m_cartColorButton, &QPushButton::clicked, this, &ScheduleBlockEditorDialog::onPickCartColorClicked);
    connect(m_queueModeCombo, &QComboBox::currentIndexChanged, this, &ScheduleBlockEditorDialog::onQueueModeChanged);
    connect(m_targetTypeCombo, &QComboBox::currentIndexChanged, this, &ScheduleBlockEditorDialog::onTargetTypeChanged);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    // Locks this dialog to its own content-driven size — without this, a
    // window manager that remembers per-title geometry (common on KDE) can
    // reapply a stale size to one of "New Schedule Block"/"Edit Schedule
    // Block" (they're different titles, so different remembered geometries)
    // if either was ever manually resized once, clipping the content. This
    // form has nothing that benefits from being resizable anyway.
    layout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(layout);

    onCartModeChanged(); // set the color button's initial enabled state to match the pre-selected mode
    onQueueModeChanged(); // set the queue-size spin box's initial enabled state to match the pre-selected mode
    onTargetTypeChanged(); // set the playlist/smart-playlist combos' initial enabled state to match the pre-selected type
}

void ScheduleBlockEditorDialog::onAllDayToggled(bool checked)
{
    m_startTimeEdit->setEnabled(!checked);
    m_endTimeEdit->setEnabled(!checked);
}

void ScheduleBlockEditorDialog::onCartModeChanged()
{
    // Disabled rather than hidden when not relevant — avoids the form
    // resizing/jumping as the mode selection changes.
    m_cartColorButton->setEnabled(m_cartModeCombo->currentData().toString() == QStringLiteral("color_sequence"));
}

void ScheduleBlockEditorDialog::onQueueModeChanged()
{
    // Queue Size only means anything in "Count" mode — disabled (not
    // hidden) in "Queue Remaining" mode, same shape as Cart Mode's own
    // Pick Color handling above.
    m_queueSizeSpin->setEnabled(m_queueModeCombo->currentData().toString() == QStringLiteral("count"));
}

void ScheduleBlockEditorDialog::onTargetTypeChanged()
{
    // Disabled rather than hidden, matching Cart Mode/Queue Mode's own
    // enable-toggling above -- keeps the row's width (and this
    // SetFixedSize dialog's overall size) stable regardless of target type.
    const int targetType = m_targetTypeCombo->currentData().toInt();
    const bool wantsSmartPlaylist = targetType == 1;
    const bool wantsClock = targetType == 2;
    m_playlistCombo->setEnabled(!wantsSmartPlaylist && !wantsClock);
    m_smartPlaylistCombo->setEnabled(wantsSmartPlaylist);
    m_clockCombo->setEnabled(wantsClock);

    // A clock target drives its own per-element track selection, cart
    // firing, and queue depth (see ClockEngine) -- these fields become
    // simply ignored, so disable them rather than leave live-looking
    // controls that do nothing.
    m_selectionLogicCombo->setEnabled(!wantsClock);
    m_queueModeCombo->setEnabled(!wantsClock);
    m_queueSizeSpin->setEnabled(!wantsClock && m_queueModeCombo->currentData().toString() == QStringLiteral("count"));
    m_cartFrequencySpin->setEnabled(!wantsClock);
    m_cartModeCombo->setEnabled(!wantsClock);
    m_cartColorButton->setEnabled(!wantsClock && m_cartModeCombo->currentData().toString() == QStringLiteral("color_sequence"));
    m_cartOffsetSpin->setEnabled(!wantsClock);
    m_cartPlaybackTypeCombo->setEnabled(!wantsClock);
}

void ScheduleBlockEditorDialog::onPickCartColorClicked()
{
    const QColor initial = m_cartColor.isEmpty() ? QColor(Qt::white) : QColor(m_cartColor);
    const QColor chosen = QColorDialog::getColor(initial, this, QStringLiteral("Cart Color"));
    if (!chosen.isValid())
        return;
    m_cartColor = chosen.name();
    updateCartColorButtonStyle();
}

void ScheduleBlockEditorDialog::updateCartColorButtonStyle()
{
    // Always applies SOME stylesheet (never an empty one) — a styled vs.
    // unstyled QPushButton renders with a genuinely different box model in
    // Qt, which was quietly making this whole dialog compute a different
    // natural size depending on whether a cart color happened to be set
    // (see the day buttons' setFixedSize() comment above for the same root
    // cause). palette(button) keeps the neutral case looking native/theme-
    // correct rather than guessing a hardcoded color.
    m_cartColorButton->setStyleSheet(m_cartColor.isEmpty()
            ? QStringLiteral("background-color: palette(button);")
            : QStringLiteral("background-color: %1;").arg(m_cartColor));
}

ScheduleBlockRecord ScheduleBlockEditorDialog::result() const
{
    ScheduleBlockRecord record = m_original; // preserves id/position/enabled - fields this dialog has no widget for
    record.name = m_nameEdit->text();

    int mask = 0;
    for (int i = 0; i < 7; ++i) {
        if (m_dayButtons[i]->isChecked())
            mask |= (1 << i);
    }
    record.daysMask = mask;

    if (m_allDayCheck->isChecked()) {
        record.startMinute = 0;
        record.endMinute = 1440;
    } else {
        const QTime start = m_startTimeEdit->time();
        const QTime end = m_endTimeEdit->time();
        record.startMinute = start.hour() * 60 + start.minute();
        record.endMinute = end.hour() * 60 + end.minute();
    }

    // At most one of the three is set, matching TrackRecord.h's own
    // "clockId >= 0 beats smartPlaylistId >= 0 beats playlistId" mutual-
    // exclusion convention -- an empty clock/smart-playlist list falls back
    // to static even if that type is selected, since there's nothing valid
    // to store.
    const int targetType = m_targetTypeCombo->currentData().toInt();
    const bool wantsClock = targetType == 2 && m_clockCombo->count() > 0;
    const bool wantsSmartPlaylist = !wantsClock && targetType == 1 && m_smartPlaylistCombo->count() > 0;
    if (wantsClock) {
        record.clockId = m_clockCombo->currentData().toLongLong();
        record.smartPlaylistId = -1;
        record.playlistId = -1;
    } else if (wantsSmartPlaylist) {
        record.clockId = -1;
        record.smartPlaylistId = m_smartPlaylistCombo->currentData().toLongLong();
        record.playlistId = -1;
    } else {
        record.clockId = -1;
        record.playlistId = m_playlistCombo->currentData().toLongLong();
        record.smartPlaylistId = -1;
    }

    const int logicIndex = m_selectionLogicCombo->currentData().toInt();
    const auto& option = kSelectionLogicOptions[logicIndex];
    record.selectionMetric = QString::fromLatin1(option.metric);
    record.selectionDirection = QString::fromLatin1(option.direction);
    record.selectionMode = QString::fromLatin1(option.mode);

    record.queueMode = m_queueModeCombo->currentData().toString();
    record.queueSize = m_queueSizeSpin->value();

    record.cartFrequency = m_cartFrequencySpin->value();
    record.cartMode = m_cartModeCombo->currentData().toString();
    record.cartColor = m_cartColor;
    record.cartOffsetSeconds = m_cartOffsetSpin->value();
    record.cartPlaybackType = m_cartPlaybackTypeCombo->currentData().toString();

    return record;
}

} // namespace radio::ui
