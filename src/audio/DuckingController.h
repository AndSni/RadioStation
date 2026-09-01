#pragma once

#include <QObject>

class QTimer;

namespace radio::audio {

class AudioEngine;

// Simple threshold-based "VAD-lite" mic ducking: watches the mic's peak
// level (AudioEngine::micPeakLevel()) on a short poll, and when it crosses
// a threshold, pulls both decks down — via AudioEngine::setDeckDuckGain(),
// a dedicated fourth gain stage in MixEngine (see its own doc comment for
// why: neither the crossfade gain stage, which would fight
// CrossfadeController's A/B balance, nor the trim stage, which the deck
// volume slider/auto-gain-compensation already own and would get clobbered
// by writing here too). Structurally parallel to CrossfadeController: owns
// its own poll timer, driven reactively rather than by anything external.
//
// MixEngine::setDuckGain() is instant-only (no ramp support) — rather than
// extending it to support ramping (a bigger, riskier change touching an
// already-load-bearing code path), this steps the value itself in small
// increments across a configurable attack/release duration via its own
// timer tick. Coarser than a true sample-accurate ramp, but ducking
// transitions are perceptually far less sensitive to this than a
// crossfade — keeps the change entirely additive/isolated to this class.
class DuckingController : public QObject {
    Q_OBJECT

public:
    explicit DuckingController(AudioEngine* engine, QObject* parent = nullptr);

    // Off by default — ducking only makes sense once a mic is actually
    // running; MixerPanelWidget's mic strip is expected to enable this
    // alongside (not instead of) starting the mic input itself. Disabling
    // restores both decks' trim to unity IMMEDIATELY (not via the release
    // ramp) — this is an explicit "turn ducking off", not "the mic went
    // quiet".
    void setEnabled(bool enabled);
    bool isEnabled() const { return m_enabled; }

    void setThresholdLinear(double threshold); // mic peak level (0..1+) that triggers ducking
    void setDuckAmountDb(double amountDb); // how far to pull deck trim down while ducking (positive magnitude, e.g. 12 = -12dB)
    void setReleaseHoldMs(qint64 holdMs); // how long the mic must stay quiet before restoring

    // Pure gate decision, extracted from onPollTick() specifically so it's
    // directly unit-testable without a real running AudioEngine (which the
    // null backend can't drive a synthetic mic signal through anyway —
    // its capture device always delivers silence). wasDucking/
    // quietSinceMs are the state carried in from the previous call
    // (quietSinceMs: -1 while at/above threshold, else the timestamp the
    // level last dropped below it); outQuietSinceMs receives the value to
    // carry into the NEXT call. Crossing at/above the threshold ducks
    // immediately (attack has no hold — matches a limiter's own instant-
    // attack precedent); dropping below only releases once the level has
    // stayed below threshold continuously for releaseHoldMs, not on the
    // first quiet poll (a single quiet sample between words shouldn't
    // un-duck mid-sentence). Pure, no state of its own — real-time-safe
    // (not that anything real-time calls it) and directly unit-testable.
    static bool updateDuckingGate(bool wasDucking, double micLevel, double thresholdLinear, qint64 quietSinceMs,
        qint64 nowMs, qint64 releaseHoldMs, qint64& outQuietSinceMs);

    // Moves current toward target by at most maxStepLinear (which must be
    // positive) — the per-tick stepping behind the coarse attack/release
    // ramp this class drives instead of a sample-accurate one (see this
    // class's own doc comment). Pure, no state — directly unit-testable.
    static double stepDuckGainLinear(double current, double target, double maxStepLinear);

private slots:
    void onPollTick();

private:
    AudioEngine* m_engine;
    QTimer* m_pollTimer;

    bool m_enabled = false;
    double m_thresholdLinear = 0.1; // ~-20dBFS
    double m_duckAmountDb = 12.0;
    qint64 m_releaseHoldMs = 300;

    bool m_ducking = false;
    qint64 m_quietSinceMs = -1; // steady-clock ms since the mic last dropped below threshold; -1 while above it
    double m_currentDuckGainLinear = 1.0; // stepped toward target each tick, applied identically to both decks
};

} // namespace radio::audio
