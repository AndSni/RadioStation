#include "AnalyzeLoudnessDialog.h"

#include "audio/ReplayGainAnalyzer.h"
#include "db/TrackRepository.h"

#include "core/Logging.h"

#include <QMessageBox>
#include <QProgressDialog>

namespace radio::ui {

using radio::audio::ReplayGainAnalyzer;
using radio::db::TrackRecord;
using radio::db::TrackRepository;

AnalyzeLoudnessDialog* AnalyzeLoudnessDialog::runAnalysis(QWidget* parentWidget)
{
    const QVector<TrackRecord> candidates = TrackRepository::tracksNeedingReplayGainAnalysis();
    if (candidates.isEmpty()) {
        QMessageBox::information(parentWidget, QStringLiteral("Analyze Loudness"),
            QStringLiteral("Every track already has ReplayGain data."));
        return nullptr;
    }

    auto* dialog = new AnalyzeLoudnessDialog(parentWidget);

    dialog->m_progress = new QProgressDialog(
        QStringLiteral("Analyzing loudness..."), QStringLiteral("Cancel"), 0, candidates.size(), parentWidget);
    // Deliberately non-modal -- see ImportDialog::start()'s identical
    // comment; MainWindow's beginLibraryOperation() enforces mutual
    // exclusion with the other library-scan actions instead.
    dialog->m_progress->setMinimumDuration(0);
    dialog->m_progress->setValue(0);

    dialog->m_analyzer = new ReplayGainAnalyzer(candidates, dialog);
    connect(dialog->m_analyzer, &ReplayGainAnalyzer::trackAnalyzed, dialog, &AnalyzeLoudnessDialog::onTrackAnalyzed);
    connect(dialog->m_analyzer, &ReplayGainAnalyzer::analysisFailed, dialog, &AnalyzeLoudnessDialog::onTrackFailed);
    connect(
        dialog->m_analyzer, &ReplayGainAnalyzer::analysisFinished, dialog, &AnalyzeLoudnessDialog::onAnalysisFinished);
    connect(dialog->m_progress, &QProgressDialog::canceled, dialog->m_analyzer, &QThread::requestInterruption);

    RS_LOG_INFO(
        "library.replaygain", QStringLiteral("Analyze Loudness started: %1 track(s)").arg(candidates.size()));
    dialog->m_analyzer->start();
    return dialog;
}

AnalyzeLoudnessDialog::AnalyzeLoudnessDialog(QWidget* parentWidget)
    : QObject(parentWidget)
    , m_parentWidget(parentWidget)
{
}

void AnalyzeLoudnessDialog::onTrackAnalyzed(qint64 trackId, double gainDb, double peak)
{
    TrackRepository::updateReplayGain(trackId, gainDb, peak);
    m_progress->setValue(m_progress->value() + 1);
}

void AnalyzeLoudnessDialog::onTrackFailed(qint64 /*trackId*/, QString /*reason*/)
{
    m_progress->setValue(m_progress->value() + 1);
}

void AnalyzeLoudnessDialog::onAnalysisFinished(int analyzedCount, int failedCount)
{
    m_progress->close();
    m_progress->deleteLater();

    const QString message = QStringLiteral("Analyzed %1 track(s).%2")
                                 .arg(analyzedCount)
                                 .arg(failedCount > 0
                                         ? QStringLiteral(" %1 track(s) could not be analyzed.").arg(failedCount)
                                         : QString());

    // Non-modal for the same reason ImportDialog::onScanFinished()'s
    // completion box is -- a blocking QMessageBox here would freeze the
    // whole console if this finishes while the station is live.
    auto* box = new QMessageBox(QMessageBox::Information, QStringLiteral("Analyze Loudness Complete"), message,
        QMessageBox::Ok, m_parentWidget);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    connect(box, &QMessageBox::finished, box, &QObject::deleteLater);
    box->show();

    emit analysisFinished();

    m_analyzer->wait();
    m_analyzer->deleteLater();
    deleteLater();
}

} // namespace radio::ui
