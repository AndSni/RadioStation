#pragma once

#include <QString>
#include <QVector>

namespace radio::db {

struct CartClipRecord {
    qint64 id = -1;
    QString filePath;
    QString label;
    QString color;
    int slotRow = 0;
    int slotCol = 0;
    QString hotkey; // e.g. "F1", "Ctrl+1" — a QKeySequence-parseable string
    qint64 durationMs = 0;
    bool inBetweenOnly = false; // true = never used for schedule-automation overlays, only solo insert/clock plays
};

// All methods must run on the UI thread (see Database.h).
class CartRepository {
public:
    static QVector<CartClipRecord> allClips();
    static qint64 addClip(
        const QString& filePath, const QString& label, const QString& color, int row, int col,
        const QString& hotkey, qint64 durationMs, bool inBetweenOnly = false);
    static bool updateClip(const CartClipRecord& clip);
    static bool removeClip(qint64 id);
};

} // namespace radio::db
