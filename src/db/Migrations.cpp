#include "Migrations.h"

#include "core/Logging.h"

#include <QDateTime>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>

namespace radio::db {

namespace {

bool exec(QSqlDatabase& db, const QString& sql)
{
    QSqlQuery query(db);
    if (!query.exec(sql)) {
        RS_LOG_ERROR("library.scan", QStringLiteral("Migration statement failed: %1 (%2)").arg(query.lastError().text(), sql));
        return false;
    }
    return true;
}

int currentVersion(QSqlDatabase& db)
{
    QSqlQuery query(db);
    if (!query.exec(QStringLiteral("SELECT MAX(version) FROM schema_migrations")))
        return 0;
    if (query.next() && !query.value(0).isNull())
        return query.value(0).toInt();
    return 0;
}

// ALTER TABLE ADD COLUMN isn't naturally idempotent in SQLite (unlike the
// CREATE TABLE IF NOT EXISTS / INSERT OR IGNORE statements v1 uses
// exclusively) — re-running it after it already succeeded throws "duplicate
// column name". table is always a fixed internal literal at every call
// site below, never external input — PRAGMA doesn't support bind
// parameters, so this interpolation is safe by construction.
bool columnExists(QSqlDatabase& db, const QString& table, const QString& column)
{
    QSqlQuery query(db);
    query.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table));
    while (query.next()) {
        if (query.value(QStringLiteral("name")).toString().compare(column, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

bool applyV1(QSqlDatabase& db)
{
    static const QStringList statements = {
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS tracks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL UNIQUE,
            title TEXT, artist TEXT, album TEXT, genre TEXT,
            duration_ms INTEGER, codec TEXT, sample_rate INTEGER, channels INTEGER, bitrate INTEGER,
            category_id INTEGER REFERENCES rotation_categories(id) ON DELETE SET NULL,
            added_at TEXT, last_played_at TEXT, play_count INTEGER DEFAULT 0,
            file_mtime INTEGER, missing INTEGER DEFAULT 0
        ))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_artist ON tracks(artist)"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_tracks_category ON tracks(category_id)"),

        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS rotation_categories (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL UNIQUE, color TEXT, target_ratio INTEGER DEFAULT 1
        ))"),

        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS playlists (
            id INTEGER PRIMARY KEY AUTOINCREMENT, name TEXT NOT NULL, is_queue INTEGER DEFAULT 0
        ))"),
        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS playlist_items (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
            track_id INTEGER NOT NULL REFERENCES tracks(id) ON DELETE CASCADE,
            position INTEGER NOT NULL, source TEXT DEFAULT 'manual'
        ))"),
        QStringLiteral("CREATE INDEX IF NOT EXISTS idx_playlist_items_playlist ON playlist_items(playlist_id, position)"),

        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS cart_clips (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            file_path TEXT NOT NULL, label TEXT, color TEXT,
            slot_row INTEGER, slot_col INTEGER, hotkey TEXT, duration_ms INTEGER
        ))"),

        QStringLiteral(R"(CREATE TABLE IF NOT EXISTS play_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            track_id INTEGER NOT NULL REFERENCES tracks(id),
            played_at TEXT NOT NULL, deck TEXT
        ))"),

        QStringLiteral("INSERT OR IGNORE INTO rotation_categories (name, color, target_ratio) VALUES ('Music', '#3b82f6', 3)"),
        QStringLiteral("INSERT OR IGNORE INTO rotation_categories (name, color, target_ratio) VALUES ('Jingles', '#f59e0b', 1)"),
        QStringLiteral("INSERT OR IGNORE INTO rotation_categories (name, color, target_ratio) VALUES ('Uncategorized', '#6b7280', 1)"),
    };

    for (const QString& statement : statements) {
        if (!exec(db, statement))
            return false;
    }
    return true;
}

// Adds a real multi-playlist system (organizational folders for playlists)
// and track ratings. Both ALTER TABLEs are columnExists()-guarded so a crash
// between them — or between one and the CREATE TABLE — leaves a safely
// retryable state, matching v1's own fully-safe-to-retry design.
bool applyV2(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("rating"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN rating INTEGER NOT NULL DEFAULT 0")))
            return false;
    }

    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS playlist_folders (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            position INTEGER NOT NULL DEFAULT 0
        ))")))
        return false;

    if (!columnExists(db, QStringLiteral("playlists"), QStringLiteral("folder_id"))) {
        if (!exec(db, QStringLiteral(
                "ALTER TABLE playlists ADD COLUMN folder_id INTEGER REFERENCES playlist_folders(id) ON DELETE SET NULL")))
            return false;
    }

    return true;
}

// Schedule blocks: named day/time windows AutoDjEngine picks from before
// falling back to plain category rotation (see BlockTimeResolver in
// rs_scheduler). A brand-new table, so CREATE TABLE IF NOT EXISTS is
// already idempotent — no columnExists() guard needed, matching v1.
bool applyV3(QSqlDatabase& db)
{
    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS schedule_blocks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            position INTEGER NOT NULL,
            enabled INTEGER NOT NULL DEFAULT 1,
            days_mask INTEGER NOT NULL DEFAULT 0,
            start_minute INTEGER NOT NULL,
            end_minute INTEGER NOT NULL,
            playlist_id INTEGER NOT NULL REFERENCES playlists(id) ON DELETE CASCADE,
            selection_metric TEXT NOT NULL DEFAULT 'none',
            selection_direction TEXT NOT NULL DEFAULT 'desc',
            selection_mode TEXT NOT NULL DEFAULT 'random_pool'
        ))")))
        return false;

    if (!exec(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_schedule_blocks_position ON schedule_blocks(position)")))
        return false;

    return true;
}

// Cart Wall automation fields, additive on schedule_blocks. Each ALTER
// TABLE is columnExists()-guarded, matching v2's idempotency practice.
bool applyV4(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("cart_frequency"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN cart_frequency INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("cart_mode"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN cart_mode TEXT NOT NULL DEFAULT 'random'")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("cart_color"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN cart_color TEXT")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("cart_offset_seconds"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN cart_offset_seconds INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    return true;
}

// Cart playback type: 'overlay' (mixes in parallel with the deck — the
// original/default behavior) vs 'insert' (deck pauses while the cart plays
// solo, then resumes — see CartAutomationEngine).
bool applyV5(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("cart_playback_type"))) {
        if (!exec(db,
                QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN cart_playback_type TEXT NOT NULL DEFAULT 'overlay'")))
            return false;
    }
    return true;
}

// Per-block queue depth: 'count' (keep queue_size tracks queued — 0 means
// inherit AutoDjEngine's own global default) vs 'remaining' (queue exactly
// enough, by summed duration, to cover the block's remaining/total time —
// see AutoDjEngine::topUpQueueInternal()).
bool applyV6(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("queue_mode"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN queue_mode TEXT NOT NULL DEFAULT 'count'")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("queue_size"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE schedule_blocks ADD COLUMN queue_size INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    return true;
}

// Smart playlists: year/energy/bpm on tracks (populated by the library
// cleanup pipeline's manifest.csv, see MetadataScanner/ImportDialog), a
// smart_playlists table (filter_json is a single JSON blob rather than
// discrete columns or a join table — the filter shape is inherently
// variable, matching this codebase's existing appetite for flexible
// TEXT-with-known-shape columns like ScheduleBlockRecord::selectionMetric),
// and an optional smart-playlist target on schedule_blocks alongside the
// existing static playlist_id. The seeded 'Music' category is renamed to
// 'Song' (plain UPDATE, naturally idempotent) since the cleanup pipeline's
// manifest only ever produces Type=Song rows now — reusing its existing
// targetRatio=3 weighting rather than seeding a redundant category.
bool applyV7(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("year"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN year INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("energy"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN energy REAL NOT NULL DEFAULT -1")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("bpm"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN bpm REAL NOT NULL DEFAULT -1")))
            return false;
    }

    if (!exec(db, QStringLiteral("UPDATE rotation_categories SET name = 'Song' WHERE name = 'Music'")))
        return false;

    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS smart_playlists (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            filter_json TEXT NOT NULL DEFAULT '{}'
        ))")))
        return false;

    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("smart_playlist_id"))) {
        if (!exec(db, QStringLiteral(
                "ALTER TABLE schedule_blocks ADD COLUMN smart_playlist_id INTEGER REFERENCES smart_playlists(id) ON DELETE SET NULL")))
            return false;
    }

    return true;
}

// ReplayGain fields on tracks, populated two ways: MetadataScanner reads an
// already-embedded GST_TAG_TRACK_GAIN/_PEAK during any normal scan (free —
// the tag list is already being pulled), and the "Analyze Loudness"
// feature (ReplayGainAnalyzer) computes and writes real tags into files
// that still lack one, then records the result here immediately rather
// than waiting for a future rescan to notice.
bool applyV8(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("has_replay_gain"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN has_replay_gain INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("replay_gain_db"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN replay_gain_db REAL NOT NULL DEFAULT 0")))
            return false;
    }
    if (!columnExists(db, QStringLiteral("tracks"), QStringLiteral("replay_gain_peak"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE tracks ADD COLUMN replay_gain_peak REAL NOT NULL DEFAULT 0")))
            return false;
    }
    return true;
}

// Resets bpm/ReplayGain to "not analyzed" for every track, unconditionally.
// Neither field previously had any way to distinguish a value RadioStation
// (or the trusted tools/library_cleanup pipeline) actually computed from one
// a file simply arrived with from wherever it was originally sourced --
// MetadataScanner trusted any embedded TBPM/REPLAYGAIN_TRACK_GAIN tag
// unconditionally. From this version on, both are only trusted when a new
// RS_VERIFIED_FIELDS marker tag is also present (see VerifiedFieldsTag,
// ReplayGainAnalyzer, the new BpmAnalyzer) -- since nothing before this
// migration ever wrote that marker, every existing value (including the
// ones ReplayGainAnalyzer itself already wrote pre-marker) is equally
// unverifiable and gets wiped here. Energy is deliberately NOT touched --
// unlike bpm/ReplayGain, it was never read from an embedded tag in the
// first place (only from the trusted manifest.csv merge or EnergyAnalyzer's
// own DB-only computation), so it was never exposed to this problem.
bool applyV9(QSqlDatabase& db)
{
    if (!exec(db, QStringLiteral("UPDATE tracks SET bpm = -1")))
        return false;
    if (!exec(db, QStringLiteral("UPDATE tracks SET has_replay_gain = 0, replay_gain_db = 0, replay_gain_peak = 0")))
        return false;
    return true;
}

// Index on play_history.played_at: PlayHistoryRepository::recentTrackIds()/
// recentArtists() both run `ORDER BY played_at DESC LIMIT :n` against this
// ever-growing table, and AutoDjEngine's separation check calls them every
// 5 seconds for the life of the process -- without an index this was a full
// table scan plus sort on every call.
bool applyV10(QSqlDatabase& db)
{
    return exec(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_play_history_played_at ON play_history(played_at)"));
}

// EBU R128/LUFS measurement history (see MixEngine's momentaryLoudnessLufs()/
// shortTermLoudnessLufs()/integratedLoudnessLufs()/outputTruePeakDb()) —
// written periodically by AudioEngine's coarse logging timer while
// streaming, via LoudnessHistoryRepository, so an operator can review a
// broadcast's loudness history after the fact rather than only ever seeing
// the live meter.
bool applyV11(QSqlDatabase& db)
{
    return exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS loudness_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            measured_at TEXT NOT NULL,
            integrated_lufs REAL NOT NULL,
            momentary_lufs REAL NOT NULL,
            short_term_lufs REAL NOT NULL,
            true_peak_dbfs REAL NOT NULL
        ))"));
}

// Real broadcast "clock wheel" support (Phase 4): a clock is a reusable,
// named hour template (clocks + clock_elements, ordered by position within
// the clock), assignable to a schedule_blocks row via the new nullable
// clock_id column -- NULL means "unchanged, existing playlist/selection/
// cart-frequency behavior", exactly the same mutual-exclusion-via-sentinel
// idiom playlist_id/smart_playlist_id already establish. clock_wheel_state
// persists "where is this block's wheel right now" (hour_start_epoch +
// position + items_done) so a restart resumes mid-hour instead of
// restarting the wheel from element 0 -- see ClockWheel/ClockEngine in
// rs_scheduler for the hour-anchored resume/restart logic this table
// backs. FK enforcement is off in this app by deliberate design (see
// Database.cpp), so every REFERENCES/CASCADE below is documentation only;
// ClockRepository::deleteClock() hand-cascades.
bool applyV12(QSqlDatabase& db)
{
    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS clocks (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL
        ))")))
        return false;

    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS clock_elements (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            clock_id INTEGER NOT NULL REFERENCES clocks(id) ON DELETE CASCADE,
            position INTEGER NOT NULL,
            element_type TEXT NOT NULL,
            label TEXT,
            item_count INTEGER NOT NULL DEFAULT 1,
            minute_offset INTEGER,
            timing_mode TEXT NOT NULL DEFAULT 'soft',
            playlist_id INTEGER REFERENCES playlists(id) ON DELETE SET NULL,
            smart_playlist_id INTEGER REFERENCES smart_playlists(id) ON DELETE SET NULL,
            selection_metric TEXT NOT NULL DEFAULT 'none',
            selection_direction TEXT NOT NULL DEFAULT 'desc',
            selection_mode TEXT NOT NULL DEFAULT 'random_pool',
            cart_mode TEXT NOT NULL DEFAULT 'random',
            cart_color TEXT,
            cart_clip_id INTEGER REFERENCES cart_clips(id) ON DELETE SET NULL
        ))")))
        return false;

    if (!exec(db, QStringLiteral("CREATE INDEX IF NOT EXISTS idx_clock_elements_clock ON clock_elements(clock_id, position)")))
        return false;

    if (!columnExists(db, QStringLiteral("schedule_blocks"), QStringLiteral("clock_id"))) {
        if (!exec(db, QStringLiteral(
                "ALTER TABLE schedule_blocks ADD COLUMN clock_id INTEGER REFERENCES clocks(id) ON DELETE SET NULL")))
            return false;
    }

    if (!exec(db, QStringLiteral(R"(CREATE TABLE IF NOT EXISTS clock_wheel_state (
            schedule_block_id INTEGER PRIMARY KEY REFERENCES schedule_blocks(id) ON DELETE CASCADE,
            clock_id INTEGER NOT NULL REFERENCES clocks(id) ON DELETE CASCADE,
            hour_start_epoch INTEGER NOT NULL,
            position INTEGER NOT NULL,
            items_done INTEGER NOT NULL DEFAULT 0,
            updated_at TEXT NOT NULL
        ))")))
        return false;

    return true;
}

// FTS5 index over tracks.title/artist/album/genre, replacing the leading-
// wildcard `LIKE '%q%'` TrackRepository::searchTracks() used to run (a full
// table scan, unindexable in SQLite regardless of what B-tree indexes
// exist -- a real, growing stall on every keystroke at real-world library
// sizes). External-content table (content='tracks'/content_rowid='id'):
// FTS5 stores only its own token index, not a second copy of the text, kept
// in sync via triggers rather than the app writing to two tables. The
// 'delete' rows in tracks_ad/tracks_au are FTS5's own external-content
// deletion idiom -- it needs the OLD text to remove the right tokens, not
// just the rowid. The backfill INSERT covers rows that already existed
// before this migration ran (a fresh virtual table starts empty regardless
// of what's already in tracks).
bool applyV13(QSqlDatabase& db)
{
    if (!exec(db, QStringLiteral(R"(CREATE VIRTUAL TABLE IF NOT EXISTS tracks_fts USING fts5(
            title, artist, album, genre,
            content='tracks', content_rowid='id'
        ))")))
        return false;

    if (!exec(db, QStringLiteral(R"(CREATE TRIGGER IF NOT EXISTS tracks_fts_ai AFTER INSERT ON tracks BEGIN
            INSERT INTO tracks_fts(rowid, title, artist, album, genre)
            VALUES (new.id, new.title, new.artist, new.album, new.genre);
        END)")))
        return false;

    if (!exec(db, QStringLiteral(R"(CREATE TRIGGER IF NOT EXISTS tracks_fts_ad AFTER DELETE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre)
            VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre);
        END)")))
        return false;

    if (!exec(db, QStringLiteral(R"(CREATE TRIGGER IF NOT EXISTS tracks_fts_au AFTER UPDATE ON tracks BEGIN
            INSERT INTO tracks_fts(tracks_fts, rowid, title, artist, album, genre)
            VALUES ('delete', old.id, old.title, old.artist, old.album, old.genre);
            INSERT INTO tracks_fts(rowid, title, artist, album, genre)
            VALUES (new.id, new.title, new.artist, new.album, new.genre);
        END)")))
        return false;

    if (!exec(db, QStringLiteral(
            "INSERT INTO tracks_fts(rowid, title, artist, album, genre) SELECT id, title, artist, album, genre FROM tracks")))
        return false;

    return true;
}

// Per-cart "in-between only" flag: some jingles carry their own musical bed,
// so overlaying them on a song still going out clashes. A clip with
// in_between_only = 1 is excluded from the schedule-automation overlay pool
// (see CartAutomationEngine::onAutoCrossfadeFinished) but stays eligible for
// the solo insert/clock paths that play between songs. columnExists-guarded
// like every other ALTER since v2 so a crash mid-migration is retryable.
bool applyV14(QSqlDatabase& db)
{
    if (!columnExists(db, QStringLiteral("cart_clips"), QStringLiteral("in_between_only"))) {
        if (!exec(db, QStringLiteral("ALTER TABLE cart_clips ADD COLUMN in_between_only INTEGER NOT NULL DEFAULT 0")))
            return false;
    }
    return true;
}

} // namespace

bool Migrations::run(QSqlDatabase& db)
{
    if (!exec(db, QStringLiteral("CREATE TABLE IF NOT EXISTS schema_migrations (version INTEGER PRIMARY KEY, applied_at TEXT)")))
        return false;

    const int version = currentVersion(db);
    if (version < 1) {
        if (!applyV1(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (1, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v1: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v1"));
    }
    if (version < 2) {
        if (!applyV2(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (2, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v2: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v2"));
    }
    if (version < 3) {
        if (!applyV3(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (3, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v3: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v3"));
    }
    if (version < 4) {
        if (!applyV4(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (4, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v4: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v4"));
    }
    if (version < 5) {
        if (!applyV5(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (5, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v5: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v5"));
    }
    if (version < 6) {
        if (!applyV6(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (6, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v6: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v6"));
    }
    if (version < 7) {
        if (!applyV7(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (7, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v7: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v7"));
    }
    if (version < 8) {
        if (!applyV8(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (8, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v8: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v8"));
    }
    if (version < 9) {
        if (!applyV9(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (9, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v9: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v9"));
    }
    if (version < 10) {
        if (!applyV10(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (10, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v10: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v10"));
    }
    if (version < 11) {
        if (!applyV11(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (11, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v11: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v11"));
    }
    if (version < 12) {
        if (!applyV12(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (12, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v12: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v12"));
    }
    if (version < 13) {
        if (!applyV13(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (13, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v13: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v13"));
    }
    if (version < 14) {
        if (!applyV14(db))
            return false;
        QSqlQuery query(db);
        query.prepare(QStringLiteral("INSERT INTO schema_migrations (version, applied_at) VALUES (14, :now)"));
        query.bindValue(QStringLiteral(":now"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
        if (!query.exec()) {
            RS_LOG_ERROR("library.scan", QStringLiteral("Failed to record migration v14: %1").arg(query.lastError().text()));
            return false;
        }
        RS_LOG_INFO("library.scan", QStringLiteral("Applied database migration v14"));
    }

    return true;
}

} // namespace radio::db
