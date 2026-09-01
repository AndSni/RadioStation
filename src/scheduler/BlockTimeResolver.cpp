#include "BlockTimeResolver.h"

#include <algorithm>

namespace radio::scheduler {

using radio::db::ScheduleBlockRecord;

namespace {
// dayOfWeek: 1=Monday..7=Sunday (QDate::dayOfWeek()'s own convention),
// matching daysMask's bit0=Monday..bit6=Sunday layout.
bool hasBit(int mask, int dayOfWeek)
{
    return (mask & (1 << (dayOfWeek - 1))) != 0;
}
}

qint64 BlockTimeResolver::resolveActiveBlockId(const QVector<ScheduleBlockRecord>& blocks, const QDateTime& now)
{
    const int nowMinute = now.time().hour() * 60 + now.time().minute();
    const int todayBit = now.date().dayOfWeek();
    const int yesterdayBit = ((todayBit - 1 + 6) % 7) + 1;

    for (const auto& block : blocks) {
        if (!block.enabled)
            continue;

        if (block.startMinute <= block.endMinute) {
            if (hasBit(block.daysMask, todayBit) && nowMinute >= block.startMinute && nowMinute < block.endMinute)
                return block.id;
        } else {
            // Wraps midnight (e.g. 22:00-02:00): active either late on the
            // block's own day, or in the early-morning tail of the day
            // after — attributed to yesterday's day bit, not today's,
            // since that's the day the block conceptually "belongs to".
            if ((hasBit(block.daysMask, todayBit) && nowMinute >= block.startMinute)
                || (hasBit(block.daysMask, yesterdayBit) && nowMinute < block.endMinute))
                return block.id;
        }
    }
    return -1;
}

qint64 BlockTimeResolver::secondsRemainingInBlock(const ScheduleBlockRecord& block, const QDateTime& now)
{
    const int nowMinute = now.time().hour() * 60 + now.time().minute();
    const qint64 nowSecondOfDay = static_cast<qint64>(nowMinute) * 60 + now.time().second();

    // Same midnight-crossing determination as resolveActiveBlockId(): if
    // start <= end, the block's end is always today. If it wraps and we're
    // past start (the pre-midnight portion), the end falls on tomorrow;
    // otherwise we're already in the post-midnight tail and the end is
    // today.
    qint64 endSecondOfDay = static_cast<qint64>(block.endMinute) * 60;
    if (block.startMinute > block.endMinute && nowMinute >= block.startMinute)
        endSecondOfDay += 24 * 60 * 60;

    return std::max<qint64>(0, endSecondOfDay - nowSecondOfDay);
}

qint64 BlockTimeResolver::totalDurationSeconds(const ScheduleBlockRecord& block)
{
    const int minutes = block.endMinute <= block.startMinute ? (1440 - block.startMinute + block.endMinute)
                                                               : (block.endMinute - block.startMinute);
    return std::max<qint64>(0, static_cast<qint64>(minutes) * 60);
}

} // namespace radio::scheduler
