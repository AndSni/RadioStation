#pragma once

#include "db/TrackRecord.h"

#include <QDateTime>
#include <QSet>
#include <QString>
#include <optional>

namespace radio::scheduler {

// Resolves a clock-driven schedule block's content, one decision at a time,
// by walking its assigned clock's ordered element list (see ClockWheel) --
// the DB-backed counterpart to ClockWheel's pure decisions. No timer of its
// own; every method takes an explicit `now` (this codebase's established
// convention for time-dependent code -- inject QDateTime, never mock the
// clock). Persists "where is this block's wheel right now" via
// ClockRepository::wheelStateFor()/saveWheelState() -- see
// ClockWheelStateRecord's doc comment for the hour-anchored resume/restart
// rule this relies on.
//
// AutoDjEngine/CartAutomationEngine/BlockTransitionController each hold an
// optional ClockEngine* (nullptr by default -- only MainWindow wires one
// in) and branch to it ONLY for a block whose clockId >= 0; a non-clock
// block's behavior is completely unaffected by this class's existence.
//
// A cart-type element (cart_random/cart_color/cart_specific) never enters
// the shared track queue -- cart clips live in cart_clips, not tracks, so
// they physically cannot become playlist_items rows. While the wheel is
// parked on a cart/timed element (not yet due, or due but not a music
// type), generateNextFromClock() instead queues a single "keep the idle
// deck cued" fill track from the nearest preceding music element, WITHOUT
// advancing the wheel cursor -- only noteCartFired() advances past a cart
// element, once it has actually fired on air.
class ClockEngine {
public:
    // Picks and queues ONE item for this clock-driven block, advancing the
    // wheel's persisted position as appropriate, and returns the queued
    // track's id -- or -1 if there's nothing to queue right now (parked on
    // a cart/timed element with no preceding music element to fill from,
    // or the clock is Exhausted for this hour). Mirrors
    // AutoDjEngine::generateNextFromBlock()'s no-repeat exclusion +
    // relaxation-ladder shape for the actual track pick.
    qint64 generateNextFromClock(const radio::db::ScheduleBlockRecord& block, const QDateTime& now,
        const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists);

    // How deep AutoDjEngine::topUpQueueInternal() should try to keep the
    // queue for this block right now -- compared directly against
    // PlaylistRepository::queueItems().size(), same convention as the
    // existing "count" mode's effectiveWatermark. 1 while parked on a
    // cart/timed element (just enough to keep the idle deck cued without
    // overqueueing past it); item_count - items_done while a music element
    // is actively playing out. 0 if Exhausted.
    int fillLimit(const radio::db::ScheduleBlockRecord& block, const QDateTime& now);

    // The cart element due to fire right now, if any (the wheel decision
    // is PlayElement or ForceFadeNow, landing on a cart-type element) --
    // CartAutomationEngine calls this from shouldSuppressAutoCrossfade()
    // ahead of its own legacy cartFrequency check for a clock-driven
    // block.
    std::optional<radio::db::ClockElementRecord> dueCartElement(
        const radio::db::ScheduleBlockRecord& block, const QDateTime& now);

    // Call once a cart returned by dueCartElement() has actually fired on
    // air -- advances the wheel cursor past it. A no-op if the wheel has
    // since moved on (e.g. called twice for the same due instant).
    void noteCartFired(const radio::db::ScheduleBlockRecord& block, const QDateTime& now);

    // The id of the HARD-timed MUSIC element whose minute has arrived (or
    // passed) right now, if any -- BlockTransitionController checks this
    // to decide whether to force an early fade rather than waiting for the
    // current deck to reach genuine EOS, and dedups its own fade request
    // against the returned id: the wheel does NOT advance past a due
    // element just because a fade was requested (only actually
    // queueing/firing its content advances it, via
    // generateNextFromClock()/noteCartFired()), so without deduping, this
    // would keep reporting the same element "due" -- and BlockTransitionController
    // would keep re-requesting a fade -- on every tick until the wheel
    // catches up.
    //
    // Deliberately excludes cart-type elements, hard-timed or not:
    // BlockTransitionController's only lever is
    // CrossfadeController::requestManualFade(), which starts a normal
    // ramped crossfade into whatever's already cued on the idle deck -- and
    // while the wheel is parked on a cart element, that's always a "keep
    // the idle deck cued" MUSIC fill track (see generateNextFromClock()'s
    // doc comment; carts can never enter the shared queue at all). Forcing
    // a fade there would ramp into the wrong content and skip the cart's
    // own audio entirely. A hard-timed cart is instead handled purely by
    // CartAutomationEngine's existing suppression mechanism (see
    // dueCartElement()), which is already due-aware -- both soft and hard
    // cart timing land at the current song's next natural opportunity (lead
    // -time trigger or genuine EOS), since neither can interrupt playback
    // mid-song without changes to CrossfadeController, which this phase
    // deliberately leaves untouched. In practice this means "hard" only
    // changes behavior for music elements in v1; flagged as a known,
    // bounded scope limitation rather than a missed requirement.
    std::optional<qint64> dueHardCutElementId(const radio::db::ScheduleBlockRecord& block, const QDateTime& now);

private:
    radio::db::ClockWheelStateRecord loadOrInitWheelState(
        const radio::db::ScheduleBlockRecord& block, const QDateTime& now) const;

    qint64 pickAndQueueFromMusicElement(const radio::db::ClockElementRecord& element, qint64 clockElementId,
        const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists);

    // Finds the nearest music-type element at or before `beforePosition` in
    // `elements` -- used to source a "keep the idle deck cued" fill track
    // while the wheel is parked on a cart/timed element. Returns nullptr
    // if none exists (e.g. a clock that starts with a timed cart and has
    // no preceding music element at all -- an edge case callers treat as
    // "nothing to fill", falling through to category rotation).
    static const radio::db::ClockElementRecord* nearestPrecedingMusicElement(
        const QVector<radio::db::ClockElementRecord>& elements, int beforePosition);
};

} // namespace radio::scheduler
