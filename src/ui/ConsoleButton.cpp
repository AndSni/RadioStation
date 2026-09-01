#include "ConsoleButton.h"
#include "ConsoleTheme.h"

#include <QEnterEvent>
#include <QPainter>
#include <QTimer>

#include <algorithm>

namespace radio::ui {

namespace {
constexpr int kBlinkIntervalMs = 450;
constexpr int kLightBarInsetX = 5;
constexpr int kLightBarTop = 4;
constexpr int kLightBarHeight = 4;
constexpr int kExtraHeightForBar = 8; // added to the native sizeHint so the bar never crowds the text
constexpr int kExtraWidth = 12;
} // namespace

ConsoleButton::ConsoleButton(const QString& text, QWidget* parent)
    : QPushButton(text, parent)
    , m_accent(theme::kLedRed)
{
    setCursor(Qt::PointingHandCursor);
    setFlat(true); // the painted cap is the whole button -- no native chrome underneath
}

void ConsoleButton::setLit(bool lit)
{
    if (m_lit == lit)
        return;
    m_lit = lit;
    update();
}

void ConsoleButton::setBlinking(bool blinking)
{
    if (m_blinking == blinking)
        return;
    m_blinking = blinking;
    if (blinking) {
        if (!m_blinkTimer) {
            m_blinkTimer = new QTimer(this);
            m_blinkTimer->setInterval(kBlinkIntervalMs);
            connect(m_blinkTimer, &QTimer::timeout, this, [this]() {
                m_blinkOn = !m_blinkOn;
                update();
            });
        }
        m_blinkOn = true;
        m_blinkTimer->start();
    } else if (m_blinkTimer) {
        m_blinkTimer->stop();
        m_blinkOn = false;
    }
    update();
}

void ConsoleButton::setAccent(const QColor& accent)
{
    if (m_accent == accent)
        return;
    m_accent = accent;
    update();
}

QSize ConsoleButton::sizeHint() const
{
    QSize base = QPushButton::sizeHint();
    return base + QSize(kExtraWidth, kExtraHeightForBar);
}

QSize ConsoleButton::minimumSizeHint() const
{
    QSize base = QPushButton::minimumSizeHint();
    return base + QSize(kExtraWidth, kExtraHeightForBar);
}

void ConsoleButton::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const QRectF body(rect());
    const bool down = isDown() || (isCheckable() && isChecked());
    theme::paintConsoleCap(painter, body, m_hovered && isEnabled(), down, /*horizontal=*/true);

    // A disabled button gets a dark veil over the CAP only -- drawn before
    // the light bar, so a disabled-but-blinking button (e.g. Fade Now
    // mid-crossfade) still shows a bright pulsing LED over a dimmed body.
    if (!isEnabled())
        painter.fillRect(body, QColor(0, 0, 0, 90));

    // Light bar across the top edge of the cap.
    const QRectF bar(body.left() + kLightBarInsetX, body.top() + kLightBarTop,
        body.width() - 2 * kLightBarInsetX, kLightBarHeight);
    const bool steady = m_lit || (isCheckable() && isChecked());
    // barLevel: 0 = off, 1 = full. Blinking pulses fully OFF <-> fully ON
    // (the same dark-filament look a steady-off button already has, not a
    // dimmed-but-still-lit in-between) -- a real toggle, not a flicker.
    const double barLevel = m_blinking ? (m_blinkOn ? 1.0 : 0.0) : (steady ? 1.0 : 0.0);
    const double glowLevel = m_blinking ? (m_blinkOn ? 1.0 : 0.0) : (steady ? 1.0 : 0.0);
    // Disabled dims the LED too -- but a blinking one stays mostly bright.
    const double ledDim = isEnabled() ? 1.0 : (m_blinking ? 0.82 : 0.5);

    if (glowLevel > 0.0) {
        for (int ring = 3; ring >= 1; --ring) {
            QColor glow = m_accent;
            glow.setAlpha(static_cast<int>(44.0 / ring * glowLevel * ledDim));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            const int pad = ring * 2;
            painter.drawRoundedRect(bar.adjusted(-pad, -pad, pad, pad), 2, 2);
        }
    }

    QColor barColour;
    if (barLevel > 0.0) {
        barColour = m_accent;
        barColour.setAlpha(static_cast<int>(std::min(255.0, (150.0 + 105.0 * barLevel) * ledDim)));
    } else {
        // Off: the actual accent hue at low alpha, not darker() -- darker()
        // crushes the colour itself toward black, losing which accent this
        // button even is (CUE's amber, etc.); a dim, translucent tint of
        // the real hue still reads as "a light that's off" while keeping
        // its colour identity recognisable, same as before this file's
        // blink-level rework.
        barColour = m_accent;
        barColour.setAlpha(isEnabled() ? 45 : 25);
    }
    painter.setPen(Qt::NoPen);
    painter.setBrush(barColour);
    painter.drawRoundedRect(bar, 2, 2);

    // Label.
    if (!text().isEmpty()) {
        QColor textColour = (barLevel > 0.0) ? theme::kPointerLine : theme::kCapText;
        if (!isEnabled())
            textColour = textColour.darker(150);
        painter.setPen(textColour);
        QRectF textRect = body.adjusted(6, kLightBarTop + kLightBarHeight + 1, -6, -3);
        if (isDown())
            textRect.translate(0, 1);
        painter.drawText(textRect, Qt::AlignCenter, text());
    }
}

void ConsoleButton::enterEvent(QEnterEvent* event)
{
    QPushButton::enterEvent(event);
    m_hovered = true;
    update();
}

void ConsoleButton::leaveEvent(QEvent* event)
{
    QPushButton::leaveEvent(event);
    m_hovered = false;
    update();
}

} // namespace radio::ui
