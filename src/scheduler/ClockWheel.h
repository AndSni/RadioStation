#pragma once

#include "db/TrackRecord.h"

#include <QVector>

namespace radio::scheduler {

enum class ClockWheelAction {
    Exhausted, // the cursor has walked past the last element -- nothing more to do this hour
    PlayElement, // the element at elementIndex is ready now: either flowing (minuteOffset < 0), or a soft-timed element whose minute has arrived (or passed -- accepted drift, no time-stretching)
    WaitForTime, // the element at elementIndex is timed and its minute hasn't arrived yet -- secondsUntilDue says how long
    ForceFadeNow, // the element at elementIndex is HARD-timed and its minute has arrived (or passed) -- whatever's currently on air must be cut, not waited out
};

struct ClockWheelDecision {
    ClockWheelAction action = ClockWheelAction::Exhausted;
    int elementIndex = -1; // valid for PlayElement/WaitForTime/ForceFadeNow
    int secondsUntilDue = 0; // valid for WaitForTime only
};

// Pure decision function for walking one clock's ordered element list (see
// ClockElementRecord in db/TrackRecord.h for the schema this operates on).
// No DB, no timers, no signals -- same shape as BlockTimeResolver/
// CartPicker. secondOfHour is 0..3599, seconds elapsed since the top of
// the CURRENT LOCAL HOUR -- a clock wheel is hour-anchored, not
// block-anchored (see ClockEngine's doc comment for why), so this is
// deliberately unrelated to ScheduleBlockRecord's own start/end minutes.
class ClockWheel {
public:
    static ClockWheelDecision evaluate(
        const QVector<radio::db::ClockElementRecord>& elements, int position, int secondOfHour);

    // Has the element at `position` been fully consumed and the cursor
    // should advance to the next one? For a music element, item_count
    // tracks are played before advancing (item_count defaults to at least
    // 1 even if stored as 0). For a cart element, item_count is a
    // music-only concept (see ClockElementRecord::itemCount's doc
    // comment) -- a cart element is complete the instant it fires once,
    // regardless of the stored value.
    static bool isElementComplete(const radio::db::ClockElementRecord& element, int itemsDone);

    // Where should a freshly-activated (or resumed-but-stale) wheel start?
    // Walks from index 0 and skips any timed element whose due second is
    // more than graceSeconds in the past -- handles both a block
    // activating mid-hour (elements timed earlier in the hour are simply
    // gone for this pass) and a restart just late enough that the
    // persisted position is itself stale. A flowing (untimed) element is
    // always eligible. Returns elements.size() (i.e. what evaluate() would
    // call Exhausted) if every element is skippable.
    static int firstEligiblePosition(
        const QVector<radio::db::ClockElementRecord>& elements, int secondOfHour, int graceSeconds = 120);
};

} // namespace radio::scheduler
