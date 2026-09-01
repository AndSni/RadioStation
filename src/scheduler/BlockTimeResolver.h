#pragma once

#include "db/TrackRecord.h"

#include <QDateTime>
#include <QVector>

namespace radio::scheduler {

// Pure day/time-window matcher for schedule blocks — no DB or Qt-event-loop
// dependency, easily unit-testable (same shape as RotationWeightedPicker).
class BlockTimeResolver {
public:
    // Returns the id of the first (topmost = highest priority; blocks are
    // assumed pre-sorted by position ASC, e.g. via
    // ScheduleBlockRepository::allBlocks()) enabled block whose days_mask +
    // start/end minute covers `now`, or -1 if none match. A block whose
    // end_minute <= start_minute crosses midnight — active either late on
    // its own day, or in the early-morning tail of the following day.
    static qint64 resolveActiveBlockId(const QVector<radio::db::ScheduleBlockRecord>& blocks, const QDateTime& now);

    // Seconds remaining until `block`'s current active period ends, given
    // `now` — handles the same midnight-crossing case resolveActiveBlockId()
    // does. Callers should already know `block` IS the one
    // resolveActiveBlockId() returned for this `now` (this doesn't
    // re-check days_mask/enabled); never negative.
    static qint64 secondsRemainingInBlock(const radio::db::ScheduleBlockRecord& block, const QDateTime& now);

    // The block's own full scheduled span, independent of "now" — for a
    // block that hasn't started yet (pre-staging via AutoDjEngine::
    // resetQueueForBlock()), which has no meaningful "remaining from now"
    // the way secondsRemainingInBlock() needs an already-active block for.
    // Same midnight-crossing handling as the other two methods; never
    // negative.
    static qint64 totalDurationSeconds(const radio::db::ScheduleBlockRecord& block);
};

} // namespace radio::scheduler
