#include "ScheduleBlockValidity.h"

#include "db/ClockRepository.h"
#include "db/PlaylistRepository.h"
#include "db/SmartPlaylistRepository.h"
#include "db/TrackRepository.h"

#include <algorithm>

namespace radio::scheduler {

using namespace radio::db;

ScheduleBlockHealth ScheduleBlockValidity::evaluate(const ScheduleBlockRecord& block)
{
    if (block.clockId >= 0) {
        if (ClockRepository::clockById(block.clockId).id < 0)
            return ScheduleBlockHealth::ClockDeleted;
        if (ClockRepository::elementsForClock(block.clockId).isEmpty())
            return ScheduleBlockHealth::ClockEmpty;
        return ScheduleBlockHealth::Ok;
    }

    if (block.smartPlaylistId >= 0) {
        if (SmartPlaylistRepository::byId(block.smartPlaylistId).id < 0)
            return ScheduleBlockHealth::TargetDeleted;
        if (TrackRepository::candidatesForSmartPlaylist(block.smartPlaylistId, {}, {}).isEmpty())
            return ScheduleBlockHealth::TargetEmpty;
        return ScheduleBlockHealth::Ok;
    }

    bool playlistExists = false;
    for (const PlaylistRecord& playlist : PlaylistRepository::listPlaylists()) {
        if (playlist.id == block.playlistId) {
            playlistExists = true;
            break;
        }
    }
    if (!playlistExists)
        return ScheduleBlockHealth::TargetDeleted;

    const auto items = PlaylistRepository::itemsForPlaylist(block.playlistId);
    const bool hasUsableTrack = std::any_of(
        items.begin(), items.end(), [](const PlaylistItemRecord& item) { return !item.missing; });
    if (!hasUsableTrack)
        return ScheduleBlockHealth::TargetEmpty;

    return ScheduleBlockHealth::Ok;
}

QString ScheduleBlockValidity::describe(ScheduleBlockHealth health)
{
    switch (health) {
    case ScheduleBlockHealth::TargetDeleted:
        return QStringLiteral("playlist deleted");
    case ScheduleBlockHealth::TargetEmpty:
        return QStringLiteral("playlist empty");
    case ScheduleBlockHealth::ClockDeleted:
        return QStringLiteral("clock deleted");
    case ScheduleBlockHealth::ClockEmpty:
        return QStringLiteral("clock has no elements");
    case ScheduleBlockHealth::Ok:
    default:
        return QString();
    }
}

} // namespace radio::scheduler
