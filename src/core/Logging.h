#pragma once

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QVector>

namespace radio::core {

enum class LogLevel { Debug, Info, Warning, Error };

QString logLevelToString(LogLevel level);

struct LogEntry {
    QDateTime timestamp;
    LogLevel level = LogLevel::Info;
    QString category;
    QString message;
    QString threadName;
    qint64 seq = 0;
};

// Permanent, always-compiled-in logging facility. Every subsystem routes
// through this so pipeline/scheduler/library failures are never silent.
// Not stripped by NDEBUG builds — visibility is a runtime toggle, not a
// compile-time one, since the debug console must always be available.
class Logger : public QObject {
    Q_OBJECT

public:
    static Logger& instance();

    // Fixed, predictable path a live `tail -f` can follow — see
    // m_liveLogFile's doc comment below for why this exists.
    static QString liveLogFilePath();

    void log(LogLevel level, const QString& category, const QString& message);

    void setMinLevel(LogLevel level);
    LogLevel minLevel() const;

    void setCategoryEnabled(const QString& category, bool enabled);
    bool isCategoryEnabled(const QString& category) const;

    // Thread-safe snapshot of the ring buffer, oldest first.
    QVector<LogEntry> entries() const;

    void clear();

signals:
    // Cross-thread safe: Qt auto-queues delivery to slots owned by a
    // different thread than the emitting one.
    void entryAdded(const radio::core::LogEntry& entry);
    void cleared();

private:
    Logger();
    ~Logger() override = default;
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    static constexpr int kRingCapacity = 10000;

    mutable QMutex m_mutex;
    QVector<LogEntry> m_ring;
    LogLevel m_minLevel = LogLevel::Debug;
    QSet<QString> m_disabledCategories;
    qint64 m_seq = 0;

    // Debug/Info lines written to m_liveLogFile since it was last flushed --
    // see log()'s doc comment on why those two levels don't flush on every
    // single write the way Warning/Error do.
    int m_unflushedDebugInfoLines = 0;
    static constexpr int kFlushEveryNLines = 20;

    // Every entry is also appended here immediately (opened truncated, once,
    // at construction) and flushed on every write — lets `tail -f` on this
    // fixed, predictable path show what the app is doing in real time,
    // rather than only being visible in-app via the Debug Console (which
    // requires the user to manually copy/export after the fact). Path is
    // exposed via liveLogFilePath() so the UI can surface/open it.
    QFile m_liveLogFile;
};

} // namespace radio::core

Q_DECLARE_METATYPE(radio::core::LogEntry)

#define RS_LOG_DEBUG(category, message) \
    ::radio::core::Logger::instance().log(::radio::core::LogLevel::Debug, (category), (message))
#define RS_LOG_INFO(category, message) \
    ::radio::core::Logger::instance().log(::radio::core::LogLevel::Info, (category), (message))
#define RS_LOG_WARN(category, message) \
    ::radio::core::Logger::instance().log(::radio::core::LogLevel::Warning, (category), (message))
#define RS_LOG_ERROR(category, message) \
    ::radio::core::Logger::instance().log(::radio::core::LogLevel::Error, (category), (message))
