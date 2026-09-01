#include "Logging.h"

#include <QDir>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>

namespace radio::core {

QString logLevelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug:
        return QStringLiteral("DEBUG");
    case LogLevel::Info:
        return QStringLiteral("INFO");
    case LogLevel::Warning:
        return QStringLiteral("WARN");
    case LogLevel::Error:
        return QStringLiteral("ERROR");
    }
    return QStringLiteral("?");
}

Logger::Logger()
{
    m_ring.reserve(kRingCapacity);

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    m_liveLogFile.setFileName(liveLogFilePath());
    // Truncate: a fresh file per process run, not an ever-growing one —
    // `tail -f` naturally follows a truncated-then-reopened file, and
    // there's no in-app viewer for old runs anyway (the ring buffer itself
    // doesn't survive a restart either).
    m_liveLogFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
}

Logger& Logger::instance()
{
    static Logger instance;
    return instance;
}

QString Logger::liveLogFilePath()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/live.log");
}

void Logger::log(LogLevel level, const QString& category, const QString& message)
{
    LogEntry entry;
    {
        QMutexLocker locker(&m_mutex);
        if (level < m_minLevel)
            return;
        if (m_disabledCategories.contains(category))
            return;

        entry.timestamp = QDateTime::currentDateTime();
        entry.level = level;
        entry.category = category;
        entry.message = message;
        entry.threadName = QThread::currentThread()->objectName();
        if (entry.threadName.isEmpty())
            entry.threadName = QStringLiteral("0x%1").arg(
                reinterpret_cast<quintptr>(QThread::currentThreadId()), 0, 16);
        entry.seq = ++m_seq;

        if (m_ring.size() >= kRingCapacity)
            m_ring.removeFirst();
        m_ring.append(entry);

        if (m_liveLogFile.isOpen()) {
            QTextStream out(&m_liveLogFile);
            out << entry.timestamp.toString(Qt::ISODateWithMs) << " [" << logLevelToString(entry.level) << "] "
                << entry.category << " [" << entry.threadName << "]: " << entry.message << '\n';
            out.flush(); // pushes the QTextStream's own buffer into the QFile -- always needed regardless of level

            // Warning/Error flush the QFile itself immediately (crash-
            // diagnostic guarantee: the on-disk file must have this line
            // even if the process crashes a moment later). Debug/Info are
            // far more frequent (deck play/pause, crossfade watch-tick
            // skip-logging, which can fire several times/sec) and don't
            // carry that same guarantee, so they're only flushed every
            // kFlushEveryNLines -- avoiding a real syscall+cross-thread-
            // mutex cost (this runs from both the UI thread and the engine
            // thread) on every routine debug line. A clean shutdown still
            // gets every buffered line via QFile's own destructor; only a
            // hard crash can lose the last few.
            if (level >= LogLevel::Warning) {
                m_liveLogFile.flush();
                m_unflushedDebugInfoLines = 0;
            } else if (++m_unflushedDebugInfoLines >= kFlushEveryNLines) {
                m_liveLogFile.flush();
                m_unflushedDebugInfoLines = 0;
            }
        }
    }
    emit entryAdded(entry);
}

void Logger::setMinLevel(LogLevel level)
{
    QMutexLocker locker(&m_mutex);
    m_minLevel = level;
}

LogLevel Logger::minLevel() const
{
    QMutexLocker locker(&m_mutex);
    return m_minLevel;
}

void Logger::setCategoryEnabled(const QString& category, bool enabled)
{
    QMutexLocker locker(&m_mutex);
    if (enabled)
        m_disabledCategories.remove(category);
    else
        m_disabledCategories.insert(category);
}

bool Logger::isCategoryEnabled(const QString& category) const
{
    QMutexLocker locker(&m_mutex);
    return !m_disabledCategories.contains(category);
}

QVector<LogEntry> Logger::entries() const
{
    QMutexLocker locker(&m_mutex);
    return m_ring;
}

void Logger::clear()
{
    {
        QMutexLocker locker(&m_mutex);
        m_ring.clear();
    }
    emit cleared();
}

} // namespace radio::core
