#pragma once

#include <QThread>
#include <functional>

#include <glib.h>

namespace radio::audio {

// Owns a GLib main loop running on a dedicated thread, isolated from Qt's
// event loop. GStreamer bus watches and all pipeline-graph mutations are
// dispatched here via invoke(), the GLib analogue of Qt::QueuedConnection.
class GstEngineThread : public QThread {
    Q_OBJECT

public:
    explicit GstEngineThread(QObject* parent = nullptr);
    ~GstEngineThread() override;

    // Schedules fn to run on this thread's GMainContext and returns
    // immediately (fire-and-forget). Safe to call from any thread.
    void invoke(std::function<void()> fn);

    GMainContext* context() const { return m_context; }

    void stop();

protected:
    void run() override;

private:
    GMainContext* m_context = nullptr;
    GMainLoop* m_loop = nullptr;
};

} // namespace radio::audio
