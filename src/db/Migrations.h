#pragma once

class QSqlDatabase;

namespace radio::db {

class Migrations {
public:
    // Idempotent: creates/upgrades the schema as needed. Safe to call every
    // startup.
    static bool run(QSqlDatabase& db);
};

} // namespace radio::db
