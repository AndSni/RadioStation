#pragma once

#include <QObject>

class QWidget;
class QProgressDialog;

namespace radio::audio {
class ReplayGainAnalyzer;
}

namespace radio::ui {

// Analyzes every track lacking ReplayGain data (TrackRepository::
// tracksNeedingReplayGainAnalysis()) via ReplayGainAnalyzer and writes each
// result back to the DB as it arrives. Modeled directly on ImportDialog's
// shape: non-modal QProgressDialog wrapping a worker thread, self-owning
// (deletes on completion) -- fire-and-forget from the caller.
class AnalyzeLoudnessDialog : public QObject {
    Q_OBJECT

public:
    // Returns nullptr (no dialog shown) if there's nothing to analyze --
    // every track already has ReplayGain data.
    static AnalyzeLoudnessDialog* runAnalysis(QWidget* parentWidget);

signals:
    void analysisFinished(); // library data changed -- refresh views

private:
    explicit AnalyzeLoudnessDialog(QWidget* parentWidget);

    void onTrackAnalyzed(qint64 trackId, double gainDb, double peak);
    void onTrackFailed(qint64 trackId, QString reason);
    void onAnalysisFinished(int analyzedCount, int failedCount);

    QWidget* m_parentWidget;
    QProgressDialog* m_progress = nullptr;
    radio::audio::ReplayGainAnalyzer* m_analyzer = nullptr;
};

} // namespace radio::ui
