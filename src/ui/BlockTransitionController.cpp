#include "BlockTransitionController.h"

#include "audio/CrossfadeController.h"
#include "core/Logging.h"
#include "db/ScheduleBlockRepository.h"
#include "scheduler/AutoDjEngine.h"
#include "scheduler/BlockTimeResolver.h"
#include "scheduler/ClockEngine.h"

#include <QDateTime>
#include <QTimer>
#include <algorithm>

namespace radio::ui {

using namespace radio::db;
using radio::audio::CrossfadeController;
using radio::scheduler::AutoDjEngine;
using radio::scheduler::BlockTimeResolver;
using radio::scheduler::ClockEngine;

BlockTransitionController::BlockTransitionController(
    CrossfadeController* crossfadeController, AutoDjEngine* autoDjEngine, QObject* parent)
    : QObject(parent)
    , m_crossfadeController(crossfadeController)
    , m_autoDjEngine(autoDjEngine)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(1000);
    connect(m_timer, &QTimer::timeout, this, &BlockTransitionController::onTick);
    m_timer->start();
}

void BlockTransitionController::setLeadSeconds(int seconds)
{
    m_leadSeconds = seconds;
}

void BlockTransitionController::onTick()
{
    evaluateAt(QDateTime::currentDateTime());
}

void BlockTransitionController::evaluateAt(const QDateTime& now)
{
    const auto blocks = ScheduleBlockRepository::allBlocks();
    const qint64 activeId = BlockTimeResolver::resolveActiveBlockId(blocks, now);

    if (m_lastKnownActiveBlockId == kUninitialized) {
        m_lastKnownActiveBlockId = activeId;
    } else if (activeId != m_lastKnownActiveBlockId) {
        // DST fall-back guard: BlockTimeResolver resolves purely from local
        // wall-clock time-of-day, so during the repeated local hour the
        // wall clock briefly walks BACKWARD into the previous block before
        // re-crossing forward again — the resolver sees the exact same
        // (from, to) pair AND its reverse (to, from), even though real
        // elapsed time only crossed the boundary once (the UTC offset
        // changed underneath, not the wall-clock readout). Checking both
        // directions against the last FIRED pair catches the backward
        // bounce as well as the forward repeat that follows it.
        // toSecsSinceEpoch() is UTC-based and stays monotonic through that
        // repeated hour, so it's what disambiguates a genuine repeat from
        // the artifact. 2 hours of margin comfortably covers any real-world
        // DST offset (currently always <= 1h) with room to spare.
        const qint64 nowEpochSecs = now.toUTC().toSecsSinceEpoch();
        constexpr qint64 kDstGuardWindowSecs = 2 * 60 * 60;
        const bool sameOrReversedPairAsLastFired = (m_lastFiredFromId == m_lastKnownActiveBlockId && m_lastFiredToId == activeId)
            || (m_lastFiredFromId == activeId && m_lastFiredToId == m_lastKnownActiveBlockId);
        const bool isDstFallbackRepeat = sameOrReversedPairAsLastFired && m_lastFiredTransitionEpochSecs >= 0
            && (nowEpochSecs - m_lastFiredTransitionEpochSecs) <= kDstGuardWindowSecs;
        if (isDstFallbackRepeat) {
            RS_LOG_INFO("scheduler.blocktransition",
                QStringLiteral("Ignoring repeated block transition %1 -> %2 (%3s after it last fired) — DST fall-back artifact")
                    .arg(m_lastKnownActiveBlockId)
                    .arg(activeId)
                    .arg(nowEpochSecs - m_lastFiredTransitionEpochSecs));
            m_lastKnownActiveBlockId = activeId;
        } else {
            m_lastFiredFromId = m_lastKnownActiveBlockId;
            m_lastFiredToId = activeId;
            m_lastFiredTransitionEpochSecs = nowEpochSecs;

            // The boundary just crossed.
            if (!m_preStaged) {
                // Pre-staging never got a chance (block shorter than the
                // lead time, or a schedule edit landed late) — catch up
                // right now rather than losing the cutover entirely.
                RS_LOG_INFO("scheduler.blocktransition",
                    QStringLiteral("Block boundary reached without pre-staging (was %1, now %2) — catching up")
                        .arg(m_lastKnownActiveBlockId)
                        .arg(activeId));
                m_crossfadeController->discardIdleCue();
                m_autoDjEngine->resetQueueForBlock(activeId);
            }
            m_lastKnownActiveBlockId = activeId;
            m_preStaged = false;
            m_fadePending = true;
            m_lastFiredHardCutElementId = -1;
        }
    } else if (activeId >= 0) {
        const auto it = std::find_if(
            blocks.begin(), blocks.end(), [activeId](const auto& block) { return block.id == activeId; });
        if (it != blocks.end()) {
            const qint64 remaining = BlockTimeResolver::secondsRemainingInBlock(*it, now);
            if (remaining <= m_leadSeconds) {
                const qint64 nextBlockId
                    = BlockTimeResolver::resolveActiveBlockId(blocks, now.addSecs(remaining + 1));
                if (!m_preStaged || m_preStagedForBlockId != nextBlockId) {
                    RS_LOG_INFO("scheduler.blocktransition",
                        QStringLiteral("Pre-staging block %1 (%2s remaining in current block)")
                            .arg(nextBlockId)
                            .arg(remaining));
                    m_crossfadeController->discardIdleCue();
                    m_autoDjEngine->resetQueueForBlock(nextBlockId);
                    m_preStaged = true;
                    m_preStagedForBlockId = nextBlockId;
                }
            }

            // Clock-driven hard-timed MUSIC element -- see
            // ClockEngine::dueHardCutElementId()'s doc comment for why this
            // only ever fires for music, never carts. Deduped against the
            // element id, not just "is something due", since the wheel
            // won't advance past it merely from a fade request.
            if (m_clockEngine && it->clockId >= 0) {
                const auto dueElementId = m_clockEngine->dueHardCutElementId(*it, now);
                if (dueElementId && *dueElementId != m_lastFiredHardCutElementId) {
                    RS_LOG_INFO("scheduler.blocktransition",
                        QStringLiteral("Hard-timed clock element %1 due — forcing an early fade").arg(*dueElementId));
                    m_fadePending = true;
                    m_lastFiredHardCutElementId = *dueElementId;
                }
            }
        }
    }

    if (m_fadePending && m_crossfadeController->canRequestManualFade()) {
        RS_LOG_INFO("scheduler.blocktransition", QStringLiteral("Forcing fade at block boundary"));
        m_crossfadeController->requestManualFade();
        m_fadePending = false;
    }
}

} // namespace radio::ui
