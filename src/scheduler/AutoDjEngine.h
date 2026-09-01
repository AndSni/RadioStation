#pragma once

#include <QHash>
#include <QObject>

class QTimer;

namespace radio::db {
struct ScheduleBlockRecord;
}

namespace radio::scheduler {

class ClockEngine;

// Tops up the queue playlist whenever it drops below queueLowWatermark.
// Each pick first tries the currently-active schedule block (see
// BlockTimeResolver — topmost-position block whose day/time window covers
// now wins), pulling from that block's playlist via its chosen selection
// logic. If no block is active, or the active block's playlist has nothing
// usable even after relaxing the no-repeat filters, falls through to the
// original behavior unchanged: picking a rotation category
// (RotationWeightedPicker) and a track within it that respects the
// no-repeat track/artist windows, progressively relaxing those constraints
// (and logging each relaxation) rather than ever silently leaving the queue
// starved. A station with zero schedule blocks behaves identically to
// before schedule blocks existed.
class AutoDjEngine : public QObject {
    Q_OBJECT

public:
    explicit AutoDjEngine(QObject* parent = nullptr);

    void start(); // begins the periodic top-up timer
    void setQueueLowWatermark(int count);
    void setNoRepeatTrackWindow(int count);
    void setNoRepeatArtistWindow(int count);

    // Optional (nullptr by default -- with no injection, every block
    // behaves exactly as before clocks existed). Only MainWindow is
    // expected to call this. Not owned.
    void setClockEngine(ClockEngine* clockEngine) { m_clockEngine = clockEngine; }

    // Runs one top-up pass immediately (used by start()'s timer, and
    // directly by tests). Returns the number of tracks added.
    int topUpQueue();

    // Discards whatever's currently queued and refills from scratch (see
    // topUpQueue()) — used when Auto DJ fully resumes after a manual
    // interruption (see CrossfadeController::handsOffResumed()), since a
    // queue built before/during the interruption may no longer match the
    // currently-active schedule block.
    void resetQueue();

    // Discards whatever's currently queued and refills targeting `blockId`
    // specifically (or category-rotation if blockId is -1), instead of
    // whatever resolveActiveBlockId(now) would naturally pick — used to
    // pre-stage the NEXT schedule block's content ahead of its actual start
    // time (see BlockTransitionController).
    void resetQueueForBlock(qint64 blockId);

signals:
    void queueUpdated();
    void starved(); // library has nothing left to offer at all

private slots:
    void onTick();

private:
    // Sentinel for generateNext()/topUpQueueInternal()'s forcedBlockId:
    // resolve the active block from the real wall clock, same as always.
    // Distinct from -1, which is a real, meaningful input ("no block at
    // all — go straight to category-rotation").
    static constexpr qint64 kResolveActiveBlockFromNow = -2;

    // Picks and queues a single track. Returns the queued track's id, or -1
    // if the library has nothing usable at all. forcedBlockId overrides
    // which block to pull from (skipping resolveActiveBlockId() entirely)
    // unless it's kResolveActiveBlockFromNow.
    qint64 generateNext(qint64 forcedBlockId = kResolveActiveBlockFromNow);

    // Shared loop behind topUpQueue()/resetQueueForBlock(): keeps calling
    // generateNext(forcedBlockId) until the watermark is met or the library
    // is starved. Returns the number of tracks added.
    int topUpQueueInternal(qint64 forcedBlockId);

    // Picks and queues a track from block's playlist per its selection
    // logic, applying the same progressive relax-artist-filter -> relax-
    // track-repeat-filter ladder as the category-rotation path. Returns the
    // queued track's id, or -1 if the block's playlist has nothing usable
    // even after relaxing (caller falls through to category-rotation).
    qint64 generateNextFromBlock(const radio::db::ScheduleBlockRecord& block);

    // Sum of durationMs across the current queue — see topUpQueueInternal()'s
    // "remaining" mode. Relies on the queue being fully cleared at every
    // block transition/pre-stage (already the case — see
    // BlockTransitionController/resetQueueForBlock()), so in normal
    // operation everything currently queued belongs to whichever block is
    // being targeted; no per-item block-attribution needed.
    qint64 queuedDurationMs() const;

    QTimer* m_timer;
    QHash<qint64, int> m_categoryCredits;
    ClockEngine* m_clockEngine = nullptr;

    int m_queueLowWatermark = 10;
    int m_noRepeatTrackWindow = 20;
    int m_noRepeatArtistWindow = 5;
};

} // namespace radio::scheduler
