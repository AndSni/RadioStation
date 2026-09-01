#include "CartPicker.h"

#include <QRandomGenerator>

namespace radio::audio {

using radio::db::CartClipRecord;

CartClipRecord CartPicker::pick(qint64 blockId, const QVector<CartClipRecord>& clips, const QString& mode,
    const QString& color, QHash<qint64, int>& cursors)
{
    if (clips.isEmpty())
        return {};

    if (mode == QStringLiteral("sequence")) {
        int& cursor = cursors[blockId];
        cursor = cursor % clips.size();
        const CartClipRecord chosen = clips.at(cursor);
        cursor = (cursor + 1) % clips.size();
        return chosen;
    }

    if (mode == QStringLiteral("color_sequence")) {
        QVector<CartClipRecord> matching;
        for (const auto& clip : clips) {
            if (clip.color == color)
                matching.append(clip);
        }
        if (matching.isEmpty())
            return {};

        int& cursor = cursors[blockId];
        cursor = cursor % matching.size();
        const CartClipRecord chosen = matching.at(cursor);
        cursor = (cursor + 1) % matching.size();
        return chosen;
    }

    // "random" (default for any unrecognized mode string, defensively).
    const int choice = clips.size() > 1 ? static_cast<int>(QRandomGenerator::global()->bounded(clips.size())) : 0;
    return clips.at(choice);
}

} // namespace radio::audio
