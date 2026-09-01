#include "AutoDjEngine.h"
#include "BlockTimeResolver.h"
#include "ClockEngine.h"
#include "RotationWeightedPicker.h"

#include "core/Logging.h"
#include "db/PlayHistoryRepository.h"
#include "db/PlaylistRepository.h"
#include "db/RotationCategoryRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "db/TrackRepository.h"

#include <QDateTime>
#include <QRandomGenerator>
#include <QTimer>
#include <algorithm>

namespace radio::scheduler {

using namespace radio::db;

namespace {
// candidatesForPlaylist() sorts by metric/direction in SQL; candidatesForSmartPlaylist()
// has no equivalent ORDER BY (its filter_json shape has nothing to sort on), so a
// smart-playlist-targeting block gets the same ordering applied client-side here instead --
// keeps "deterministic" mode and metric-biased "random" pools meaningful either way.
void sortCandidatesByMetric(QVector<TrackRecord>& candidates, SelectionMetric metric, SortDirection direction)
{
    if (metric == SelectionMetric::None)
        return;
    std::sort(candidates.begin(), candidates.end(), [metric, direction](const TrackRecord& a, const TrackRecord& b) {
        const int valueA = metric == SelectionMetric::Rating ? a.rating : a.playCount;
        const int valueB = metric == SelectionMetric::Rating ? b.rating : b.playCount;
        if (valueA != valueB)
            return direction == SortDirection::Descending ? valueA > valueB : valueA < valueB;
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0; // tie-break: title A-Z, matches the SQL path's tie-break
    });
}
}

AutoDjEngine::AutoDjEngine(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(5000);
    connect(m_timer, &QTimer::timeout, this, &AutoDjEngine::onTick);
}

void AutoDjEngine::start()
{
    // Deferred rather than called synchronously here: this runs from
    // MainWindow's constructor, before the window is ever painted --
    // running the first fill pass inline would block first paint on
    // however long it takes (library-size-dependent). QTimer::singleShot(0,
    // ...) still runs it before anything else gets a chance to touch the
    // queue, just on the first event-loop iteration instead of blocking the
    // caller.
    QTimer::singleShot(0, this, &AutoDjEngine::onTick);
    m_timer->start();
}

void AutoDjEngine::setQueueLowWatermark(int count)
{
    m_queueLowWatermark = count;
}

void AutoDjEngine::setNoRepeatTrackWindow(int count)
{
    m_noRepeatTrackWindow = count;
}

void AutoDjEngine::setNoRepeatArtistWindow(int count)
{
    m_noRepeatArtistWindow = count;
}

void AutoDjEngine::onTick()
{
    topUpQueue();
}

int AutoDjEngine::topUpQueue()
{
    return topUpQueueInternal(kResolveActiveBlockFromNow);
}

void AutoDjEngine::resetQueue()
{
    PlaylistRepository::clearQueue();
    RS_LOG_INFO("scheduler.autodj", QStringLiteral("Queue reset (Auto DJ fully resumed)"));
    topUpQueueInternal(kResolveActiveBlockFromNow);
}

void AutoDjEngine::resetQueueForBlock(qint64 blockId)
{
    PlaylistRepository::clearQueue();
    RS_LOG_INFO("scheduler.autodj", QStringLiteral("Queue reset, targeting block %1").arg(blockId));
    topUpQueueInternal(blockId);
}

int AutoDjEngine::topUpQueueInternal(qint64 forcedBlockId)
{
    const auto blocks = ScheduleBlockRepository::allBlocks();
    const qint64 targetBlockId = forcedBlockId != kResolveActiveBlockFromNow
        ? forcedBlockId
        : BlockTimeResolver::resolveActiveBlockId(blocks, QDateTime::currentDateTime());
    const auto it = targetBlockId >= 0
        ? std::find_if(blocks.begin(), blocks.end(), [targetBlockId](const auto& block) { return block.id == targetBlockId; })
        : blocks.end();

    int added = 0;
    if (m_clockEngine && it != blocks.end() && it->clockId >= 0) {
        // Clock-driven block: the depth target comes from where the wheel
        // currently is (see ClockEngine::fillLimit()'s doc comment), not
        // from queueMode/queueSize -- those fields are ignored for a
        // clock-driven block, same as playlistId/selectionMetric/etc.
        const int depthTarget = m_clockEngine->fillLimit(*it, QDateTime::currentDateTime());
        while (static_cast<int>(PlaylistRepository::queueItems().size()) < depthTarget) {
            if (generateNext(forcedBlockId) < 0) {
                emit starved();
                break;
            }
            ++added;
        }
    } else if (it != blocks.end() && it->queueMode == QStringLiteral("remaining")) {
        // Queue exactly enough, by summed duration, to cover however much
        // of the block is left — a block that hasn't started yet
        // (pre-staging, forcedBlockId is a real id) has no "remaining from
        // now" to measure against, so use its own full scheduled span
        // instead.
        const qint64 targetSeconds = forcedBlockId == kResolveActiveBlockFromNow
            ? BlockTimeResolver::secondsRemainingInBlock(*it, QDateTime::currentDateTime())
            : BlockTimeResolver::totalDurationSeconds(*it);
        const qint64 targetMs = targetSeconds * 1000;
        while (queuedDurationMs() < targetMs) {
            if (generateNext(forcedBlockId) < 0) {
                emit starved();
                break;
            }
            ++added;
        }
    } else {
        const int effectiveWatermark = (it != blocks.end() && it->queueSize > 0) ? it->queueSize : m_queueLowWatermark;
        while (static_cast<int>(PlaylistRepository::queueItems().size()) < effectiveWatermark) {
            if (generateNext(forcedBlockId) < 0) {
                emit starved();
                break;
            }
            ++added;
        }
    }
    if (added > 0)
        emit queueUpdated();
    return added;
}

qint64 AutoDjEngine::queuedDurationMs() const
{
    qint64 total = 0;
    for (const auto& item : PlaylistRepository::queueItems())
        total += item.durationMs;
    return total;
}

qint64 AutoDjEngine::generateNext(qint64 forcedBlockId)
{
    const auto blocks = ScheduleBlockRepository::allBlocks(); // ORDER BY position ASC
    const qint64 activeBlockId = forcedBlockId != kResolveActiveBlockFromNow
        ? forcedBlockId
        : BlockTimeResolver::resolveActiveBlockId(blocks, QDateTime::currentDateTime());
    if (activeBlockId >= 0) {
        const auto it = std::find_if(
            blocks.begin(), blocks.end(), [activeBlockId](const auto& block) { return block.id == activeBlockId; });
        if (it != blocks.end()) {
            const qint64 picked = generateNextFromBlock(*it);
            if (picked >= 0)
                return picked;
            // Block resolved but its playlist is exhausted even after
            // relaxing filters — treat the same as "no block matched" and
            // fall through to category-rotation below, rather than leaving
            // the queue starved while a usable fallback sits unused.
        }
    }

    const auto categories = RotationCategoryRepository::allCategories();
    if (categories.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj", QStringLiteral("No rotation categories exist; cannot pick a track"));
        return -1;
    }

    const qint64 categoryId = RotationWeightedPicker::pickCategory(categories, m_categoryCredits);

    QSet<qint64> excludeTrackIds = PlayHistoryRepository::recentTrackIds(m_noRepeatTrackWindow);
    QSet<QString> excludeArtists = PlayHistoryRepository::recentArtists(m_noRepeatArtistWindow);
    for (const auto& item : PlaylistRepository::queueItems()) {
        excludeTrackIds.insert(item.trackId); // don't queue something already queued
        // ...and don't stack the same artist back-to-back within a single
        // queue-fill pass either — recentArtists() only looks at PLAY
        // history, which is empty/stale for a queue that hasn't played yet,
        // so without this a large pool of never-played, same-artist tracks
        // (e.g. an artist folder scanned contiguously) can get picked
        // several times in a row while filling one queue.
        if (!item.artist.isEmpty())
            excludeArtists.insert(item.artist);
    }

    QVector<TrackRecord> candidates = TrackRepository::candidatesForCategory(categoryId, excludeTrackIds, excludeArtists);

    if (candidates.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj",
            QStringLiteral("No candidates in category %1 respecting no-repeat window; relaxing artist filter")
                .arg(categoryId));
        candidates = TrackRepository::candidatesForCategory(categoryId, excludeTrackIds, {});
    }

    if (candidates.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj",
            QStringLiteral("Still no candidates in category %1; relaxing track-repeat filter too").arg(categoryId));
        candidates = TrackRepository::candidatesForCategory(categoryId, {}, {});
    }

    if (candidates.isEmpty()) {
        RS_LOG_WARN(
            "scheduler.autodj", QStringLiteral("Category %1 is empty; falling back to any category").arg(categoryId));
        candidates = TrackRepository::candidatesForCategory(-1, {}, {});
    }

    if (candidates.isEmpty()) {
        RS_LOG_ERROR("scheduler.autodj", QStringLiteral("Library has no usable tracks at all — cannot fill queue"));
        return -1;
    }

    const int poolSize = std::min(static_cast<int>(candidates.size()), 10);
    const int choice = poolSize > 1 ? static_cast<int>(QRandomGenerator::global()->bounded(poolSize)) : 0;
    const TrackRecord& picked = candidates.at(choice);

    if (!PlaylistRepository::appendToQueue(picked.id, QStringLiteral("autodj")))
        return -1;

    RS_LOG_INFO("scheduler.autodj",
        QStringLiteral("Queued '%1' by %2 (category %3)").arg(picked.title, picked.artist).arg(categoryId));
    return picked.id;
}

qint64 AutoDjEngine::generateNextFromBlock(const radio::db::ScheduleBlockRecord& block)
{
    if (m_clockEngine && block.clockId >= 0) {
        QSet<qint64> excludeTrackIds = PlayHistoryRepository::recentTrackIds(m_noRepeatTrackWindow);
        QSet<QString> excludeArtists = PlayHistoryRepository::recentArtists(m_noRepeatArtistWindow);
        for (const auto& item : PlaylistRepository::queueItems()) {
            excludeTrackIds.insert(item.trackId);
            if (!item.artist.isEmpty())
                excludeArtists.insert(item.artist); // see generateNext()'s comment on the same pattern
        }
        // A negative return here (parked on a cart with no preceding music
        // element to fill from, or the clock Exhausted) falls through to
        // category rotation via generateNext()'s existing "block resolved
        // but exhausted" handling, exactly like a legacy block whose
        // playlist ran dry.
        return m_clockEngine->generateNextFromClock(block, QDateTime::currentDateTime(), excludeTrackIds, excludeArtists);
    }

    using radio::db::SelectionMetric;
    using radio::db::SortDirection;

    SelectionMetric metric = SelectionMetric::None;
    if (block.selectionMetric == QStringLiteral("rating"))
        metric = SelectionMetric::Rating;
    else if (block.selectionMetric == QStringLiteral("play_count"))
        metric = SelectionMetric::PlayCount;

    const SortDirection direction
        = block.selectionDirection == QStringLiteral("asc") ? SortDirection::Ascending : SortDirection::Descending;

    QSet<qint64> excludeTrackIds = PlayHistoryRepository::recentTrackIds(m_noRepeatTrackWindow);
    QSet<QString> excludeArtists = PlayHistoryRepository::recentArtists(m_noRepeatArtistWindow);
    for (const auto& item : PlaylistRepository::queueItems()) {
        excludeTrackIds.insert(item.trackId);
        if (!item.artist.isEmpty())
            excludeArtists.insert(item.artist); // see generateNext()'s comment on the same pattern
    }

    const bool targetsSmartPlaylist = block.smartPlaylistId >= 0;
    auto fetchCandidates = [&](const QSet<qint64>& excludeIds, const QSet<QString>& excludeArt) {
        QVector<TrackRecord> result = targetsSmartPlaylist
            ? TrackRepository::candidatesForSmartPlaylist(block.smartPlaylistId, excludeIds, excludeArt)
            : TrackRepository::candidatesForPlaylist(block.playlistId, metric, direction, excludeIds, excludeArt);
        if (targetsSmartPlaylist)
            sortCandidatesByMetric(result, metric, direction);
        return result;
    };

    QVector<TrackRecord> candidates = fetchCandidates(excludeTrackIds, excludeArtists);

    if (candidates.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj",
            QStringLiteral("No candidates in block '%1' respecting no-repeat window; relaxing artist filter")
                .arg(block.name));
        candidates = fetchCandidates(excludeTrackIds, {});
    }

    if (candidates.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj",
            QStringLiteral("Still no candidates in block '%1'; relaxing track-repeat filter too").arg(block.name));
        candidates = fetchCandidates({}, {});
    }

    if (candidates.isEmpty()) {
        RS_LOG_WARN("scheduler.autodj",
            QStringLiteral("Block '%1''s %2 is empty or exhausted")
                .arg(block.name, targetsSmartPlaylist ? QStringLiteral("smart playlist") : QStringLiteral("playlist")));
        return -1;
    }

    qint64 pickedId;
    QString pickedTitle;
    QString pickedArtist;
    if (block.selectionMode == QStringLiteral("deterministic")) {
        const TrackRecord& picked = candidates.first();
        pickedId = picked.id;
        pickedTitle = picked.title;
        pickedArtist = picked.artist;
    } else {
        // "Random" (metric == None) picks over the WHOLE candidate list —
        // there's no meaningful "top" to bias toward without a sort metric.
        // The metric-driven "...random" variants bias toward a top-10 pool,
        // same constant as the category-rotation path above.
        const int poolSize = metric == SelectionMetric::None ? static_cast<int>(candidates.size())
                                                               : std::min(static_cast<int>(candidates.size()), 10);
        const int choice = poolSize > 1 ? static_cast<int>(QRandomGenerator::global()->bounded(poolSize)) : 0;
        const TrackRecord& picked = candidates.at(choice);
        pickedId = picked.id;
        pickedTitle = picked.title;
        pickedArtist = picked.artist;
    }

    if (!PlaylistRepository::appendToQueue(pickedId, QStringLiteral("autodj")))
        return -1;

    RS_LOG_INFO("scheduler.autodj",
        QStringLiteral("Queued '%1' by %2 (block '%3')").arg(pickedTitle, pickedArtist, block.name));
    return pickedId;
}

} // namespace radio::scheduler
