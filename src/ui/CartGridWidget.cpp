#include "CartGridWidget.h"
#include "CartButton.h"
#include "CartEditDialog.h"

#include "audio/AudioEngine.h"
#include "db/CartRepository.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QKeySequence>
#include <QTimer>
#include <algorithm>

namespace radio::ui {

using radio::db::CartClipRecord;
using radio::db::CartRepository;

CartGridWidget::CartGridWidget(radio::audio::AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_engine(engine)
{
    auto* layout = new QGridLayout(this);
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            auto* button = new CartButton(row, col, this);
            connect(button, &CartButton::assignRequested, this, &CartGridWidget::onAssignRequested);
            connect(button, &CartButton::editRequested, this, &CartGridWidget::onEditRequested);
            connect(button, &CartButton::removeRequested, this, &CartGridWidget::onRemoveRequested);
            connect(button, &CartButton::triggerRequested, this, &CartGridWidget::onTriggerRequested);
            layout->addWidget(button, row, col);
            m_buttons[row][col] = button;
        }
    }
    setLayout(layout);

    // 150ms — same interval CartAutomationEngine's own resume-poll timer
    // uses for the identical isCartTokenActive() check, comfortably tighter
    // than any cart clip's expected duration.
    m_cartPlaybackPollTimer = new QTimer(this);
    m_cartPlaybackPollTimer->setInterval(150);
    connect(m_cartPlaybackPollTimer, &QTimer::timeout, this, &CartGridWidget::onCartPlaybackPollTick);
    m_cartPlaybackPollTimer->start();

    refresh();
}

bool CartGridWidget::hotkeyInUse(const QVector<CartClipRecord>& clips, const QString& hotkey, qint64 excludeClipId)
{
    const QString normalized = QKeySequence(hotkey).toString();
    if (normalized.isEmpty())
        return false;
    return std::any_of(clips.begin(), clips.end(), [&](const CartClipRecord& c) {
        return c.id != excludeClipId && QKeySequence(c.hotkey).toString() == normalized;
    });
}

void CartGridWidget::refresh()
{
    for (int row = 0; row < kRows; ++row)
        for (int col = 0; col < kCols; ++col)
            m_buttons[row][col]->setClip(std::nullopt);

    for (const auto& clip : CartRepository::allClips()) {
        if (clip.slotRow >= 0 && clip.slotRow < kRows && clip.slotCol >= 0 && clip.slotCol < kCols)
            m_buttons[clip.slotRow][clip.slotCol]->setClip(clip);
    }
}

void CartGridWidget::onAssignRequested(int row, int col)
{
    // The one blocking call left in this flow: a short, singular native
    // file browse, before any cart with a hotkey to protect even exists —
    // not part of the risky chain the rest of this used to be (see
    // CartEditDialog's doc comment).
    const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Assign Cart Clip"), QString(),
        QStringLiteral("Audio Files (*.mp3 *.wav *.flac *.ogg *.m4a);;All Files (*)"));
    if (path.isEmpty())
        return;

    CartClipRecord clip;
    clip.filePath = path;
    clip.label = QFileInfo(path).completeBaseName();
    clip.slotRow = row;
    clip.slotCol = col;
    openEditor(clip);
}

void CartGridWidget::onEditRequested(qint64 clipId)
{
    const auto clips = CartRepository::allClips();
    auto it = std::find_if(clips.begin(), clips.end(), [clipId](const CartClipRecord& c) { return c.id == clipId; });
    if (it == clips.end())
        return;
    openEditor(*it);
}

void CartGridWidget::openEditor(const CartClipRecord& clip)
{
    if (m_openEditor) {
        m_openEditor->raise();
        m_openEditor->activateWindow();
        return;
    }

    auto* dialog = new CartEditDialog(clip, CartRepository::allClips(), this);
    m_openEditor = dialog;
    connect(dialog, &CartEditDialog::accepted, this, [this, dialog]() {
        const CartClipRecord result = dialog->result();
        if (result.id < 0)
            CartRepository::addClip(result.filePath, result.label, result.color, result.slotRow, result.slotCol,
                result.hotkey, result.durationMs, result.inBetweenOnly);
        else
            CartRepository::updateClip(result);
        refresh();
    });
    // WA_DeleteOnClose (set in CartEditDialog's constructor) only covers the
    // window-manager close path (the X button) -- QDialog::done(), which
    // accept()/reject() both funnel through, calls hide() rather than
    // close(), so it never sends the QCloseEvent WA_DeleteOnClose reacts
    // to. finished() fires for accept, reject, AND the X button alike, so
    // this is the one place that reliably cleans up every path.
    connect(dialog, &CartEditDialog::finished, dialog, &QObject::deleteLater);
    dialog->show();
}

void CartGridWidget::onRemoveRequested(qint64 clipId)
{
    CartRepository::removeClip(clipId);
    refresh();
}

void CartGridWidget::onTriggerRequested(qint64 clipId, const QString& filePath)
{
    const QString token = m_engine->triggerCart(filePath);
    if (CartButton* button = buttonForClip(clipId))
        trackTriggeredToken(button, token);
}

void CartGridWidget::onExternalCartTriggered(qint64 clipId, const QString& token)
{
    if (CartButton* button = buttonForClip(clipId))
        trackTriggeredToken(button, token);
}

CartButton* CartGridWidget::buttonForClip(qint64 clipId) const
{
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            if (m_buttons[row][col]->clip() && m_buttons[row][col]->clip()->id == clipId)
                return m_buttons[row][col];
        }
    }
    return nullptr;
}

void CartGridWidget::trackTriggeredToken(CartButton* button, const QString& token)
{
    if (token.isEmpty())
        return;
    m_activeTokensByButton[button].append(token);
    button->setPlaying(true);
}

void CartGridWidget::onCartPlaybackPollTick()
{
    for (auto it = m_activeTokensByButton.begin(); it != m_activeTokensByButton.end();) {
        QVector<QString>& tokens = it.value();
        tokens.erase(std::remove_if(tokens.begin(), tokens.end(),
                         [this](const QString& token) { return !m_engine->isCartTokenActive(token); }),
            tokens.end());
        if (tokens.isEmpty()) {
            it.key()->setPlaying(false);
            it = m_activeTokensByButton.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace radio::ui
