#include "CartAutomationEngine.h"
#include "AudioEngine.h"
#include "CartPicker.h"
#include "CrossfadeController.h"

#include "core/Logging.h"
#include "db/CartRepository.h"
#include "db/ScheduleBlockRepository.h"
#include "scheduler/BlockTimeResolver.h"
#include "scheduler/ClockEngine.h"

#include <QDateTime>
#include <QTimer>
#include <algorithm>
#include <optional>

namespace radio::audio {

using radio::db::CartClipRecord;
using radio::db::CartRepository;
using radio::db::ClockElementRecord;
using radio::db::ScheduleBlockRecord;
using radio::db::ScheduleBlockRepository;
using radio::scheduler::BlockTimeResolver;
using radio::scheduler::ClockEngine;

namespace {

// Shared by shouldSuppressAutoCrossfade() and onAutoCrossfadeSuppressed() —
// both need "the block active right now", resolved the same way.
std::optional<ScheduleBlockRecord> resolveActiveBlock()
{
    const auto blocks = ScheduleBlockRepository::allBlocks(); // ORDER BY position ASC
    const qint64 activeBlockId = BlockTimeResolver::resolveActiveBlockId(blocks, QDateTime::currentDateTime());
    if (activeBlockId < 0)
        return std::nullopt;

    const auto it = std::find_if(
        blocks.begin(), blocks.end(), [activeBlockId](const auto& block) { return block.id == activeBlockId; });
    if (it == blocks.end())
        return std::nullopt;
    return *it;
}

} // namespace

CartAutomationEngine::CartAutomationEngine(AudioEngine* audioEngine, CrossfadeController* crossfadeController, QObject* parent)
    : QObject(parent)
    , m_audioEngine(audioEngine)
    , m_crossfadeController(crossfadeController)
{
    connect(m_crossfadeController, &CrossfadeController::autoCrossfadeFinished, this,
        &CartAutomationEngine::onAutoCrossfadeFinished);
    connect(m_crossfadeController, &CrossfadeController::autoCrossfadeSuppressed, this,
        &CartAutomationEngine::onAutoCrossfadeSuppressed);
    m_crossfadeController->setAutoCrossfadeSuppressor([this]() { return shouldSuppressAutoCrossfade(); });

    // 150ms: comfortably tighter than any cart clip's expected duration,
    // without polling AudioEngine::isCartTokenActive()'s brief cross-thread
    // block often enough to matter. No-ops cheaply whenever nothing is
    // pending, which is most of the time.
    m_resumePollTimer = new QTimer(this);
    m_resumePollTimer->setInterval(150);
    connect(m_resumePollTimer, &QTimer::timeout, this, &CartAutomationEngine::onResumePollTimeout);
    m_resumePollTimer->start();
}

bool CartAutomationEngine::shouldSuppressAutoCrossfade() const
{
    if (m_insertCutawayInFlight)
        return true;

    const auto block = resolveActiveBlock();
    if (!block)
        return false;

    if (block->clockId >= 0) {
        // Clock-driven: entirely governed by whether the wheel has a cart
        // element due right now -- the legacy cartPlaybackType/cartFrequency
        // columns below are ignored for this block (see setClockEngine()'s
        // doc comment).
        return m_clockEngine && m_clockEngine->dueCartElement(*block, QDateTime::currentDateTime()).has_value();
    }

    if (block->cartPlaybackType != QStringLiteral("insert") || block->cartFrequency <= 0)
        return false;

    // Peek, don't commit: the real increment happens in
    // onAutoCrossfadeSuppressed(), once the hand-off actually occurs.
    const int counter = m_blockCartCounters.value(block->id, 0);
    return (counter + 1) >= block->cartFrequency;
}

void CartAutomationEngine::onAutoCrossfadeFinished(const QString&)
{
    const auto block = resolveActiveBlock();
    if (!block)
        return;

    // Clock-driven blocks never use the overlay path -- every clock cart
    // fires through the suppressed-handoff path below instead (see
    // shouldSuppressAutoCrossfade()). Must return here rather than falling
    // into the legacy counter logic: m_blockCartCounters would otherwise
    // keep incrementing against a possibly-nonzero leftover cartFrequency
    // left over from before the block was assigned a clock, and could fire
    // a stray overlay cart.
    if (block->clockId >= 0)
        return;

    if (block->cartFrequency <= 0)
        return; // 0 = cart automation disabled for this block

    int& counter = m_blockCartCounters[block->id]; // default-constructs to 0 on first use
    ++counter;
    if (counter < block->cartFrequency)
        return;

    // "insert" mode's own cart fires from onAutoCrossfadeSuppressed() instead
    // (shouldSuppressAutoCrossfade() intercepts the transition before the
    // ramp even starts once the counter reaches this point, so normal
    // operation reaches THIS branch only on the transition immediately
    // before that hand-off — never later, since onAutoCrossfadeSuppressed()
    // resets the counter itself). Still needs the counter incremented above
    // like every other transition, just not the overlay pick-and-schedule
    // below; that stays insert mode's suppressed-handoff job.
    if (block->cartPlaybackType == QStringLiteral("insert"))
        return;

    counter = 0;

    // Overlay carts play in parallel with a song still going out; a jingle
    // that carries its own musical bed would clash. Clips flagged
    // "in-between only" are eligible for the solo insert/clock paths
    // (onAutoCrossfadeSuppressed) but never here.
    auto clips = CartRepository::allClips();
    clips.erase(std::remove_if(clips.begin(), clips.end(),
                    [](const CartClipRecord& clip) { return clip.inBetweenOnly; }),
        clips.end());
    const auto chosen = CartPicker::pick(block->id, clips, block->cartMode, block->cartColor, m_blockSequenceCursors);
    if (chosen.id < 0) {
        RS_LOG_WARN("audio.cartautomation",
            QStringLiteral("Block '%1' wants a cart but no clip matched (mode '%2')").arg(block->name, block->cartMode));
        return;
    }

    // Captured by value, not re-read from the DB at fire time — avoids a
    // race if the user edits/deletes the clip in the few seconds between
    // scheduling and firing. `this` as the context object means the
    // callback is automatically cancelled if this engine (or its parent) is
    // destroyed before the timer fires.
    const QString filePath = chosen.filePath;
    const qint64 clipId = chosen.id;
    const int offsetMs = std::max(0, block->cartOffsetSeconds) * 1000;
    QTimer::singleShot(offsetMs, this, [this, filePath, clipId]() {
        const QString token = m_audioEngine->triggerCart(filePath);
        if (!token.isEmpty())
            emit cartTriggered(clipId, token);
    });

    RS_LOG_INFO("audio.cartautomation",
        QStringLiteral("Scheduled overlay cart '%1' for block '%2' (%3s offset)")
            .arg(chosen.label, block->name)
            .arg(block->cartOffsetSeconds));
}

void CartAutomationEngine::onAutoCrossfadeSuppressed(const QString& fromDeck, const QString& toDeck)
{
    m_insertCutawayInFlight = true;

    const auto block = resolveActiveBlock();
    if (!block) {
        // Whatever made shouldSuppressAutoCrossfade() return true a moment
        // ago is no longer resolvable (e.g. the block was edited/deleted in
        // the interim) — don't leave the station stuck waiting on a cart
        // that's no longer coming.
        RS_LOG_WARN("audio.cartautomation",
            QStringLiteral("Suppressed crossfade %1 -> %2 but no active block resolves now — cutting straight over")
                .arg(fromDeck, toDeck));
        m_insertCutawayInFlight = false;
        m_crossfadeController->completeSuppressedCrossfade();
        return;
    }

    CartClipRecord chosen;
    if (block->clockId >= 0) {
        const auto dueElement = m_clockEngine ? m_clockEngine->dueCartElement(*block, QDateTime::currentDateTime())
                                               : std::nullopt;
        if (!dueElement) {
            // The wheel moved on since shouldSuppressAutoCrossfade() peeked
            // (e.g. the block/clock was edited in the interim) -- nothing to
            // fire, don't leave the station stuck.
            RS_LOG_WARN("audio.cartautomation",
                QStringLiteral("Suppressed crossfade %1 -> %2 for a clock block, but no cart is due now — cutting straight over")
                    .arg(fromDeck, toDeck));
            m_insertCutawayInFlight = false;
            m_crossfadeController->completeSuppressedCrossfade();
            return;
        }

        if (dueElement->elementType == QStringLiteral("cart_specific")) {
            const auto clips = CartRepository::allClips();
            const auto it = std::find_if(clips.begin(), clips.end(),
                [&dueElement](const CartClipRecord& clip) { return clip.id == dueElement->cartClipId; });
            if (it != clips.end())
                chosen = *it;
        } else {
            const auto clips = CartRepository::allClips();
            chosen = CartPicker::pick(dueElement->id, clips, dueElement->cartMode, dueElement->cartColor, m_blockSequenceCursors);
        }

        if (chosen.id < 0) {
            RS_LOG_WARN("audio.cartautomation",
                QStringLiteral("Clock element %1 wants a cart but no clip matched — cutting straight to %2")
                    .arg(dueElement->id)
                    .arg(toDeck));
            m_insertCutawayInFlight = false;
            m_crossfadeController->completeSuppressedCrossfade();
            return;
        }

        const QString token = m_audioEngine->triggerCart(chosen.filePath);
        if (token.isEmpty()) {
            RS_LOG_WARN("audio.cartautomation", QStringLiteral("Failed to trigger clock cart for block '%1'").arg(block->name));
            m_insertCutawayInFlight = false;
            m_crossfadeController->completeSuppressedCrossfade();
            return;
        }

        m_clockEngine->noteCartFired(*block, QDateTime::currentDateTime());
        m_pendingCutawayToken = token;
        emit cartTriggered(chosen.id, token);
        RS_LOG_INFO("audio.cartautomation",
            QStringLiteral("Clock cart '%1' for block '%2': %3 reached its end, cutting to %4 once the cart finishes")
                .arg(chosen.label, block->name, fromDeck, toDeck));
        return;
    }

    // Commit the peek shouldSuppressAutoCrossfade() made.
    m_blockCartCounters[block->id] = 0;

    const auto clips = CartRepository::allClips();
    chosen = CartPicker::pick(block->id, clips, block->cartMode, block->cartColor, m_blockSequenceCursors);
    if (chosen.id < 0) {
        RS_LOG_WARN("audio.cartautomation",
            QStringLiteral("Block '%1' wants an insert cart but no clip matched (mode '%2') — cutting straight to %3")
                .arg(block->name, block->cartMode, toDeck));
        m_insertCutawayInFlight = false;
        m_crossfadeController->completeSuppressedCrossfade();
        return;
    }

    const QString token = m_audioEngine->triggerCart(chosen.filePath);
    if (token.isEmpty()) {
        RS_LOG_WARN("audio.cartautomation", QStringLiteral("Failed to trigger insert cart for block '%1'").arg(block->name));
        m_insertCutawayInFlight = false;
        m_crossfadeController->completeSuppressedCrossfade();
        return;
    }

    m_pendingCutawayToken = token;
    emit cartTriggered(chosen.id, token);
    RS_LOG_INFO("audio.cartautomation",
        QStringLiteral("Insert cart '%1' for block '%2': %3 reached its end, cutting to %4 once the cart finishes")
            .arg(chosen.label, block->name, fromDeck, toDeck));
}

void CartAutomationEngine::onResumePollTimeout()
{
    if (!m_insertCutawayInFlight || m_pendingCutawayToken.isEmpty())
        return;
    if (m_audioEngine->isCartTokenActive(m_pendingCutawayToken))
        return;

    m_pendingCutawayToken.clear();
    m_insertCutawayInFlight = false;

    // The deck we're about to bring up may have gone manual in the time
    // since the cart was triggered — don't force-play over a human who's
    // since taken it over. onWatchTick()'s own manual-override guard
    // already keeps auto-advance from doing anything with either deck while
    // that holds, so simply not completing here leaves things in a sane
    // state; it resolves itself once the deck is released back to Auto DJ.
    const QString toDeck = m_crossfadeController->idleDeck();
    if (m_crossfadeController->isManualOverride(toDeck)) {
        RS_LOG_INFO("audio.cartautomation",
            QStringLiteral("Not completing insert cutaway to %1: now under manual control").arg(toDeck));
        return;
    }

    m_crossfadeController->completeSuppressedCrossfade();
}

} // namespace radio::audio
