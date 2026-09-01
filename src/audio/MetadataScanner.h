#pragma once

#include "db/TrackRecord.h"

#include <QStringList>
#include <QThread>

namespace radio::audio {

// Extracts metadata for a batch of local audio files via GstDiscoverer, one
// file at a time, on its own worker thread — kept separate from
// GstEngineThread so a large library scan never contends with live playback
// bus dispatch. Discovery results are handed back to the UI thread via
// normal queued signal emission (this object is constructed on, and keeps
// affinity to, the UI thread; only run() executes on the worker thread).
class MetadataScanner : public QThread {
    Q_OBJECT

public:
    explicit MetadataScanner(QStringList filePaths, QObject* parent = nullptr);

signals:
    void trackDiscovered(radio::db::TrackRecord record);
    void fileFailed(QString filePath, QString reason);
    void scanFinished(int discoveredCount, int failedCount);

protected:
    void run() override;

private:
    QStringList m_filePaths;
};

} // namespace radio::audio
