#include "CartButton.h"

#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QShortcut>
#include <QTimer>

namespace radio::ui {

using radio::db::CartClipRecord;

namespace {
// 400ms — same interval as CrossfaderWidget's own Fade Now blink, for a
// consistent "this is actively happening" cadence across the app.
constexpr int kBlinkIntervalMs = 400;
const QString kBlinkOnBorder = QStringLiteral("border: 3px solid #4ade80;");
// Transparent, not absent — an absent border would shrink the button by the
// border's own width every other tick (same reasoning CrossfaderWidget's
// Fade Now button already documents for its own blink).
const QString kBlinkOffBorder = QStringLiteral("border: 3px solid transparent;");
}

CartButton::CartButton(int row, int col, QWidget* parent)
    : QPushButton(parent)
    , m_row(row)
    , m_col(col)
{
    setMinimumSize(120, 60);

    m_blinkTimer = new QTimer(this);
    m_blinkTimer->setInterval(kBlinkIntervalMs);
    connect(m_blinkTimer, &QTimer::timeout, this, &CartButton::onBlinkTick);

    setClip(std::nullopt);

    connect(this, &QPushButton::clicked, this, [this]() {
        if (m_clip)
            emit triggerRequested(m_clip->id, m_clip->filePath);
        else
            emit assignRequested(m_row, m_col);
    });
}

void CartButton::setClip(const std::optional<CartClipRecord>& clip)
{
    m_clip = clip;

    // Defensive: if this slot is reassigned while a previous clip's
    // triggered instance was still being tracked, don't leave it blinking
    // for a clip that's no longer here.
    m_blinkTimer->stop();
    m_playing = false;
    m_blinkOn = false;

    delete m_shortcut;
    m_shortcut = nullptr;

    if (m_clip) {
        setText(m_clip->label.isEmpty() ? QStringLiteral("(untitled)") : m_clip->label);
        setToolTip(m_clip->hotkey.isEmpty() ? m_clip->filePath
                                             : QStringLiteral("%1 [%2]").arg(m_clip->filePath, m_clip->hotkey));

        if (!m_clip->hotkey.isEmpty()) {
            const QKeySequence sequence(m_clip->hotkey);
            if (!sequence.isEmpty()) {
                m_shortcut = new QShortcut(sequence, this);
                m_shortcut->setContext(Qt::ApplicationShortcut);
                // Qt defaults autoRepeat to true — without this, holding
                // the key down re-fires activated() repeatedly, each one
                // triggering another overlapping cart instance with no
                // debounce (CartWallEngine::trigger() has its own cooldown
                // as defense in depth, but the real fix is not spamming in
                // the first place).
                m_shortcut->setAutoRepeat(false);
                connect(m_shortcut, &QShortcut::activated, this, &QPushButton::click);
            }
        }
    } else {
        setText(QStringLiteral("+"));
        setToolTip(QStringLiteral("Click to assign a clip"));
    }

    applyStyle();
}

void CartButton::setPlaying(bool playing)
{
    if (m_playing == playing)
        return;
    m_playing = playing;
    if (playing) {
        m_blinkOn = true;
        m_blinkTimer->start();
    } else {
        m_blinkTimer->stop();
        m_blinkOn = false;
    }
    applyStyle();
}

void CartButton::onBlinkTick()
{
    m_blinkOn = !m_blinkOn;
    applyStyle();
}

void CartButton::applyStyle()
{
    QString style;
    if (m_clip && !m_clip->color.isEmpty())
        style = QStringLiteral("background-color: %1;").arg(m_clip->color);
    if (m_playing)
        style += m_blinkOn ? kBlinkOnBorder : kBlinkOffBorder;
    setStyleSheet(style);
}

void CartButton::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::RightButton && m_clip) {
        QMenu menu(this);
        QAction* editAction = menu.addAction(QStringLiteral("Edit..."));
        QAction* removeAction = menu.addAction(QStringLiteral("Remove"));
        QAction* chosen = menu.exec(event->globalPosition().toPoint());
        if (chosen == editAction)
            emit editRequested(m_clip->id);
        else if (chosen == removeAction)
            emit removeRequested(m_clip->id);
        return;
    }
    QPushButton::mousePressEvent(event);
}

} // namespace radio::ui
