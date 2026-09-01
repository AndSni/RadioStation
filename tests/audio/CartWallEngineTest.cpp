#include "audio/AudioEngine.h"

#include <QTest>

using namespace radio::audio;

class CartWallEngineTest : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    void cartOverlaysWithoutInterruptingActiveDeck();
    void retriggerOverlapsRatherThanRestarting();
    void triggerCartTokenTracksThatSpecificInstance();
    void cartTriggersNormallyWhenDeckAHasNonDefaultEq();
    void cartThatErrorsSelfCleansInsteadOfStayingActiveForever();
};

void CartWallEngineTest::initTestCase()
{
    gst_init(nullptr, nullptr);
}

void CartWallEngineTest::cartOverlaysWithoutInterruptingActiveDeck()
{
    AudioEngine engine;
    engine.start();

    const QString trackA = QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav");
    engine.loadTrack(QStringLiteral("A"), trackA);
    engine.play(QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.state(QStringLiteral("A")) == DeckState::Playing, 3000);

    // short_beep.wav (~0.9s) rather than the longer crossfade fixtures —
    // now that the local sink properly paces to real time (see AudioEngine's
    // fakesink sync=TRUE fix), a cart clip takes genuinely that long to
    // reach EOS and self-clean.
    engine.triggerCart(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 1, 3000);

    // The deck must keep playing, undisturbed, the whole time the cart mixes in.
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);
    QTest::qWait(300);
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);

    // The cart is a short clip and should self-clean once it finishes.
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);
    QVERIFY(engine.state(QStringLiteral("A")) == DeckState::Playing);

    engine.shutdown();
}

void CartWallEngineTest::retriggerOverlapsRatherThanRestarting()
{
    AudioEngine engine;
    engine.start();

    const QString cart = QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav");
    engine.triggerCart(cart);
    engine.triggerCart(cart); // immediate retrigger — should overlap, not replace

    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 2, 3000);

    // Both instances self-clean independently once they finish.
    QTRY_VERIFY_WITH_TIMEOUT(engine.activeCartCount() == 0, 5000);

    engine.shutdown();
}

void CartWallEngineTest::triggerCartTokenTracksThatSpecificInstance()
{
    AudioEngine engine;
    engine.start();

    const QString token = engine.triggerCart(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    QVERIFY(!token.isEmpty());

    QTRY_VERIFY_WITH_TIMEOUT(engine.isCartTokenActive(token), 3000);
    QVERIFY(!engine.isCartTokenActive(QStringLiteral("cart-not-a-real-token")));

    // Self-cleans once it finishes — isCartTokenActive() must reflect that.
    QTRY_VERIFY_WITH_TIMEOUT(!engine.isCartTokenActive(token), 5000);

    engine.shutdown();
}

void CartWallEngineTest::cartTriggersNormallyWhenDeckAHasNonDefaultEq()
{
    // Regression coverage for CartSlotPlayer now attaching WITH processing
    // and seeding its EQ from Deck A's current curve (see MixEngineTest's
    // eqBandGain* tests for the underlying getter/setter mechanics — this
    // just proves triggering still works end-to-end through AudioEngine
    // once Deck A actually has a non-flat curve, not just at defaults).
    AudioEngine engine;
    engine.start();

    engine.loadTrack(QStringLiteral("A"), QStringLiteral(RS_TEST_FIXTURES_DIR "/tone440.wav"));
    engine.setDeckEqBandGain(QStringLiteral("A"), 1, 12.0);
    QCOMPARE(engine.deckEqBandGain(QStringLiteral("A"), 1), 12.0);

    const QString token = engine.triggerCart(QStringLiteral(RS_TEST_FIXTURES_DIR "/short_beep.wav"));
    QVERIFY(!token.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(engine.isCartTokenActive(token), 3000);
    QTRY_VERIFY_WITH_TIMEOUT(!engine.isCartTokenActive(token), 5000);

    engine.shutdown();
}

void CartWallEngineTest::cartThatErrorsSelfCleansInsteadOfStayingActiveForever()
{
    // Regression test for the Phase 2 watchdog fix: CartSlotPlayer::onError
    // used to only log, never tear the instance down the way onEos always
    // has — a cart that errors (here: a nonexistent file, a load-time
    // error) stayed "active" in CartWallEngine::m_active forever.
    AudioEngine engine;
    engine.start();

    const QString token = engine.triggerCart(QStringLiteral("/nonexistent/path/does-not-exist.mp3"));
    QVERIFY(!token.isEmpty());

    // Must self-clean, not stay stuck active — before the fix this token
    // would report active indefinitely.
    QTRY_VERIFY_WITH_TIMEOUT(!engine.isCartTokenActive(token), 3000);
    QCOMPARE(engine.activeCartCount(), 0);

    engine.shutdown();
}

QTEST_MAIN(CartWallEngineTest)
#include "CartWallEngineTest.moc"
