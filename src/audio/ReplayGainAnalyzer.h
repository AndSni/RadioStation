#pragma once

#include "db/TrackRecord.h"

#include <QThread>
#include <QVector>

namespace radio::audio {

// Computes real ReplayGain (GStreamer's rganalysis element -- the actual
// ReplayGain algorithm, not hand-rolled DSP) for a batch of tracks lacking
// it, writes the result into each file's own tags (TagLib -- GStreamer's
// own muxers silently drop ReplayGain tags on write, which is why TagLib is
// linked at all), and reports it back per-track so the caller can record it
// via TrackRepository::updateReplayGain() immediately, matching
// MetadataScanner's onTrackDiscovered() pattern.
//
// Modeled directly on MetadataScanner's shape: own worker thread (an
// offline batch analysis pipeline has no business contending with
// GstEngineThread's live playback bus dispatch), same per-item/finished
// signal pattern, same cooperative-cancellation contract (checked between
// files, same as a cancel button wired to requestInterruption()).
//
// WAV files are analyzed and reported like any other format, but the
// computed tag is deliberately NOT written into the file itself -- TagLib's
// WAV support isn't confirmed safe against this version's RIFF handling,
// and corrupting a station's WAV files is a far worse outcome than an
// unwritten tag a future re-analysis can just recompute. AAC/ADTS files get
// the same treatment -- they have no reliably-supported tag container here
// either. Both are accepted, documented gaps, not analysis failures --
// trackAnalyzed() still fires so the DB value is recorded either way.
class ReplayGainAnalyzer : public QThread {
    Q_OBJECT

public:
    explicit ReplayGainAnalyzer(QVector<radio::db::TrackRecord> tracks, QObject* parent = nullptr);

signals:
    void trackAnalyzed(qint64 trackId, double gainDb, double peak);
    void analysisFailed(qint64 trackId, QString reason);
    void analysisFinished(int analyzedCount, int failedCount);

protected:
    void run() override;

private:
    QVector<radio::db::TrackRecord> m_tracks;
};

} // namespace radio::audio
