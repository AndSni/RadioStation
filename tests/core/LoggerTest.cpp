#include "core/Logging.h"

#include <QSignalSpy>
#include <QTest>

using namespace radio::core;

class LoggerTest : public QObject {
    Q_OBJECT

private slots:
    void init();

    void logAppendsEntry();
    void minLevelFiltersLowerSeverity();
    void disabledCategoryIsSuppressed();
    void clearEmptiesRingAndEmitsSignal();
};

void LoggerTest::init()
{
    Logger::instance().clear();
    Logger::instance().setMinLevel(LogLevel::Debug);
    Logger::instance().setCategoryEnabled(QStringLiteral("test"), true);
}

void LoggerTest::logAppendsEntry()
{
    QSignalSpy spy(&Logger::instance(), &Logger::entryAdded);
    Logger::instance().log(LogLevel::Info, QStringLiteral("test"), QStringLiteral("hello"));

    QCOMPARE(spy.count(), 1);
    const auto entries = Logger::instance().entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().message, QStringLiteral("hello"));
    QCOMPARE(entries.first().category, QStringLiteral("test"));
    QCOMPARE(entries.first().level, LogLevel::Info);
}

void LoggerTest::minLevelFiltersLowerSeverity()
{
    Logger::instance().setMinLevel(LogLevel::Warning);
    Logger::instance().log(LogLevel::Info, QStringLiteral("test"), QStringLiteral("should be dropped"));
    Logger::instance().log(LogLevel::Error, QStringLiteral("test"), QStringLiteral("should be kept"));

    const auto entries = Logger::instance().entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().message, QStringLiteral("should be kept"));
}

void LoggerTest::disabledCategoryIsSuppressed()
{
    Logger::instance().setCategoryEnabled(QStringLiteral("noisy"), false);
    Logger::instance().log(LogLevel::Error, QStringLiteral("noisy"), QStringLiteral("ignored"));
    Logger::instance().log(LogLevel::Error, QStringLiteral("test"), QStringLiteral("kept"));

    const auto entries = Logger::instance().entries();
    QCOMPARE(entries.size(), 1);
    QCOMPARE(entries.first().category, QStringLiteral("test"));
}

void LoggerTest::clearEmptiesRingAndEmitsSignal()
{
    Logger::instance().log(LogLevel::Info, QStringLiteral("test"), QStringLiteral("x"));
    QSignalSpy spy(&Logger::instance(), &Logger::cleared);

    Logger::instance().clear();

    QCOMPARE(spy.count(), 1);
    QCOMPARE(Logger::instance().entries().size(), 0);
}

QTEST_MAIN(LoggerTest)
#include "LoggerTest.moc"
