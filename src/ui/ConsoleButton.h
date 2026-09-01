#pragma once

#include <QColor>
#include <QPushButton>

class QTimer;

namespace radio::ui {

// A rectangular DJ-console button: a hand-painted metal cap
// (ConsoleTheme::paintConsoleCap()) with the label on top and a thin
// "light bar" across its upper edge that glows when the button is engaged --
// the same visual grammar as a Pioneer/hardware transport button, where the
// button itself shows its state rather than a separate label doing it.
//
// Same house style as RoundButton / ConsoleFader: everything is drawn in
// paintEvent(), no QSS. It IS a QPushButton -- checkable or momentary per
// the caller, every signal/slot unchanged -- so it drops in wherever a
// text QPushButton was, and findChild<QPushButton*>()/text() keep working.
//
// The light bar lights when: the button is checkable and :checked, OR
// setLit(true) was called (for a momentary button whose "on" state is
// driven by something external, e.g. a deck's Playing state), OR
// setBlinking(true) is active (a slow pulse -- the deck CUE button uses
// this for "track loaded, not yet played").
class ConsoleButton : public QPushButton {
    Q_OBJECT

public:
    explicit ConsoleButton(const QString& text = QString(), QWidget* parent = nullptr);

    // Explicit lit state, independent of checkable/checked. No-op repaint
    // guard, like the rest of this app's painted widgets.
    void setLit(bool lit);
    bool isLit() const { return m_lit; }

    // Slow pulse of the light bar (~450 ms). While blinking, the bar
    // overrides both setLit() and :checked. Stopping it returns the bar to
    // whatever those say.
    void setBlinking(bool blinking);
    bool isBlinking() const { return m_blinking; }

    // The lit/blink colour. Defaults to ConsoleTheme::kLedRed (this app's
    // "engaged / live" colour).
    void setAccent(const QColor& accent);

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;

private:
    QColor m_accent;
    bool m_lit = false;
    bool m_blinking = false;
    bool m_blinkOn = false;
    bool m_hovered = false;
    QTimer* m_blinkTimer = nullptr;
};

} // namespace radio::ui
