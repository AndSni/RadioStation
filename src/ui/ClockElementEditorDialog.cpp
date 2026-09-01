#include "ClockElementEditorDialog.h"

#include "db/CartRepository.h"
#include "db/PlaylistRepository.h"
#include "db/SmartPlaylistRepository.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

namespace radio::ui {

using radio::db::CartRepository;
using radio::db::ClockElementRecord;
using radio::db::PlaylistRepository;
using radio::db::SmartPlaylistRepository;

namespace {
// Index-aligned with m_elementTypeCombo's insertion order below.
const char* const kElementTypes[]
    = { "music_playlist", "music_smart_playlist", "cart_random", "cart_color", "cart_specific" };
constexpr int kElementTypeCount = 5;

int indexForElementType(const QString& elementType)
{
    for (int i = 0; i < kElementTypeCount; ++i) {
        if (elementType == QString::fromLatin1(kElementTypes[i]))
            return i;
    }
    return 0;
}

// Same small, independent duplicate of ScheduleBlockEditorDialog's own
// selection-logic table as ClockEngine.cpp/AutoDjEngine.cpp's
// metricFromString()/directionFromString() -- see those files' comments for
// why this isn't shared.
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

ClockElementEditorDialog::ClockElementEditorDialog(
    qint64 clockId, const ClockElementRecord& element, QWidget* parent)
    : QDialog(parent)
    , m_original(element)
    , m_clockId(clockId)
    , m_cartColor(element.cartColor)
{
    setWindowTitle(element.id < 0 ? QStringLiteral("New Clock Element") : QStringLiteral("Edit Clock Element"));

    m_elementTypeCombo = new QComboBox(this);
    m_elementTypeCombo->addItem(QStringLiteral("Music - Playlist"));
    m_elementTypeCombo->addItem(QStringLiteral("Music - Smart Playlist"));
    m_elementTypeCombo->addItem(QStringLiteral("Cart - Random"));
    m_elementTypeCombo->addItem(QStringLiteral("Cart - Color Sequence"));
    m_elementTypeCombo->addItem(QStringLiteral("Cart - Specific Clip"));
    m_elementTypeCombo->setCurrentIndex(indexForElementType(element.elementType));

    m_labelEdit = new QLineEdit(element.label, this);
    m_labelEdit->setPlaceholderText(QStringLiteral("(optional, shown on the queue)"));

    const bool isFlow = element.minuteOffset < 0;
    m_flowCheck = new QCheckBox(QStringLiteral("Flow (play immediately after the previous element)"), this);
    m_flowCheck->setChecked(isFlow);

    m_minuteSpin = new QSpinBox(this);
    m_minuteSpin->setRange(0, 59);
    m_minuteSpin->setSuffix(QStringLiteral(" min past the hour"));
    m_minuteSpin->setValue(isFlow ? 0 : element.minuteOffset);
    m_minuteSpin->setEnabled(!isFlow);

    m_timingModeCombo = new QComboBox(this);
    m_timingModeCombo->addItem(QStringLiteral("Soft (wait for the current song to finish)"), QStringLiteral("soft"));
    m_timingModeCombo->addItem(QStringLiteral("Hard (cut to it at this minute)"), QStringLiteral("hard"));
    const int timingModeIndex = m_timingModeCombo->findData(element.timingMode);
    m_timingModeCombo->setCurrentIndex(timingModeIndex >= 0 ? timingModeIndex : 0);
    m_timingModeCombo->setEnabled(!isFlow);

    auto* timingRow = new QHBoxLayout();
    timingRow->addWidget(m_minuteSpin);
    timingRow->addWidget(m_timingModeCombo);

    m_itemCountSpin = new QSpinBox(this);
    m_itemCountSpin->setRange(1, 50);
    m_itemCountSpin->setValue(std::max(1, element.itemCount));
    m_itemCountSpin->setToolTip(QStringLiteral("How many tracks to play from this element before the wheel advances."));

    m_playlistCombo = new QComboBox(this);
    for (const auto& playlist : PlaylistRepository::listPlaylists()) {
        m_playlistCombo->addItem(playlist.name, playlist.id);
        if (playlist.id == element.playlistId)
            m_playlistCombo->setCurrentIndex(m_playlistCombo->count() - 1);
    }

    m_smartPlaylistCombo = new QComboBox(this);
    for (const auto& smartPlaylist : SmartPlaylistRepository::allSmartPlaylists()) {
        m_smartPlaylistCombo->addItem(smartPlaylist.name, smartPlaylist.id);
        if (smartPlaylist.id == element.smartPlaylistId)
            m_smartPlaylistCombo->setCurrentIndex(m_smartPlaylistCombo->count() - 1);
    }

    auto* targetRow = new QHBoxLayout();
    targetRow->addWidget(m_playlistCombo, 1);
    targetRow->addWidget(m_smartPlaylistCombo, 1);

    m_selectionLogicCombo = new QComboBox(this);
    int selectedLogicIndex = 0;
    for (int i = 0; i < kSelectionLogicOptionCount; ++i) {
        const auto& option = kSelectionLogicOptions[i];
        m_selectionLogicCombo->addItem(QString::fromLatin1(option.label), i);
        if (element.selectionMetric == QString::fromLatin1(option.metric)
            && element.selectionDirection == QString::fromLatin1(option.direction)
            && element.selectionMode == QString::fromLatin1(option.mode))
            selectedLogicIndex = i;
    }
    m_selectionLogicCombo->setCurrentIndex(selectedLogicIndex);

    m_cartColorButton = new QPushButton(QStringLiteral("Pick Color..."), this);
    m_cartColorButton->setMinimumWidth(110);
    updateCartColorButtonStyle();

    m_cartClipCombo = new QComboBox(this);
    for (const auto& clip : CartRepository::allClips()) {
        m_cartClipCombo->addItem(clip.label, clip.id);
        if (clip.id == element.cartClipId)
            m_cartClipCombo->setCurrentIndex(m_cartClipCombo->count() - 1);
    }

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Type"), m_elementTypeCombo);
    form->addRow(QStringLiteral("Label"), m_labelEdit);
    form->addRow(QStringLiteral("Timing"), m_flowCheck);
    form->addRow(QString(), timingRow);
    form->addRow(QStringLiteral("Item Count"), m_itemCountSpin);
    form->addRow(QStringLiteral("Target"), targetRow);
    form->addRow(QStringLiteral("Track Selection"), m_selectionLogicCombo);
    form->addRow(QStringLiteral("Cart Color"), m_cartColorButton);
    form->addRow(QStringLiteral("Cart Clip"), m_cartClipCombo);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_elementTypeCombo, &QComboBox::currentIndexChanged, this, &ClockElementEditorDialog::onElementTypeChanged);
    connect(m_flowCheck, &QCheckBox::toggled, this, &ClockElementEditorDialog::onFlowToggled);
    connect(m_cartColorButton, &QPushButton::clicked, this, &ClockElementEditorDialog::onPickCartColorClicked);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    // Same fixed-size rationale as ScheduleBlockEditorDialog -- see its own
    // comment on this exact line.
    layout->setSizeConstraint(QLayout::SetFixedSize);
    setLayout(layout);

    onElementTypeChanged(); // set every type-dependent field's initial enabled state
}

void ClockElementEditorDialog::onElementTypeChanged()
{
    const QString type = QString::fromLatin1(kElementTypes[m_elementTypeCombo->currentIndex()]);
    const bool isMusicPlaylist = type == QStringLiteral("music_playlist");
    const bool isMusicSmartPlaylist = type == QStringLiteral("music_smart_playlist");
    const bool isMusic = isMusicPlaylist || isMusicSmartPlaylist;
    const bool isCartColor = type == QStringLiteral("cart_color");
    const bool isCartSpecific = type == QStringLiteral("cart_specific");

    m_itemCountSpin->setEnabled(isMusic);
    m_playlistCombo->setEnabled(isMusicPlaylist);
    m_smartPlaylistCombo->setEnabled(isMusicSmartPlaylist);
    m_selectionLogicCombo->setEnabled(isMusic);
    m_cartColorButton->setEnabled(isCartColor);
    m_cartClipCombo->setEnabled(isCartSpecific);
}

void ClockElementEditorDialog::onFlowToggled(bool checked)
{
    m_minuteSpin->setEnabled(!checked);
    m_timingModeCombo->setEnabled(!checked);
}

void ClockElementEditorDialog::onPickCartColorClicked()
{
    const QColor initial = m_cartColor.isEmpty() ? QColor(Qt::white) : QColor(m_cartColor);
    const QColor chosen = QColorDialog::getColor(initial, this, QStringLiteral("Cart Color"));
    if (!chosen.isValid())
        return;
    m_cartColor = chosen.name();
    updateCartColorButtonStyle();
}

void ClockElementEditorDialog::updateCartColorButtonStyle()
{
    // Same rationale as ScheduleBlockEditorDialog::updateCartColorButtonStyle().
    m_cartColorButton->setStyleSheet(m_cartColor.isEmpty()
            ? QStringLiteral("background-color: palette(button);")
            : QStringLiteral("background-color: %1;").arg(m_cartColor));
}

ClockElementRecord ClockElementEditorDialog::result() const
{
    ClockElementRecord record = m_original; // preserves id/position -- fields this dialog has no widget for
    record.clockId = m_clockId;
    record.elementType = QString::fromLatin1(kElementTypes[m_elementTypeCombo->currentIndex()]);
    record.label = m_labelEdit->text();

    record.minuteOffset = m_flowCheck->isChecked() ? -1 : m_minuteSpin->value();
    record.timingMode = m_timingModeCombo->currentData().toString();

    record.itemCount = m_itemCountSpin->value();

    if (record.elementType == QStringLiteral("music_smart_playlist") && m_smartPlaylistCombo->count() > 0) {
        record.smartPlaylistId = m_smartPlaylistCombo->currentData().toLongLong();
        record.playlistId = -1;
    } else if (record.elementType == QStringLiteral("music_playlist") && m_playlistCombo->count() > 0) {
        record.playlistId = m_playlistCombo->currentData().toLongLong();
        record.smartPlaylistId = -1;
    } else {
        record.playlistId = -1;
        record.smartPlaylistId = -1;
    }

    const int logicIndex = m_selectionLogicCombo->currentData().toInt();
    const auto& option = kSelectionLogicOptions[logicIndex];
    record.selectionMetric = QString::fromLatin1(option.metric);
    record.selectionDirection = QString::fromLatin1(option.direction);
    record.selectionMode = QString::fromLatin1(option.mode);

    // cartMode follows directly from which cart type was chosen -- see this
    // class's doc comment for why there's no separate cart-mode combo.
    if (record.elementType == QStringLiteral("cart_random"))
        record.cartMode = QStringLiteral("random");
    else if (record.elementType == QStringLiteral("cart_color"))
        record.cartMode = QStringLiteral("color_sequence");
    record.cartColor = m_cartColor;
    record.cartClipId
        = (record.elementType == QStringLiteral("cart_specific") && m_cartClipCombo->count() > 0)
        ? m_cartClipCombo->currentData().toLongLong()
        : -1;

    return record;
}

} // namespace radio::ui
