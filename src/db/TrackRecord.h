#pragma once

#include <QDateTime>
#include <QMetaType>
#include <QString>

namespace radio::db {

struct TrackRecord {
    qint64 id = -1;
    QString filePath;
    QString title;
    QString artist;
    QString album;
    QString genre;
    qint64 durationMs = 0;
    QString codec;
    int sampleRate = 0;
    int channels = 0;
    int bitrate = 0;
    qint64 categoryId = -1; // -1 == uncategorized
    QDateTime addedAt;
    QDateTime lastPlayedAt;
    int playCount = 0;
    qint64 fileMtime = 0;
    bool missing = false;
    int rating = 0; // 0 = unrated, 1-5 stars
    int year = 0; // 0 = unknown
    double energy = -1.0; // -1 = not analyzed; else 0..1 normalized composite score
    double bpm = -1.0; // -1 = not analyzed; else tempo, independent of energy
    // Naming matches GstSourcePipeline's hasReplayGain()/replayGainDb()/
    // replayGainPeak() triad. false until either MetadataScanner finds an
    // embedded GST_TAG_TRACK_GAIN during a normal scan, or
    // ReplayGainAnalyzer computes and writes one.
    bool hasReplayGain = false;
    double replayGainDb = 0.0;
    double replayGainPeak = 0.0;
};

struct RotationCategoryRecord {
    qint64 id = -1;
    QString name;
    QString color;
    int targetRatio = 1;
};

struct QueueItemRecord {
    qint64 playlistItemId = -1;
    qint64 trackId = -1;
    QString filePath;
    QString title;
    QString artist;
    int position = 0;
    // "manual" | "autodj" | "clock:<clockElementId>" -- the clock-tagged
    // form is written when a clock-driven schedule block queues a track
    // from one of its elements (see ClockEngine::generateNextFromClock()),
    // so QueueWidget can show which wheel slot a queued item came from.
    QString source;
    qint64 durationMs = 0;
    bool missing = false;
};

struct PlaylistFolderRecord {
    qint64 id = -1;
    QString name;
    int position = 0;
};

struct PlaylistRecord {
    qint64 id = -1;
    QString name;
    qint64 folderId = -1; // -1 == not in a folder
    int trackCount = 0; // computed via COUNT join, not a stored column
};

struct PlaylistItemRecord {
    qint64 playlistItemId = -1;
    qint64 trackId = -1;
    QString filePath;
    QString title;
    QString artist;
    int position = 0;
    // "manual" | "autodj" | "import" | "clock:<clockElementId>" -- see
    // QueueItemRecord::source's doc comment for the clock-tagged form.
    QString source;
    qint64 durationMs = 0;
    bool missing = false;
};

struct ScheduleBlockRecord {
    qint64 id = -1;
    QString name;
    int position = 0; // priority; lower = topmost = wins on overlap
    bool enabled = true;
    int daysMask = 0; // bit0=Mon..bit6=Sun
    int startMinute = 0; // 0..1439
    int endMinute = 0; // 1..1440; end <= start crosses midnight; (0,1440) = All Day
    qint64 playlistId = -1;
    QString selectionMetric = QStringLiteral("none"); // "none" | "rating" | "play_count"
    QString selectionDirection = QStringLiteral("desc"); // "asc" | "desc"
    QString selectionMode = QStringLiteral("random_pool"); // "deterministic" | "random_pool"
    int cartFrequency = 0; // 0 = disabled; fire a cart after every Nth song played in this block
    QString cartMode = QStringLiteral("random"); // "random" | "sequence" | "color_sequence"
    QString cartColor; // used when cartMode == "color_sequence"; matched against cart_clips.color
    int cartOffsetSeconds = 0; // fire this many seconds after a deck's auto-crossfade finishes
    QString cartPlaybackType = QStringLiteral("overlay"); // "overlay" (mixes in parallel) | "insert" (deck pauses, cart plays solo, deck resumes)
    QString queueMode = QStringLiteral("count"); // "count" | "remaining"
    int queueSize = 0; // 0 = inherit AutoDjEngine's global default watermark; only meaningful in "count" mode
    qint64 smartPlaylistId = -1; // -1 == use playlistId (static); >=0 == target this smart playlist instead
    // -1 == this block behaves exactly as above (unchanged). >=0 == this
    // block is CLOCK-DRIVEN: content comes from walking clockId's ordered
    // clock_elements sequence (see ClockEngine) instead of playlistId/
    // smartPlaylistId/selectionMetric/cartFrequency/etc, which are then
    // simply ignored -- same mutual-exclusion-via-sentinel idiom as
    // smartPlaylistId vs playlistId above, just a third option.
    qint64 clockId = -1;
};

// A reusable, named hour template (see ClockWheel/ClockEngine in
// rs_scheduler) -- assign via ScheduleBlockRecord::clockId to any number of
// schedule blocks/dayparts that should share the same intra-hour structure.
struct ClockRecord {
    qint64 id = -1;
    QString name;
};

// One ordered slot within a clock. position is the ordering within the
// clock (like ScheduleBlockRecord::position, but scoped to one clock).
// minute_offset/timing_mode/elementType are documented in full in
// ClockWheel.h, which is where they're actually interpreted.
struct ClockElementRecord {
    qint64 id = -1;
    qint64 clockId = -1;
    int position = 0;
    // "music_playlist" | "music_smart_playlist" | "cart_random" |
    // "cart_color" | "cart_specific"
    QString elementType;
    QString label;
    int itemCount = 1; // how many music tracks this slot represents before the wheel advances past it (ignored for cart_* types)
    int minuteOffset = -1; // -1 == flow (play immediately after the previous element); 0..59 == minute-of-hour this element is timed against
    QString timingMode = QStringLiteral("soft"); // "soft" | "hard"; ignored when minuteOffset < 0
    qint64 playlistId = -1;
    qint64 smartPlaylistId = -1;
    QString selectionMetric = QStringLiteral("none");
    QString selectionDirection = QStringLiteral("desc");
    QString selectionMode = QStringLiteral("random_pool");
    QString cartMode = QStringLiteral("random"); // "random" | "color_sequence" -- only meaningful for cart_random/cart_color
    QString cartColor;
    qint64 cartClipId = -1; // only meaningful for cart_specific
};

// Persists "where is this clock-driven block's wheel right now", read once
// at block activation/resume and written on every advance -- see
// ClockEngine. hourStartEpoch is UTC epoch seconds of the top of the local
// hour this wheel pass belongs to (see ClockWheel.h for why the wheel is
// hour-anchored, not block-anchored).
struct ClockWheelStateRecord {
    qint64 scheduleBlockId = -1;
    qint64 clockId = -1;
    qint64 hourStartEpoch = 0;
    int position = 0;
    int itemsDone = 0;
};

// A saved Type/Genre/Era/Energy/BPM query. filterJson is a JSON blob (see
// SmartPlaylistFilter::matches()) rather than discrete columns — the filter
// shape (genre text, a set of category ids, year/energy/bpm ranges) is
// inherently variable, so a blob keeps CRUD trivial and keeps the filter's
// shape entirely in C++.
struct SmartPlaylistRecord {
    qint64 id = -1;
    QString name;
    QString filterJson = QStringLiteral("{}");
};

} // namespace radio::db

Q_DECLARE_METATYPE(radio::db::TrackRecord)
