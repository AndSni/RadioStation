#pragma once

#include "db/CartRepository.h"

#include <QPushButton>
#include <optional>

class QShortcut;
class QTimer;

namespace radio::ui {

// A single cart-wall slot: empty ("+", click to assign a clip) or loaded
// (shows label/color, click triggers playback, right-click edits/removes).
class CartButton : public QPushButton {
    Q_OBJECT

public:
    CartButton(int row, int col, QWidget* parent = nullptr);

    void setClip(const std::optional<radio::db::CartClipRecord>& clip);
    const std::optional<radio::db::CartClipRecord>& clip() const { return m_clip; }

    int row() const { return m_row; }
    int col() const { return m_col; }

    // Starts/stops this button blinking (see onBlinkTick()) — CartGridWidget
    // calls this while at least one triggered instance of this button's
    // clip is still audibly playing, whether triggered by a click, a
    // hotkey, or schedule-driven Cart Automation. No-op if already in the
    // requested state.
    void setPlaying(bool playing);
    bool isPlaying() const { return m_playing; }

signals:
    void triggerRequested(qint64 clipId, const QString& filePath);
    void assignRequested(int row, int col); // empty slot clicked
    void editRequested(qint64 clipId);
    void removeRequested(qint64 clipId);

protected:
    void mousePressEvent(QMouseEvent* event) override;

private slots:
    void onBlinkTick();

private:
    void applyStyle();

    int m_row;
    int m_col;
    std::optional<radio::db::CartClipRecord> m_clip;
    QShortcut* m_shortcut = nullptr;

    bool m_playing = false;
    bool m_blinkOn = false;
    QTimer* m_blinkTimer;
};

} // namespace radio::ui
