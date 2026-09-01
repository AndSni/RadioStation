#include "GstEngineThread.h"

#include "core/Logging.h"

namespace radio::audio {

namespace {

struct InvokePayload {
    std::function<void()> fn;
};

gboolean invokeTrampoline(gpointer data)
{
    auto* payload = static_cast<InvokePayload*>(data);
    payload->fn();
    return G_SOURCE_REMOVE;
}

void invokeDestroyNotify(gpointer data)
{
    delete static_cast<InvokePayload*>(data);
}

} // namespace

GstEngineThread::GstEngineThread(QObject* parent)
    : QThread(parent)
{
    setObjectName(QStringLiteral("GstEngineThread"));
    // Created here (not in run()) so invoke() can be called safely as soon
    // as this object exists, before start() has actually spun the thread up.
    m_context = g_main_context_new();
}

GstEngineThread::~GstEngineThread()
{
    stop();
    if (m_context)
        g_main_context_unref(m_context);
}

void GstEngineThread::invoke(std::function<void()> fn)
{
    auto* payload = new InvokePayload{std::move(fn)};
    g_main_context_invoke_full(m_context, G_PRIORITY_DEFAULT, invokeTrampoline, payload, invokeDestroyNotify);
}

void GstEngineThread::run()
{
    g_main_context_push_thread_default(m_context);
    m_loop = g_main_loop_new(m_context, FALSE);

    RS_LOG_INFO("audio.pipeline", QStringLiteral("GstEngineThread main loop starting"));
    g_main_loop_run(m_loop);
    RS_LOG_INFO("audio.pipeline", QStringLiteral("GstEngineThread main loop stopped"));

    g_main_loop_unref(m_loop);
    m_loop = nullptr;
    g_main_context_pop_thread_default(m_context);
}

void GstEngineThread::stop()
{
    if (!isRunning())
        return;
    if (m_loop)
        g_main_loop_quit(m_loop);
    wait();
}

} // namespace radio::audio
