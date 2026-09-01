#pragma once

#include "db/TrackRecord.h"

namespace radio::scheduler {

// Whether a schedule block's target (static playlist or smart playlist) can
// actually supply tracks right now -- surfaced by AutoDjPanelWidget so a
// block that would silently stall Auto DJ (deleted playlist, playlist whose
// tracks all moved/got deleted, empty smart-playlist filter) is visible
// before it's live, not discovered when the queue runs dry on air.
enum class ScheduleBlockHealth {
    Ok,
    TargetDeleted, // playlist_id/smart_playlist_id no longer resolves to a row
    TargetEmpty, // resolves, but has zero non-missing candidate tracks
    ClockDeleted, // clock_id no longer resolves to a row
    ClockEmpty, // resolves, but the clock has zero elements
};

class ScheduleBlockValidity {
public:
    static ScheduleBlockHealth evaluate(const radio::db::ScheduleBlockRecord& block);

    // Empty string for Ok -- callers append this directly to a summary
    // label, so "no warning" needs to mean "nothing appended".
    static QString describe(ScheduleBlockHealth health);
};

} // namespace radio::scheduler
