#include "Database.h"
#include "Migrations.h"

#include "core/Logging.h"

#include <QDir>
#include <QSqlError>
#include <QStandardPaths>

namespace radio::db {

namespace {
const QString kConnectionName = QStringLiteral("radiostation");
}

// PRAGMA foreign_keys = ON was tried here and reverted -- turning on real
// enforcement isn't just a decorative-to-enforced backstop the way it first
// looked from the production delete paths alone (which do all already
// replicate the correct cascade/guard by hand -- see
// TrackRepository::syncMissingForRoot, PlaylistRepository::deletePlaylist,
// SmartPlaylistRepository::remove, PlaylistFolderRepository::deleteFolder).
// It also rejects INSERTs against a foreign key with no matching row, and
// this codebase deliberately relies on being able to create exactly that
// state: ScheduleBlockValidity's whole "target playlist deleted" health
// check exists to detect a schedule_blocks row whose playlist_id no longer
// resolves (see AutoDjPanelWidgetTest::blockTargetingDeletedPlaylistIsFlaggedBroken,
// which constructs that state directly via a nonexistent playlistId) --
// enforcing the FK at the schema level would make that state impossible to
// reach at all, contradicting a feature built specifically to handle it.
// Several other tests across the suite (TrackRepositoryTest,
// AutoDjEngineTest, BlockTransitionControllerTest, RadioStatisticsPanelTest)
// similarly use synthetic/arbitrary foreign key values for fixtures that
// don't need a real referenced row to exist for what they're actually
// testing. Left off; the existing hand-written cascades remain the correct
// mechanism here.
bool Database::open()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/library.sqlite3");

    QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), kConnectionName);
    db.setDatabaseName(path);
    if (!db.open()) {
        RS_LOG_ERROR("library.scan",
            QStringLiteral("Failed to open database at %1: %2").arg(path, db.lastError().text()));
        return false;
    }

    RS_LOG_INFO("library.scan", QStringLiteral("Database opened at %1").arg(path));
    return Migrations::run(db);
}

QSqlDatabase Database::handle()
{
    return QSqlDatabase::database(kConnectionName);
}

} // namespace radio::db
