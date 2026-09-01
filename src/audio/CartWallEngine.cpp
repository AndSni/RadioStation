#include "CartWallEngine.h"
#include "CartSlotPlayer.h"
#include "GstEngineThread.h"

#include "core/Logging.h"

#include <QUrl>
#include <QUuid>
#include <algorithm>

namespace radio::audio {

CartWallEngine::CartWallEngine(MixEngine* mixEngine, GstEngineThread* engineThread)
    : m_mixEngine(mixEngine)
    , m_engineThread(engineThread)
{
}

CartWallEngine::~CartWallEngine()
{
    // Destructor runs from AudioEngine::shutdown()'s engine-thread-invoked
    // lambda, so this is safe to do directly.
    m_active.clear();
    std::lock_guard<std::mutex> lock(m_activeTokensMutex);
    m_activeTokens.clear();
}

QString CartWallEngine::trigger(const QString& filePath)
{
    const QString uri = QUrl::fromLocalFile(filePath).toString();
    // Generated here (before dispatching), not inside CartSlotPlayer, so it
    // can be returned to the caller synchronously — the CartSlotPlayer
    // instance itself is only built asynchronously, on the engine thread.
    const QString token = QStringLiteral("cart-") + QUuid::createUuid().toString(QUuid::Id128);

    m_engineThread->invoke([this, uri, token]() {
        auto player = std::make_unique<CartSlotPlayer>(
            m_mixEngine, m_engineThread, uri, token, [this](CartSlotPlayer* finished) {
                auto it = std::find_if(m_active.begin(), m_active.end(),
                    [finished](const std::unique_ptr<CartSlotPlayer>& p) { return p.get() == finished; });
                if (it != m_active.end()) {
                    {
                        std::lock_guard<std::mutex> lock(m_activeTokensMutex);
                        m_activeTokens.remove(finished->token());
                    }
                    m_active.erase(it); // erasing the unique_ptr runs ~CartSlotPlayer (teardown() is idempotent)
                }
            });

        if (player->start()) {
            {
                std::lock_guard<std::mutex> lock(m_activeTokensMutex);
                m_activeTokens.insert(token);
            }
            m_active.push_back(std::move(player));
        } else {
            RS_LOG_ERROR("audio.pipeline", QStringLiteral("Cart trigger failed for %1").arg(uri));
        }
    });

    return token;
}

void CartWallEngine::stopAll()
{
    m_engineThread->invoke([this]() { m_active.clear(); });
    // Cleared here, synchronously, rather than waiting for the invoke()
    // above to actually run on the engine thread -- stopAll()'s whole point
    // is "these are stopped now", so the UI-visible snapshot should reflect
    // that immediately rather than racing the teardown.
    std::lock_guard<std::mutex> lock(m_activeTokensMutex);
    m_activeTokens.clear();
}

bool CartWallEngine::isActive(const QString& token) const
{
    std::lock_guard<std::mutex> lock(m_activeTokensMutex);
    return m_activeTokens.contains(token);
}

size_t CartWallEngine::activeCount() const
{
    std::lock_guard<std::mutex> lock(m_activeTokensMutex);
    return static_cast<size_t>(m_activeTokens.size());
}

} // namespace radio::audio
