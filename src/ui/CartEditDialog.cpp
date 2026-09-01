#include "CartEditDialog.h"
#include "CartGridWidget.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

namespace radio::ui {

using radio::db::CartClipRecord;

namespace {
const QColor kDefaultColor(0x6b, 0x72, 0x80);
}

CartEditDialog::CartEditDialog(const CartClipRecord& clip, QVector<CartClipRecord> existingClips, QWidget* parent)
    : QDialog(parent)
    , m_clip(clip)
    , m_existingClips(std::move(existingClips))
    , m_color(clip.color.isEmpty() ? kDefaultColor : QColor(clip.color))
{
    setWindowTitle(clip.id < 0 ? QStringLiteral("Assign Cart Clip") : QStringLiteral("Edit Cart Clip"));
    setAttribute(Qt::WA_DeleteOnClose);

    m_labelEdit = new QLineEdit(clip.label, this);

    m_colorButton = new QPushButton(QStringLiteral("Choose…"), this);
    connect(m_colorButton, &QPushButton::clicked, this, &CartEditDialog::onPickColorClicked);
    updateColorButtonStyle();

    m_hotkeyEdit = new QLineEdit(clip.hotkey, this);
    m_hotkeyEdit->setPlaceholderText(QStringLiteral("e.g. F1, Ctrl+1 — optional"));
    connect(m_hotkeyEdit, &QLineEdit::textChanged, this, &CartEditDialog::onHotkeyEdited);

    m_hotkeyConflictLabel = new QLabel(this);
    m_hotkeyConflictLabel->setStyleSheet(QStringLiteral("color: #dc2626;"));
    m_hotkeyConflictLabel->setWordWrap(true);
    m_hotkeyConflictLabel->hide();

    m_inBetweenOnlyCheck = new QCheckBox(QStringLiteral("In-between only (never overlaid on a song)"), this);
    m_inBetweenOnlyCheck->setChecked(clip.inBetweenOnly);
    m_inBetweenOnlyCheck->setToolTip(QStringLiteral(
        "Restrict this clip to plays between songs. Schedule automation will not pick it for an overlay "
        "cart that plays on top of a track still going out — use this for jingles that carry their own music."));

    auto* form = new QFormLayout;
    form->addRow(QStringLiteral("Label"), m_labelEdit);
    form->addRow(QStringLiteral("Color"), m_colorButton);
    form->addRow(QStringLiteral("Hotkey"), m_hotkeyEdit);
    form->addRow(QStringLiteral("Usage"), m_inBetweenOnlyCheck);

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(m_hotkeyConflictLabel);
    layout->addWidget(m_buttonBox);
    setLayout(layout);

    onHotkeyEdited(m_hotkeyEdit->text()); // reflect any conflict already present in the loaded value
}

void CartEditDialog::onPickColorClicked()
{
    // A single one-off dialog, not part of a blocking chain — acceptable to
    // stay modal (see this class's own doc comment for the distinction).
    const QColor picked = QColorDialog::getColor(m_color, this, QStringLiteral("Cart Color"));
    if (picked.isValid()) {
        m_color = picked;
        updateColorButtonStyle();
    }
}

void CartEditDialog::updateColorButtonStyle()
{
    m_colorButton->setStyleSheet(QStringLiteral("background-color: %1;").arg(m_color.name()));
}

void CartEditDialog::onHotkeyEdited(const QString& text)
{
    const bool conflict = CartGridWidget::hotkeyInUse(m_existingClips, text, m_clip.id);
    if (conflict) {
        const auto it = std::find_if(m_existingClips.begin(), m_existingClips.end(), [&](const CartClipRecord& c) {
            return c.id != m_clip.id && QKeySequence(c.hotkey).toString() == QKeySequence(text).toString();
        });
        m_hotkeyConflictLabel->setText(QStringLiteral("'%1' is already bound to '%2'.")
                                            .arg(text, it != m_existingClips.end() ? it->label : QStringLiteral("another cart")));
        m_hotkeyConflictLabel->show();
    } else {
        m_hotkeyConflictLabel->hide();
    }
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(!conflict);
}

CartClipRecord CartEditDialog::result() const
{
    CartClipRecord updated = m_clip;
    updated.label = m_labelEdit->text();
    updated.color = m_color.name();
    updated.hotkey = QKeySequence(m_hotkeyEdit->text()).isEmpty() ? QString() : m_hotkeyEdit->text();
    updated.inBetweenOnly = m_inBetweenOnlyCheck->isChecked();
    return updated;
}

} // namespace radio::ui
