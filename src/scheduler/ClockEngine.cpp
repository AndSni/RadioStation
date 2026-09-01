#include "ClockEngine.h"
#include "ClockWheel.h"

#include "core/Logging.h"
#include "db/ClockRepository.h"
#include "db/PlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QRandomGenerator>
#include <QTime>
#include <QTimeZone>
#include <algorithm>

namespace radio::scheduler {

using namespace radio::db;

namespace {
bool isMusicElementType(const QString& elementType)
{
    return elementType == QStringLiteral("music_playlist") || elementType == QStringLiteral("music_smart_playlist");
}

bool isCartElementType(const QString& elementType)
{
    return elementType == QStringLiteral("cart_random") || elementType == QStringLiteral("cart_color")
        || elementType == QStringLiteral("cart_specific");
}

int secondOfHour(const QDateTime& now)
{
    const QTime t = now.time();
    return t.minute() * 60 + t.second();
}

// UTC epoch seconds of the top of now's LOCAL hour -- see
// ClockWheelStateRecord's doc comment for why the wheel is anchored to
// this rather than to the enclosing schedule block's own start time.
qint64 hourStartEpoch(const QDateTime& now)
{
    const QDateTime hourStart(now.date(), QTime(now.time().hour(), 0, 0), now.timeZone());
    return hourStart.toUTC().toSecsSinceEpoch();
}

// Mirrors AutoDjEngine::generateNextFromBlock()'s own metric/direction
// string<->enum conversion -- kept as a small, independent duplicate
// rather than a shared extraction, since the two call sites' surrounding
// logic (queue-wide exclusion vs. per-element wheel bookkeeping) differs
// enough that a shared helper would buy little beyond these few lines.
SelectionMetric metricFromString(const QString& metric)
{
    if (metric == QStringLiteral("rating"))
        return SelectionMetric::Rating;
    if (metric == QStringLiteral("play_count"))
        return SelectionMetric::PlayCount;
    return SelectionMetric::None;
}

SortDirection directionFromString(const QString& direction)
{
    return direction == QStringLiteral("asc") ? SortDirection::Ascending : SortDirection::Descending;
}
}

ClockWheelStateRecord ClockEngine::loadOrInitWheelState(const ScheduleBlockRecord& block, const QDateTime& now) const
{
    const qint64 currentHourEpoch = hourStartEpoch(now);
    ClockWheelStateRecord state = ClockRepository::wheelStateFor(block.id);
    if (state.scheduleBlockId >= 0 && state.clockId == block.clockId && state.hourStartEpoch == currentHourEpoch)
        return state; // resume mid-hour

    // Fresh pass -- first activation, a new hour, a new day, or a resume
    // stale enough that it belongs to a different hour than this one.
    const auto elements = ClockRepository::elementsForClock(block.clockId);
    ClockWheelStateRecord fresh;
    fresh.scheduleBlockId = block.id;
    fresh.clockId = block.clockId;
    fresh.hourStartEpoch = currentHourEpoch;
    fresh.position = ClockWheel::firstEligiblePosition(elements, secondOfHour(now));
    fresh.itemsDone = 0;
    ClockRepository::saveWheelState(fresh);
    RS_LOG_INFO("scheduler.clock",
        QStringLiteral("Clock wheel for block '%1' starting a fresh hour pass at element position %2")
            .arg(block.name)
            .arg(fresh.position));
    return fresh;
}

const ClockElementRecord* ClockEngine::nearestPrecedingMusicElement(
    const QVector<ClockElementRecord>& elements, int beforePosition)
{
    const int start = std::min(beforePosition, static_cast<int>(elements.size()) - 1);
    for (int i = start; i >= 0; --i) {
        if (isMusicElementType(elements.at(i).elementType))
            return &elements.at(i);
    }
    return nullptr;
}

qint64 ClockEngine::pickAndQueueFromMusicElement(const ClockElementRecord& element, qint64 clockElementId,
    const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    const SelectionMetric metric = metricFromString(element.selectionMetric);
    const SortDirection direction = directionFromString(element.selectionDirection);
    const bool targetsSmartPlaylist = element.smartPlaylistId >= 0;

    auto fetchCandidates = [&](const QSet<qint64>& excludeIds, const QSet<QString>& excludeArt) {
        return targetsSmartPlaylist
            ? TrackRepository::candidatesForSmartPlaylist(element.smartPlaylistId, excludeIds, excludeArt)
            : TrackRepository::candidatesForPlaylist(element.playlistId, metric, direction, excludeIds, excludeArt);
    };

    QVector<TrackRecord> candidates = fetchCandidates(excludeTrackIds, excludeArtists);
    if (candidates.isEmpty())
        candidates = fetchCandidates(excludeTrackIds, {});
    if (candidates.isEmpty())
        candidates = fetchCandidates({}, {});
    if (candidates.isEmpty()) {
        RS_LOG_WARN(
            "scheduler.clock", QStringLiteral("Clock element %1's playlist is empty or exhausted").arg(clockElementId));
        return -1;
    }

    qint64 pickedId;
    QString pickedTitle;
    QString pickedArtist;
    if (element.selectionMode == QStringLiteral("deterministic")) {
        const TrackRecord& picked = candidates.first();
        pickedId = picked.id;
        pickedTitle = picked.title;
        pickedArtist = picked.artist;
    } else {
        const int poolSize = metric == SelectionMetric::None ? static_cast<int>(candidates.size())
                                                               : std::min(static_cast<int>(candidates.size()), 10);
        const int choice = poolSize > 1 ? static_cast<int>(QRandomGenerator::global()->bounded(poolSize)) : 0;
        const TrackRecord& picked = candidates.at(choice);
        pickedId = picked.id;
        pickedTitle = picked.title;
        pickedArtist = picked.artist;
    }

    if (!PlaylistRepository::appendToQueue(pickedId, QStringLiteral("clock:%1").arg(clockElementId)))
        return -1;

    RS_LOG_INFO("scheduler.clock",
        QStringLiteral("Queued '%1' by %2 (clock element %3)").arg(pickedTitle, pickedArtist).arg(clockElementId));
    return pickedId;
}

qint64 ClockEngine::generateNextFromClock(const ScheduleBlockRecord& block, const QDateTime& now,
    const QSet<qint64>& excludeTrackIds, const QSet<QString>& excludeArtists)
{
    if (block.clockId < 0)
        return -1;

    const auto elements = ClockRepository::elementsForClock(block.clockId);
    ClockWheelStateRecord state = loadOrInitWheelState(block, now);
    const auto decision = ClockWheel::evaluate(elements, state.position, secondOfHour(now));

    if (decision.action == ClockWheelAction::Exhausted)
        return -1;

    const bool parkedOnCartOrNotDue
        = decision.action == ClockWheelAction::WaitForTime || isCartElementType(elements.at(decision.elementIndex).elementType);
    if (parkedOnCartOrNotDue) {
        // Keep the idle deck cued with a fill track from the nearest
        // preceding music element, WITHOUT advancing the cursor -- see
        // this class's doc comment.
        const ClockElementRecord* fillElement = nearestPrecedingMusicElement(elements, decision.elementIndex);
        if (!fillElement)
            return -1; // nothing to fill from -- caller falls through to category rotation
        return pickAndQueueFromMusicElement(*fillElement, fillElement->id, excludeTrackIds, excludeArtists);
    }

    // PlayElement or ForceFadeNow landing on a MUSIC element -- actually
    // advance the wheel.
    const ClockElementRecord& element = elements.at(decision.elementIndex);
    const qint64 pickedId = pickAndQueueFromMusicElement(element, element.id, excludeTrackIds, excludeArtists);
    if (pickedId < 0)
        return -1;

    state.itemsDone += 1;
    if (ClockWheel::isElementComplete(element, state.itemsDone)) {
        state.position += 1;
        state.itemsDone = 0;
    }
    ClockRepository::saveWheelState(state);
    return pickedId;
}

int ClockEngine::fillLimit(const ScheduleBlockRecord& block, const QDateTime& now)
{
    if (block.clockId < 0)
        return 0;

    const auto elements = ClockRepository::elementsForClock(block.clockId);
    const ClockWheelStateRecord state = loadOrInitWheelState(block, now);
    const auto decision = ClockWheel::evaluate(elements, state.position, secondOfHour(now));

    if (decision.action == ClockWheelAction::Exhausted)
        return 0;
    if (decision.action == ClockWheelAction::WaitForTime)
        return 1;

    const ClockElementRecord& element = elements.at(decision.elementIndex);
    if (isCartElementType(element.elementType))
        return 1;
    return std::max(1, element.itemCount - state.itemsDone);
}

std::optional<ClockElementRecord> ClockEngine::dueCartElement(const ScheduleBlockRecord& block, const QDateTime& now)
{
    if (block.clockId < 0)
        return std::nullopt;

    const auto elements = ClockRepository::elementsForClock(block.clockId);
    const ClockWheelStateRecord state = loadOrInitWheelState(block, now);
    const auto decision = ClockWheel::evaluate(elements, state.position, secondOfHour(now));

    if (decision.action != ClockWheelAction::PlayElement && decision.action != ClockWheelAction::ForceFadeNow)
        return std::nullopt;

    const ClockElementRecord& element = elements.at(decision.elementIndex);
    if (!isCartElementType(element.elementType))
        return std::nullopt;
    return element;
}

void ClockEngine::noteCartFired(const ScheduleBlockRecord& block, const QDateTime& now)
{
    if (block.clockId < 0)
        return;

    const auto elements = ClockRepository::elementsForClock(block.clockId);
    ClockWheelStateRecord state = loadOrInitWheelState(block, now);
    const auto decision = ClockWheel::evaluate(elements, state.position, secondOfHour(now));
    if (decision.action != ClockWheelAction::PlayElement && decision.action != ClockWheelAction::ForceFadeNow)
        return; // the wheel has already moved on since dueCartElement() was checked -- nothing to do

    const ClockElementRecord& element = elements.at(decision.elementIndex);
    if (!isCartElementType(element.elementType))
        return;

    state.position += 1;
    state.itemsDone = 0;
    ClockRepository::saveWheelState(state);
    RS_LOG_INFO("scheduler.clock",
        QStringLiteral("Clock cart element %1 fired; wheel advanced to position %2").arg(element.id).arg(state.position));
}

std::optional<qint64> ClockEngine::dueHardCutElementId(const ScheduleBlockRecord& block, const QDateTime& now)
{
    if (block.clockId < 0)
        return std::nullopt;

    const auto elements = ClockRepository::elementsForClock(block.clockId);
    const ClockWheelStateRecord state = loadOrInitWheelState(block, now);
    const auto decision = ClockWheel::evaluate(elements, state.position, secondOfHour(now));
    if (decision.action != ClockWheelAction::ForceFadeNow)
        return std::nullopt;

    const ClockElementRecord& element = elements.at(decision.elementIndex);
    if (!isMusicElementType(element.elementType))
        return std::nullopt; // see this method's doc comment -- carts never force a fade
    return element.id;
}

} // namespace radio::scheduler
