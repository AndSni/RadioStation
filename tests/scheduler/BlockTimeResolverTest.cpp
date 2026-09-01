#include "scheduler/BlockTimeResolver.h"

#include <QDate>
#include <QDateTime>
#include <QTest>
#include <QTime>

using namespace radio::scheduler;
using namespace radio::db;

namespace {
int dayBit(const QDate& date)
{
    return 1 << (date.dayOfWeek() - 1);
}

ScheduleBlockRecord makeBlock(qint64 id, int daysMask, int startMinute, int endMinute, bool enabled = true)
{
    ScheduleBlockRecord block;
    block.id = id;
    block.enabled = enabled;
    block.daysMask = daysMask;
    block.startMinute = startMinute;
    block.endMinute = endMinute;
    return block;
}
}

class BlockTimeResolverTest : public QObject {
    Q_OBJECT

private slots:
    void noBlocksReturnsInvalid();
    void disabledBlockNeverMatches();
    void sameDayBlockMatchesWithinRange();
    void sameDayBlockDoesNotMatchOutsideRange();
    void sameDayBlockRequiresCorrectDayBit();
    void midnightCrossingBlockMatchesLateOnStartDay();
    void midnightCrossingBlockMatchesEarlyOnNextDay();
    void midnightCrossingBlockDoesNotMatchMidday();
    void topmostBlockWinsOnOverlap();
    void allDayBlockAlwaysMatchesOnItsDay();
    void secondsRemainingForSameDayBlock();
    void secondsRemainingNeverNegativeAtExactEnd();
    void secondsRemainingForMidnightCrossingBlockBeforeMidnight();
    void secondsRemainingForMidnightCrossingBlockAfterMidnight();
    void secondsRemainingForAllDayBlockCountsDownToMidnight();
    void totalDurationForSameDayBlock();
    void totalDurationForMidnightCrossingBlock();
    void totalDurationForAllDayBlockIsA24HourSpan();
};

void BlockTimeResolverTest::noBlocksReturnsInvalid()
{
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({}, QDateTime::currentDateTime()), qint64(-1));
}

void BlockTimeResolverTest::disabledBlockNeverMatches()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(12, 0));
    const auto block = makeBlock(1, dayBit(today), 0, 1440, /*enabled=*/false);
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(-1));
}

void BlockTimeResolverTest::sameDayBlockMatchesWithinRange()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(10, 30));
    const auto block = makeBlock(1, dayBit(today), 9 * 60, 17 * 60); // 09:00-17:00
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(1));
}

void BlockTimeResolverTest::sameDayBlockDoesNotMatchOutsideRange()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(20, 0));
    const auto block = makeBlock(1, dayBit(today), 9 * 60, 17 * 60);
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(-1));
}

void BlockTimeResolverTest::sameDayBlockRequiresCorrectDayBit()
{
    const QDate today(2026, 3, 10);
    const QDate otherDay = today.addDays(1);
    const QDateTime now(today, QTime(10, 30));
    const auto block = makeBlock(1, dayBit(otherDay), 9 * 60, 17 * 60); // wrong day's bit
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(-1));
}

void BlockTimeResolverTest::midnightCrossingBlockMatchesLateOnStartDay()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(23, 0));
    const auto block = makeBlock(1, dayBit(today), 22 * 60, 2 * 60); // 22:00-02:00
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(1));
}

void BlockTimeResolverTest::midnightCrossingBlockMatchesEarlyOnNextDay()
{
    const QDate startDay(2026, 3, 10);
    const QDate nextDay = startDay.addDays(1);
    const QDateTime now(nextDay, QTime(1, 0));
    // The block's day bit is startDay's, not nextDay's - it should still
    // match during nextDay's early-morning tail.
    const auto block = makeBlock(1, dayBit(startDay), 22 * 60, 2 * 60);
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(1));
}

void BlockTimeResolverTest::midnightCrossingBlockDoesNotMatchMidday()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(12, 0));
    const auto block = makeBlock(1, dayBit(today), 22 * 60, 2 * 60);
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, now), qint64(-1));
}

void BlockTimeResolverTest::topmostBlockWinsOnOverlap()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(10, 0));
    const auto topBlock = makeBlock(1, dayBit(today), 9 * 60, 17 * 60);
    const auto bottomBlock = makeBlock(2, dayBit(today), 8 * 60, 18 * 60); // also covers 10:00, but listed second
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ topBlock, bottomBlock }, now), qint64(1));
    // Reversing the list order flips which one wins - confirms this is
    // genuinely position-in-the-list-driven, not id-driven or coincidental.
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ bottomBlock, topBlock }, now), qint64(2));
}

void BlockTimeResolverTest::allDayBlockAlwaysMatchesOnItsDay()
{
    const QDate today(2026, 3, 10);
    const auto block = makeBlock(1, dayBit(today), 0, 1440);
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, QDateTime(today, QTime(0, 0))), qint64(1));
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, QDateTime(today, QTime(12, 0))), qint64(1));
    QCOMPARE(BlockTimeResolver::resolveActiveBlockId({ block }, QDateTime(today, QTime(23, 59))), qint64(1));
}

void BlockTimeResolverTest::secondsRemainingForSameDayBlock()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(9, 30, 15));
    const auto block = makeBlock(1, dayBit(today), 9 * 60, 10 * 60); // 09:00-10:00
    // 10:00:00 - 09:30:15 = 29m45s = 1785s
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, now), qint64(1785));
}

void BlockTimeResolverTest::secondsRemainingNeverNegativeAtExactEnd()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(10, 0, 0)); // exactly at the end boundary
    const auto block = makeBlock(1, dayBit(today), 9 * 60, 10 * 60);
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, now), qint64(0));

    const QDateTime pastEnd(today, QTime(10, 5, 0)); // past the end entirely
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, pastEnd), qint64(0));
}

void BlockTimeResolverTest::secondsRemainingForMidnightCrossingBlockBeforeMidnight()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(23, 50, 0));
    const auto block = makeBlock(1, dayBit(today), 22 * 60, 2 * 60); // 22:00-02:00
    // End is tomorrow at 02:00 - 23:50:00 today = 2h10m = 7800s
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, now), qint64(7800));
}

void BlockTimeResolverTest::secondsRemainingForMidnightCrossingBlockAfterMidnight()
{
    const QDate startDay(2026, 3, 10);
    const QDate nextDay = startDay.addDays(1);
    const QDateTime now(nextDay, QTime(1, 45, 0));
    const auto block = makeBlock(1, dayBit(startDay), 22 * 60, 2 * 60);
    // End is today (nextDay) at 02:00 - 01:45:00 = 15m = 900s
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, now), qint64(900));
}

void BlockTimeResolverTest::secondsRemainingForAllDayBlockCountsDownToMidnight()
{
    const QDate today(2026, 3, 10);
    const QDateTime now(today, QTime(23, 0, 0));
    const auto block = makeBlock(1, dayBit(today), 0, 1440); // All Day
    // Midnight - 23:00:00 = 1h = 3600s
    QCOMPARE(BlockTimeResolver::secondsRemainingInBlock(block, now), qint64(3600));
}

void BlockTimeResolverTest::totalDurationForSameDayBlock()
{
    const auto block = makeBlock(1, 0, 9 * 60, 17 * 60); // 09:00-17:00 = 8h
    QCOMPARE(BlockTimeResolver::totalDurationSeconds(block), qint64(8 * 3600));
}

void BlockTimeResolverTest::totalDurationForMidnightCrossingBlock()
{
    const auto block = makeBlock(1, 0, 22 * 60, 2 * 60); // 22:00-02:00 = 4h
    QCOMPARE(BlockTimeResolver::totalDurationSeconds(block), qint64(4 * 3600));
}

void BlockTimeResolverTest::totalDurationForAllDayBlockIsA24HourSpan()
{
    const auto block = makeBlock(1, 0, 0, 1440); // All Day
    QCOMPARE(BlockTimeResolver::totalDurationSeconds(block), qint64(24 * 3600));
}

QTEST_MAIN(BlockTimeResolverTest)
#include "BlockTimeResolverTest.moc"
