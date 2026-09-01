#include "ui/CartButton.h"
#include "ui/CartGridWidget.h"

#include "db/CartRepository.h"

#include <QShortcut>
#include <QTest>

using namespace radio::ui;
using radio::db::CartClipRecord;

class CartButtonTest : public QObject {
    Q_OBJECT

private slots:
    void hotkeyShortcutHasAutoRepeatDisabled();
    void hotkeyInUseDetectsCollisionAcrossSlots();
    void hotkeyInUseExcludesSelfOnEdit();
    void hotkeyInUseIgnoresEmptyHotkey();
    void setPlayingTogglesIsPlaying();
    void setPlayingTogglesStyleOverTime();
    void setClipResetsPlayingState();
};

void CartButtonTest::hotkeyShortcutHasAutoRepeatDisabled()
{
    CartButton button(0, 0);

    CartClipRecord clip;
    clip.id = 1;
    clip.label = QStringLiteral("Jingle");
    clip.filePath = QStringLiteral("/tmp/jingle.wav");
    clip.hotkey = QStringLiteral("F1");
    button.setClip(clip);

    auto* shortcut = button.findChild<QShortcut*>();
    QVERIFY(shortcut != nullptr);
    // Qt defaults autoRepeat to true — holding the key down would otherwise
    // re-fire activated() repeatedly, spawning an unbounded stack of
    // overlapping cart instances of the same clip. This is the actual
    // confirmed root cause of the "10 songs at once" bug.
    QVERIFY(!shortcut->autoRepeat());
}

void CartButtonTest::hotkeyInUseDetectsCollisionAcrossSlots()
{
    QVector<CartClipRecord> clips;
    CartClipRecord a;
    a.id = 1;
    a.hotkey = QStringLiteral("F1");
    clips.append(a);

    CartClipRecord b;
    b.id = 2;
    b.hotkey = QStringLiteral("Ctrl+1");
    clips.append(b);

    QVERIFY(CartGridWidget::hotkeyInUse(clips, QStringLiteral("F1")));
    QVERIFY(CartGridWidget::hotkeyInUse(clips, QStringLiteral("Ctrl+1")));
    QVERIFY(!CartGridWidget::hotkeyInUse(clips, QStringLiteral("F2")));
}

void CartButtonTest::hotkeyInUseExcludesSelfOnEdit()
{
    QVector<CartClipRecord> clips;
    CartClipRecord a;
    a.id = 1;
    a.hotkey = QStringLiteral("F1");
    clips.append(a);

    // Editing clip 1 and keeping its own existing hotkey must not report a
    // collision with itself.
    QVERIFY(!CartGridWidget::hotkeyInUse(clips, QStringLiteral("F1"), /*excludeClipId=*/1));
    // But a DIFFERENT clip claiming that same hotkey is still a collision.
    QVERIFY(CartGridWidget::hotkeyInUse(clips, QStringLiteral("F1"), /*excludeClipId=*/2));
}

void CartButtonTest::hotkeyInUseIgnoresEmptyHotkey()
{
    QVector<CartClipRecord> clips;
    CartClipRecord a;
    a.id = 1;
    a.hotkey = QString();
    clips.append(a);

    // Two carts with no hotkey assigned isn't a collision.
    QVERIFY(!CartGridWidget::hotkeyInUse(clips, QString()));
}

void CartButtonTest::setPlayingTogglesIsPlaying()
{
    CartButton button(0, 0);
    CartClipRecord clip;
    clip.id = 1;
    clip.filePath = QStringLiteral("/tmp/jingle.wav");
    button.setClip(clip);

    QVERIFY(!button.isPlaying());
    button.setPlaying(true);
    QVERIFY(button.isPlaying());
    // A border is applied the instant blinking starts (blink-on) — style
    // must actually change, not just the isPlaying() flag.
    QVERIFY(!button.styleSheet().isEmpty());

    button.setPlaying(false);
    QVERIFY(!button.isPlaying());
}

void CartButtonTest::setPlayingTogglesStyleOverTime()
{
    CartButton button(0, 0);
    CartClipRecord clip;
    clip.id = 1;
    clip.filePath = QStringLiteral("/tmp/jingle.wav");
    button.setClip(clip);

    button.setPlaying(true);
    const QString firstStyle = button.styleSheet();
    QTest::qWait(500); // longer than the 400ms blink interval — at least one tick must have fired
    const QString secondStyle = button.styleSheet();
    QVERIFY2(firstStyle != secondStyle,
        qPrintable(QStringLiteral("expected the blink tick to change the button's style; stayed '%1'").arg(firstStyle)));

    button.setPlaying(false);
}

void CartButtonTest::setClipResetsPlayingState()
{
    CartButton button(0, 0);
    CartClipRecord clip;
    clip.id = 1;
    clip.filePath = QStringLiteral("/tmp/jingle.wav");
    button.setClip(clip);
    button.setPlaying(true);
    QVERIFY(button.isPlaying());

    // Reassigning the slot (even to the same clip) must not leave a stale
    // blink running for whatever was here before.
    button.setClip(clip);
    QVERIFY(!button.isPlaying());
}

QTEST_MAIN(CartButtonTest)
#include "CartButtonTest.moc"
