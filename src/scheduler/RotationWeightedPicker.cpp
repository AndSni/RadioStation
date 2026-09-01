#include "RotationWeightedPicker.h"

namespace radio::scheduler {

using radio::db::RotationCategoryRecord;

qint64 RotationWeightedPicker::pickCategory(const QVector<RotationCategoryRecord>& categories, QHash<qint64, int>& credits)
{
    if (categories.isEmpty())
        return -1;

    for (const auto& category : categories)
        credits[category.id] += category.targetRatio;

    qint64 bestId = categories.first().id;
    int bestCredit = credits.value(bestId, 0);
    for (const auto& category : categories) {
        const int credit = credits.value(category.id, 0);
        if (credit > bestCredit) {
            bestCredit = credit;
            bestId = category.id;
        }
    }

    credits[bestId] = 0;
    return bestId;
}

} // namespace radio::scheduler
