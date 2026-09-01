#include "DuckingController.h"
#include "AudioEngine.h"

#include <QTimer>

#include <algorithm>
#include <chrono>
#include <cmath>

namespace radio::audio {

namespace {
constexpr int kPollIntervalMs = 30; // ~20-50ms, per this class's own doc comment
// How long a full duck-down/restore transition takes, regardless of
// direction or duck depth — see onPollTick()'s stepping comment.
constexpr double kTransitionSeconds = 0.20;

const QString kDeckA = QStringLiteral("A");
const QString kDeckB = QStringLiteral("B");

double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

qint64 steadyClockNowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}

DuckingController::DuckingController(AudioEngine* engine, QObject* parent)
    : QObject(parent)
    , m_engine(engine)
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(kPollIntervalMs);
    connect(m_pollTimer, &QTimer::timeout, this, &DuckingController::onPollTick);
    m_pollTimer->start();
}

void DuckingController::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (!enabled) {
        m_currentDuckGainLinear = 1.0;
        m_ducking = false;
        m_quietSinceMs = -1;
        m_engine->setDeckDuckGain(kDeckA, 1.0);
        m_engine->setDeckDuckGain(kDeckB, 1.0);
    }
}

void DuckingController::setThresholdLinear(double threshold)
{
    m_thresholdLinear = threshold;
}

void DuckingController::setDuckAmountDb(double amountDb)
{
    m_duckAmountDb = amountDb;
}

void DuckingController::setReleaseHoldMs(qint64 holdMs)
{
    m_releaseHoldMs = holdMs;
}

bool DuckingController::updateDuckingGate(bool wasDucking, double micLevel, double thresholdLinear,
    qint64 quietSinceMs, qint64 nowMs, qint64 releaseHoldMs, qint64& outQuietSinceMs)
{
    if (micLevel >= thresholdLinear) {
        outQuietSinceMs = -1;
        return true;
    }
    if (!wasDucking) {
        outQuietSinceMs = -1;
        return false;
    }
    if (quietSinceMs < 0) {
        outQuietSinceMs = nowMs; // mic just dropped quiet -- start the hold timer
        return true;
    }
    if (nowMs - quietSinceMs >= releaseHoldMs) {
        outQuietSinceMs = -1;
        return false; // stayed quiet through the whole hold -- release
    }
    outQuietSinceMs = quietSinceMs; // still within the hold -- keep ducking, keep the original quiet-start time
    return true;
}

double DuckingController::stepDuckGainLinear(double current, double target, double maxStepLinear)
{
    if (current < target)
        return std::min(current + maxStepLinear, target);
    if (current > target)
        return std::max(current - maxStepLinear, target);
    return current;
}

void DuckingController::onPollTick()
{
    if (!m_enabled)
        return;

    float left = 0.0f;
    float right = 0.0f;
    m_engine->micPeakLevel(left, right);
    const double micLevel = std::max(left, right);

    const qint64 nowMs = steadyClockNowMs();
    m_ducking = updateDuckingGate(
        m_ducking, micLevel, m_thresholdLinear, m_quietSinceMs, nowMs, m_releaseHoldMs, m_quietSinceMs);

    const double targetGainLinear = m_ducking ? dbToLinear(-m_duckAmountDb) : 1.0;

    // Linear-amplitude stepping, not a dB-domain ramp — simpler, and
    // ducking transitions aren't musically sensitive enough to need the
    // dB-perceptual smoothness a crossfade does. The step size is a fixed
    // fraction of the full 0..1 range per tick, so a shallower duck reaches
    // its target sooner than kTransitionSeconds implies (an accepted
    // approximation, not a bug).
    const double maxStepLinear = (1.0 / kTransitionSeconds) * (static_cast<double>(kPollIntervalMs) / 1000.0);
    const double nextGainLinear = stepDuckGainLinear(m_currentDuckGainLinear, targetGainLinear, maxStepLinear);
    if (nextGainLinear == m_currentDuckGainLinear)
        return; // already at target -- avoid redundant setDeckDuckGain() calls every tick at steady state
    m_currentDuckGainLinear = nextGainLinear;

    m_engine->setDeckDuckGain(kDeckA, m_currentDuckGainLinear);
    m_engine->setDeckDuckGain(kDeckB, m_currentDuckGainLinear);
}

} // namespace radio::audio
