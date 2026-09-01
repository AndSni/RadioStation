#pragma once

#include "TrackRecord.h"

#include <QJsonObject>

namespace radio::db {

// Shared predicate for smart-playlist filter_json, used by both
// TrackRepository::candidatesForSmartPlaylist() (DB layer, AutoDjEngine
// path) and SmartPlaylistFilterProxyModel (UI layer) so the two never drift
// out of sync. Shape: {"genre": "Ambient", "categoryIds": [4,7],
// "yearMin": 1990, "yearMax": 2010, "energyMin": 0.0, "energyMax": 0.4,
// "bpmMin": 128, "bpmMax": 135} — absent/null fields mean "no constraint".
// A track whose energy/bpm is unanalyzed (-1 sentinel) never matches a
// filter that constrains that axis.
bool matchesSmartPlaylistFilter(const TrackRecord& track, const QJsonObject& filter);

} // namespace radio::db
