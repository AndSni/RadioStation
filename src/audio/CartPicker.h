#pragma once

#include "db/CartRepository.h"

#include <QHash>
#include <QString>
#include <QVector>

namespace radio::audio {

// Pure clip-selection logic for schedule-block Cart Wall automation — no
// DB/Qt-event-loop dependency, easily unit-testable (same shape as
// RotationWeightedPicker/BlockTimeResolver in rs_scheduler).
class CartPicker {
public:
    // mode: "random" | "sequence" | "color_sequence" (any other value is
    // treated as "random"). color is only used for "color_sequence".
    // Returns a default-constructed (id == -1) record if no clip could be
    // chosen (no clips at all, or none match color). cursors is caller-
    // owned state, keyed by blockId, advanced in place for the sequence
    // modes — each block keeps its own independent position in the
    // rotation, and self-corrects (via modulo) if the clip set shrinks
    // between calls rather than going out of range.
    static radio::db::CartClipRecord pick(qint64 blockId, const QVector<radio::db::CartClipRecord>& clips,
        const QString& mode, const QString& color, QHash<qint64, int>& cursors);
};

} // namespace radio::audio
