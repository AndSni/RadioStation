#pragma once

#include <QSqlDatabase>

namespace radio::db {

// Single UI-thread-only SQLite connection. All QSqlDatabase access in this
// application happens on the UI thread — MetadataScanner extracts on its
// own worker thread and hands results back via queued signals for the UI
// thread to write, avoiding Qt/SQLite cross-thread-connection pitfalls.
class Database {
public:
    // Opens (creating if needed) ~/.local/share/RadioStation/library.sqlite3
    // and runs migrations. Call once at startup, on the UI thread.
    static bool open();
    static QSqlDatabase handle();
};

} // namespace radio::db
