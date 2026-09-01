#pragma once

#include "TrackRecord.h"

#include <QVector>

namespace radio::db {

// All methods must run on the UI thread (see Database.h).
class RotationCategoryRepository {
public:
    static QVector<RotationCategoryRecord> allCategories();
    static qint64 addCategory(const QString& name, const QString& color, int targetRatio);
};

} // namespace radio::db
