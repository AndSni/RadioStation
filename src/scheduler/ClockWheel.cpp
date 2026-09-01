#include "ClockWheel.h"

#include <algorithm>

namespace radio::scheduler {

using radio::db::ClockElementRecord;

namespace {
bool isMusicElement(const ClockElementRecord& element)
{
    return element.elementType == QStringLiteral("music_playlist")
        || element.elementType == QStringLiteral("music_smart_playlist");
}
}

ClockWheelDecision ClockWheel::evaluate(const QVector<ClockElementRecord>& elements, int position, int secondOfHour)
{
    if (position < 0 || position >= elements.size())
        return { ClockWheelAction::Exhausted, -1, 0 };

    const ClockElementRecord& element = elements.at(position);
    if (element.minuteOffset < 0)
        return { ClockWheelAction::PlayElement, position, 0 };

    const int dueSecond = element.minuteOffset * 60;
    if (secondOfHour >= dueSecond) {
        // Due or LATE (drift accumulated) -- fire now either way; no
        // time-stretching/elastic pacing, per this feature's own scope.
        const bool hard = element.timingMode == QStringLiteral("hard");
        return { hard ? ClockWheelAction::ForceFadeNow : ClockWheelAction::PlayElement, position, 0 };
    }
    return { ClockWheelAction::WaitForTime, position, dueSecond - secondOfHour };
}

bool ClockWheel::isElementComplete(const ClockElementRecord& element, int itemsDone)
{
    if (!isMusicElement(element))
        return itemsDone > 0;
    return itemsDone >= std::max(1, element.itemCount);
}

int ClockWheel::firstEligiblePosition(const QVector<ClockElementRecord>& elements, int secondOfHour, int graceSeconds)
{
    for (int i = 0; i < elements.size(); ++i) {
        const ClockElementRecord& element = elements.at(i);
        if (element.minuteOffset < 0)
            return i;
        const int dueSecond = element.minuteOffset * 60;
        if (secondOfHour - dueSecond <= graceSeconds)
            return i; // not yet due, or due within the grace window
    }
    return elements.size();
}

} // namespace radio::scheduler
