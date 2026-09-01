#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// CRUD for reusable hour-template "clocks" and their ordered elements (see
// ClockWheel/ClockEngine in rs_scheduler for how these are actually
// walked), plus per-block wheel-position persistence. All methods must run
// on the UI thread (see Database.h).
class ClockRepository {
public:
    static QVector<ClockRecord> allClocks(); // ORDER BY name COLLATE NOCASE

    // Default-constructed record (id == -1) if no such clock exists,
    // matching this codebase's existing id == -1 "not found" convention.
    static ClockRecord clockById(qint64 id);

    // Default-constructed record (id == -1) if no such element exists.
    // Used by QueueWidget to label which wheel slot a queued item came from
    // (see QueueItemRecord::source's doc comment).
    static ClockElementRecord elementById(qint64 elementId);

    static qint64 createClock(const QString& name);
    static bool renameClock(qint64 id, const QString& name);

    // Hand-cascades (SQLite FK enforcement is off, see Migrations.cpp --
    // every REFERENCES/CASCADE in the schema is documentation only):
    // deletes every clock_elements row for this clock, every
    // clock_wheel_state row referencing it, and clears clock_id on any
    // schedule block that targeted it.
    static bool deleteClock(qint64 id);

    static QVector<ClockElementRecord> elementsForClock(qint64 clockId); // ORDER BY position ASC

    // Ignores element.id/element.position -- assigns position at the end
    // (scoped to element.clockId), mirroring
    // ScheduleBlockRepository::createBlock(). Returns the new element's id.
    static qint64 createElement(const ClockElementRecord& element);

    // Full field update by element.id. Does NOT touch position -- use
    // setElementOrder() for reordering.
    static bool updateElement(const ClockElementRecord& element);

    static bool deleteElement(qint64 elementId);

    // Rewrites position = index for each id, in order, within one
    // transaction -- mirrors ScheduleBlockRepository::setBlockOrder()
    // exactly. Not scoped to a single clock_id (element ids are already
    // globally unique).
    static bool setElementOrder(const QVector<qint64>& elementIdsInOrder);

    // Default-constructed (scheduleBlockId == -1) if no wheel state is
    // persisted yet for this block -- callers treat that as "start fresh
    // at position 0".
    static ClockWheelStateRecord wheelStateFor(qint64 scheduleBlockId);

    // Upserts, keyed on scheduleBlockId (the table's PRIMARY KEY).
    static bool saveWheelState(const ClockWheelStateRecord& state);

    static bool clearWheelState(qint64 scheduleBlockId);
};

} // namespace radio::db
