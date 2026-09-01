#pragma once

#include "db/TrackRecord.h"

#include <QHash>
#include <QVector>

namespace radio::scheduler {

// Pure surplus/deficit weighted round-robin over rotation categories — no DB
// or Qt event-loop dependency, easily unit-testable.
class RotationWeightedPicker {
public:
    // Adds each category's target_ratio to its running credit, then returns
    // the id of the category with the highest credit (ties broken by lowest
    // category id for determinism) and resets that category's credit to 0.
    // credits is caller-owned state, keyed by category id, persisted across
    // calls to maintain the rotation.
    static qint64 pickCategory(
        const QVector<radio::db::RotationCategoryRecord>& categories, QHash<qint64, int>& credits);
};

} // namespace radio::scheduler
