#pragma once

#include <QSet>
#include <QString>
#include <memory>
#include <mutex>
#include <vector>

namespace radio::audio {

class GstEngineThread;
class CartSlotPlayer;
class MixEngine;

// Owns the set of currently-playing cart-wall clip instances. All state is
// touched only from the engine thread (both trigger() and the EOS-driven
// cleanup path dispatch through GstEngineThread::invoke), so no locking is
// needed despite trigger() being callable from the UI thread.
//
// Deliberately no debounce/cooldown here: immediate repeated retriggers of
// the SAME clip are legitimate cart-machine behavior (a deliberate fast
// double-tap should overlap, not be swallowed — see
// CartWallEngineTest::retriggerOverlapsRatherThanRestarting). The actual
// "10 songs at once" bug was a held hotkey exploiting Qt's default
// QShortcut auto-repeat, not legitimate retriggers — fixed at the source in
// CartButton::setClip() via setAutoRepeat(false), not here.
class CartWallEngine {
public:
    CartWallEngine(MixEngine* mixEngine, GstEngineThread* engineThread);
    ~CartWallEngine();

    // Fires a new overlapping instance of the clip at filePath. Returns a
    // token identifying this specific instance — see isActive() to find out
    // later whether it's still playing. Safe to call from the UI thread.
    //
    // Deliberately NOT a completion-callback API (e.g. "call me back when
    // this token finishes"): an earlier version of this method took an
    // std::function invoked from inside the EOS-driven teardown/erase path
    // above, and callers that used it to reach back into Qt's cross-thread
    // signal machinery (QMetaObject::invokeMethod(..., Qt::QueuedConnection)
    // targeting a QObject on another thread) reliably corrupted the heap —
    // reproduced deterministically via CartWallEngineTest::
    // retriggerOverlapsRatherThanRestarting, root cause not fully pinned
    // down (something about GstEngineThread being a QThread that runs a
    // GLib main loop via an overridden run() rather than QThread::exec()).
    // isActive() below, polled from the caller's own thread the same way
    // activeCount() already safely is, sidesteps the whole hazard.
    QString trigger(const QString& filePath);

    // Stops and removes all currently-playing cart instances immediately.
    void stopAll();

    // True if the instance trigger() returned this token for is still
    // playing. Safe to call from any thread -- backed by a mutex-guarded
    // snapshot (m_activeTokens) kept in sync with m_active at every
    // mutation (trigger()'s start, the EOS-driven teardown, stopAll()),
    // NOT a blocking round-trip through the engine thread. That round-trip
    // is what this used to be (via AudioEngine::isCartTokenActive()'s old
    // QSemaphore dispatch) -- CartGridWidget and CartAutomationEngine each
    // poll this every 150ms per active cart token, so a blocking cross-
    // thread call here meant the UI thread could stall behind whatever else
    // was queued on the engine thread's GMainContext (a deck load, another
    // cart pipeline build, the stream encoder's bus dispatch).
    bool isActive(const QString& token) const;

    // Safe to call from any thread, same as isActive() above.
    size_t activeCount() const;

private:
    MixEngine* m_mixEngine;
    GstEngineThread* m_engineThread;
    std::vector<std::unique_ptr<CartSlotPlayer>> m_active; // engine-thread-only

    mutable std::mutex m_activeTokensMutex;
    QSet<QString> m_activeTokens; // mirrors m_active's token set -- see isActive()'s doc comment
};

} // namespace radio::audio
