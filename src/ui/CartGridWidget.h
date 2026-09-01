#pragma once

#include <QHash>
#include <QPointer>
#include <QVector>
#include <QWidget>

#include "db/CartRepository.h"

class QTimer;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

class CartButton;
class CartEditDialog;

// A grid of hotkey-triggerable jingle/sound-drop slots (the "cart wall").
// Fixed 4x4 grid for v1 — empty slots show "+" and prompt for a clip when
// clicked; loaded slots trigger playback (overlapping retriggers, not
// restarting — see CartWallEngine) and can be right-clicked to edit/remove.
class CartGridWidget : public QWidget {
    Q_OBJECT

public:
    CartGridWidget(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

    // Exposed for standalone unit testing without any dialogs. Returns true
    // if `hotkey` (as a normalized QKeySequence) is already bound to a clip
    // other than excludeClipId.
    static bool hotkeyInUse(
        const QVector<radio::db::CartClipRecord>& clips, const QString& hotkey, qint64 excludeClipId = -1);

public slots:
    void refresh();

    // A cart clip was triggered somewhere OTHER than this widget's own
    // buttons — currently, schedule-driven Cart Automation (see
    // CartAutomationEngine::cartTriggered()). Makes the matching button
    // blink exactly the same as a direct click would, so an operator can
    // see an unattended cart fire even if they didn't hear it.
    void onExternalCartTriggered(qint64 clipId, const QString& token);

private slots:
    void onAssignRequested(int row, int col);
    void onEditRequested(qint64 clipId);
    void onRemoveRequested(qint64 clipId);
    void onTriggerRequested(qint64 clipId, const QString& filePath);
    void onCartPlaybackPollTick();

private:
    static constexpr int kRows = 4;
    static constexpr int kCols = 4;

    // Opens m_openEditor non-modally for `clip` (id < 0 means a new clip
    // for an empty slot) — shared by onAssignRequested()/onEditRequested().
    // Only one editor at a time: a second assign/edit request while one is
    // already open just raises the existing dialog instead of opening a
    // duplicate (mirroring AutoDjPanelWidget's identical guard for
    // ScheduleBlockEditorDialog).
    void openEditor(const radio::db::CartClipRecord& clip);

    // Returns the button currently assigned clipId, or nullptr if none
    // (e.g. it was reassigned/removed between the trigger and this lookup).
    CartButton* buttonForClip(qint64 clipId) const;

    // Starts tracking `token` as belonging to `button`'s clip, and makes it
    // blink if it isn't already — shared by both onTriggerRequested() (this
    // widget's own buttons) and onExternalCartTriggered().
    void trackTriggeredToken(CartButton* button, const QString& token);

    radio::audio::AudioEngine* m_engine;
    CartButton* m_buttons[kRows][kCols] = {};

    // A button keeps blinking as long as ANY of its tracked tokens is still
    // playing — cart clips retrigger as overlapping instances rather than
    // restarting (see CartWallEngine's own doc comment), so a single
    // click-while-already-playing must not lose track of the still-running
    // earlier instance.
    QHash<CartButton*, QVector<QString>> m_activeTokensByButton;
    QTimer* m_cartPlaybackPollTimer;

    QPointer<CartEditDialog> m_openEditor;
};

} // namespace radio::ui
