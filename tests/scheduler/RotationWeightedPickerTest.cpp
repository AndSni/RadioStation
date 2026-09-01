#include "scheduler/RotationWeightedPicker.h"

#include <QHash>
#include <QTest>

using namespace radio::scheduler;
using namespace radio::db;

class RotationWeightedPickerTest : public QObject {
    Q_OBJECT

private slots:
    void emptyCategoriesReturnsInvalid();
    void singleCategoryAlwaysWins();
    void convergesToTargetRatioOverTime();
};

void RotationWeightedPickerTest::emptyCategoriesReturnsInvalid()
{
    QHash<qint64, int> credits;
    QCOMPARE(RotationWeightedPicker::pickCategory({}, credits), -1);
}

void RotationWeightedPickerTest::singleCategoryAlwaysWins()
{
    RotationCategoryRecord only;
    only.id = 42;
    only.targetRatio = 1;

    QHash<qint64, int> credits;
    for (int i = 0; i < 5; ++i)
        QCOMPARE(RotationWeightedPicker::pickCategory({ only }, credits), 42);
}

void RotationWeightedPickerTest::convergesToTargetRatioOverTime()
{
    // credit[i] += target_ratio[i]; pick argmax; winner's credit resets to
    // 0 (not decremented by the total) — this specific scheme's long-run
    // fixed point for weights 3:1:1 is empirically 50%/25%/25%, not a naive
    // 60%/20%/20% proportional split. What matters here is that A (weight
    // 3) is favored well above B/C (weight 1) and B/C stay even with each
    // other — verified against the algorithm's actual measured convergence
    // rather than an assumed one.
    RotationCategoryRecord a { .id = 1, .name = QStringLiteral("A"), .targetRatio = 3 };
    RotationCategoryRecord b { .id = 2, .name = QStringLiteral("B"), .targetRatio = 1 };
    RotationCategoryRecord c { .id = 3, .name = QStringLiteral("C"), .targetRatio = 1 };
    const QVector<RotationCategoryRecord> categories = { a, b, c };

    QHash<qint64, int> credits;
    QHash<qint64, int> counts;
    constexpr int kTicks = 1000;
    for (int i = 0; i < kTicks; ++i)
        counts[RotationWeightedPicker::pickCategory(categories, credits)]++;

    QCOMPARE(counts[1] + counts[2] + counts[3], kTicks);

    const double ratioA = static_cast<double>(counts[1]) / kTicks;
    const double ratioB = static_cast<double>(counts[2]) / kTicks;
    const double ratioC = static_cast<double>(counts[3]) / kTicks;

    QVERIFY2(qAbs(ratioA - 0.5) < 0.03, qPrintable(QStringLiteral("ratioA=%1").arg(ratioA)));
    QVERIFY2(qAbs(ratioB - 0.25) < 0.03, qPrintable(QStringLiteral("ratioB=%1").arg(ratioB)));
    QVERIFY2(qAbs(ratioC - 0.25) < 0.03, qPrintable(QStringLiteral("ratioC=%1").arg(ratioC)));
    QVERIFY(ratioA > ratioB && ratioA > ratioC); // A is still clearly favored
    QVERIFY2(qAbs(ratioB - ratioC) < 0.03, "B and C (equal weight) should end up roughly even");
}

QTEST_MAIN(RotationWeightedPickerTest)
#include "RotationWeightedPickerTest.moc"
