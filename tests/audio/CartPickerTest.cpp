#include "audio/CartPicker.h"

#include <QSet>
#include <QTest>

using namespace radio::audio;
using namespace radio::db;

namespace {
CartClipRecord makeClip(qint64 id, const QString& color = QString())
{
    CartClipRecord clip;
    clip.id = id;
    clip.filePath = QStringLiteral("/fake/cart%1.wav").arg(id);
    clip.label = QStringLiteral("Clip %1").arg(id);
    clip.color = color;
    return clip;
}
}

class CartPickerTest : public QObject {
    Q_OBJECT

private slots:
    void emptyClipsReturnsInvalid();
    void randomModeAlwaysPicksFromInputClips();
    void sequenceModeCyclesInOrder();
    void sequenceModePerBlockCursorsAreIndependent();
    void sequenceModeSelfCorrectsWhenClipListShrinks();
    void colorSequenceModeFiltersToMatchingColor();
    void colorSequenceModeReturnsInvalidWhenNoColorMatches();
    void colorSequenceModeCyclesAmongMatchingClipsInOrder();
    void unrecognizedModeDefaultsToRandom();
};

void CartPickerTest::emptyClipsReturnsInvalid()
{
    QHash<qint64, int> cursors;
    const auto result = CartPicker::pick(1, {}, QStringLiteral("random"), QString(), cursors);
    QCOMPARE(result.id, qint64(-1));
}

void CartPickerTest::randomModeAlwaysPicksFromInputClips()
{
    const QVector<CartClipRecord> clips = { makeClip(1), makeClip(2), makeClip(3) };
    QHash<qint64, int> cursors;

    QSet<qint64> seen;
    for (int i = 0; i < 50; ++i) {
        const auto result = CartPicker::pick(1, clips, QStringLiteral("random"), QString(), cursors);
        QVERIFY(result.id == 1 || result.id == 2 || result.id == 3);
        seen.insert(result.id);
    }
    // Over 50 draws from 3 items, every item should have come up at least once.
    QCOMPARE(seen.size(), 3);
}

void CartPickerTest::sequenceModeCyclesInOrder()
{
    const QVector<CartClipRecord> clips = { makeClip(10), makeClip(20), makeClip(30) };
    QHash<qint64, int> cursors;

    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(10));
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(20));
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(30));
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(10)); // wraps
}

void CartPickerTest::sequenceModePerBlockCursorsAreIndependent()
{
    const QVector<CartClipRecord> clips = { makeClip(10), makeClip(20) };
    QHash<qint64, int> cursors;

    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(10)); // block 1: 10
    QCOMPARE(CartPicker::pick(2, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(10)); // block 2: own cursor, 10
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(20)); // block 1: 20
    QCOMPARE(CartPicker::pick(2, clips, QStringLiteral("sequence"), QString(), cursors).id, qint64(20)); // block 2: 20
}

void CartPickerTest::sequenceModeSelfCorrectsWhenClipListShrinks()
{
    const QVector<CartClipRecord> bigList = { makeClip(1), makeClip(2), makeClip(3), makeClip(4) };
    QHash<qint64, int> cursors;

    CartPicker::pick(1, bigList, QStringLiteral("sequence"), QString(), cursors); // cursor -> 1
    CartPicker::pick(1, bigList, QStringLiteral("sequence"), QString(), cursors); // cursor -> 2
    CartPicker::pick(1, bigList, QStringLiteral("sequence"), QString(), cursors); // cursor -> 3, about to pick index 3

    // Now only 2 items - index 3 is out of range for this smaller list.
    const QVector<CartClipRecord> smallList = { makeClip(100), makeClip(200) };
    const auto result = CartPicker::pick(1, smallList, QStringLiteral("sequence"), QString(), cursors);
    QVERIFY(result.id == 100 || result.id == 200); // modulo wraps rather than going out of range
}

void CartPickerTest::colorSequenceModeFiltersToMatchingColor()
{
    const QVector<CartClipRecord> clips = { makeClip(1, QStringLiteral("#ff0000")), makeClip(2, QStringLiteral("#00ff00")),
        makeClip(3, QStringLiteral("#ff0000")) };
    QHash<qint64, int> cursors;

    for (int i = 0; i < 6; ++i) {
        const auto result = CartPicker::pick(1, clips, QStringLiteral("color_sequence"), QStringLiteral("#ff0000"), cursors);
        QVERIFY(result.id == 1 || result.id == 3);
    }
}

void CartPickerTest::colorSequenceModeReturnsInvalidWhenNoColorMatches()
{
    const QVector<CartClipRecord> clips = { makeClip(1, QStringLiteral("#ff0000")) };
    QHash<qint64, int> cursors;
    const auto result = CartPicker::pick(1, clips, QStringLiteral("color_sequence"), QStringLiteral("#0000ff"), cursors);
    QCOMPARE(result.id, qint64(-1));
}

void CartPickerTest::colorSequenceModeCyclesAmongMatchingClipsInOrder()
{
    const QVector<CartClipRecord> clips = { makeClip(1, QStringLiteral("#ff0000")), makeClip(2, QStringLiteral("#00ff00")),
        makeClip(3, QStringLiteral("#ff0000")) };
    QHash<qint64, int> cursors;

    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("color_sequence"), QStringLiteral("#ff0000"), cursors).id, qint64(1));
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("color_sequence"), QStringLiteral("#ff0000"), cursors).id, qint64(3));
    QCOMPARE(CartPicker::pick(1, clips, QStringLiteral("color_sequence"), QStringLiteral("#ff0000"), cursors).id,
        qint64(1)); // wraps
}

void CartPickerTest::unrecognizedModeDefaultsToRandom()
{
    const QVector<CartClipRecord> clips = { makeClip(1) };
    QHash<qint64, int> cursors;
    const auto result = CartPicker::pick(1, clips, QStringLiteral("nonsense"), QString(), cursors);
    QCOMPARE(result.id, qint64(1));
}

QTEST_MAIN(CartPickerTest)
#include "CartPickerTest.moc"
