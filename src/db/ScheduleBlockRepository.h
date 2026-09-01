#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// Named day/time windows AutoDjEngine picks tracks from (see
// BlockTimeResolver in rs_scheduler for the "which block is active now"
// logic — topmost position wins on overlap). All methods must run on the
// UI thread (see Database.h).
class ScheduleBlockRepository {
public:
    static QVector<ScheduleBlockRecord> allBlocks(); // ORDER BY position ASC

    // Ignores block.id and block.position — assigns position at the end.
    // Returns the new block's id.
    static qint64 createBlock(const ScheduleBlockRecord& block);

    // Full field update by block.id. Does NOT touch position — use
    // setBlockOrder() for reordering.
    static bool updateBlock(const ScheduleBlockRecord& block);

    static bool deleteBlock(qint64 blockId);
    static bool setBlockOrder(const QVector<qint64>& blockIdsInOrder);
};

} // namespace radio::db
