#include "ImportDialog.h"

#include "audio/MetadataScanner.h"
#include "db/RotationCategoryRepository.h"
#include "db/TrackRepository.h"

#include "core/Logging.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QProgressDialog>
#include <QTextStream>

namespace radio::ui {

using radio::audio::MetadataScanner;
using radio::db::RotationCategoryRepository;
using radio::db::TrackRecord;
using radio::db::TrackRepository;

namespace {
const QStringList kAudioExtensions = { QStringLiteral("*.mp3"), QStringLiteral("*.wav"), QStringLiteral("*.flac"),
    QStringLiteral("*.ogg"), QStringLiteral("*.m4a"), QStringLiteral("*.aac") };

// Minimal RFC4180-ish CSV line splitter -- genre/album/artist/title/type
// values from the cleanup pipeline can legitimately contain commas or
// quotes (e.g. an artist named "X, The"), which Python's csv.DictWriter
// already quotes correctly; this just has to undo that on the read side.
QStringList parseCsvLine(const QString& line)
{
    QStringList fields;
    QString current;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (inQuotes) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    current += QLatin1Char('"');
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                current += c;
            }
        } else if (c == QLatin1Char('"')) {
            inQuotes = true;
        } else if (c == QLatin1Char(',')) {
            fields.append(current);
            current.clear();
        } else {
            current += c;
        }
    }
    fields.append(current);
    return fields;
}
}

namespace {
QStringList findAudioFiles(const QString& folder)
{
    QStringList files;
    QDirIterator it(folder, kAudioExtensions, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext())
        files.append(it.next());
    return files;
}
}

ImportDialog* ImportDialog::runImport(QWidget* parentWidget)
{
    const QString folder = QFileDialog::getExistingDirectory(parentWidget, QStringLiteral("Import Folder"));
    if (folder.isEmpty())
        return nullptr;

    const QStringList files = findAudioFiles(folder);
    if (files.isEmpty()) {
        QMessageBox::information(
            parentWidget, QStringLiteral("Import"), QStringLiteral("No audio files found in that folder."));
        return nullptr;
    }

    auto* importer = new ImportDialog(parentWidget);
    importer->m_importRoot = folder;
    importer->loadManifest(folder);
    importer->start(files);
    return importer;
}

ImportDialog* ImportDialog::runRescan(QWidget* parentWidget, const QString& rootPath)
{
    const QStringList files = findAudioFiles(rootPath);
    if (files.isEmpty()) {
        QMessageBox::information(parentWidget, QStringLiteral("Refresh Library"),
            QStringLiteral("No audio files found under %1.").arg(rootPath));
        return nullptr;
    }

    auto* importer = new ImportDialog(parentWidget, rootPath);
    importer->m_importRoot = rootPath;
    importer->loadManifest(rootPath);
    importer->start(files);
    return importer;
}

ImportDialog::ImportDialog(QWidget* parentWidget, const QString& missingSyncRoot)
    : QObject(parentWidget)
    , m_parentWidget(parentWidget)
    , m_missingSyncRoot(missingSyncRoot)
{
}

void ImportDialog::start(const QStringList& files)
{
    m_progress = new QProgressDialog(
        QStringLiteral("Scanning audio files..."), QStringLiteral("Cancel"), 0, files.size(), m_parentWidget);
    // Deliberately non-modal -- see AnalyzeLoudnessDialog::runAnalysis()'s
    // identical comment; MainWindow enforces mutual exclusion across all
    // four library-scan actions instead.
    m_progress->setMinimumDuration(0);
    m_progress->setValue(0);

    m_scanner = new MetadataScanner(files, this);
    connect(m_scanner, &MetadataScanner::trackDiscovered, this, &ImportDialog::onTrackDiscovered);
    connect(m_scanner, &MetadataScanner::fileFailed, this, &ImportDialog::onFileFailed);
    connect(m_scanner, &MetadataScanner::scanFinished, this, &ImportDialog::onScanFinished);
    connect(m_progress, &QProgressDialog::canceled, m_scanner, &QThread::requestInterruption);

    RS_LOG_INFO("library.scan", QStringLiteral("Import started: %1 files").arg(files.size()));
    m_scanner->start();
}

void ImportDialog::loadManifest(const QString& rootPath)
{
    QFile file(rootPath + QStringLiteral("/manifest.csv"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return; // no manifest -- empty hash, plain import behaves exactly as before

    QTextStream stream(&file);
    QString headerLine = stream.readLine();
    if (headerLine.startsWith(QLatin1Char('#')))
        headerLine = stream.readLine(); // skip the "# manifest_schema_version=N" comment line

    const QStringList headers = parseCsvLine(headerLine);
    const int relativePathIdx = headers.indexOf(QStringLiteral("relative_path"));
    const int typeIdx = headers.indexOf(QStringLiteral("type"));
    const int yearIdx = headers.indexOf(QStringLiteral("year"));
    const int energyIdx = headers.indexOf(QStringLiteral("energy"));
    const int bpmIdx = headers.indexOf(QStringLiteral("bpm"));
    if (relativePathIdx < 0) {
        RS_LOG_WARN("library.scan", QStringLiteral("manifest.csv at %1 has no relative_path column -- ignoring").arg(rootPath));
        return;
    }

    while (!stream.atEnd()) {
        const QString line = stream.readLine();
        if (line.isEmpty())
            continue;
        const QStringList fields = parseCsvLine(line);
        if (fields.size() <= relativePathIdx)
            continue;

        ManifestEntry entry;
        if (typeIdx >= 0 && typeIdx < fields.size())
            entry.type = fields.at(typeIdx);
        if (yearIdx >= 0 && yearIdx < fields.size() && !fields.at(yearIdx).isEmpty())
            entry.year = fields.at(yearIdx).toInt();
        if (energyIdx >= 0 && energyIdx < fields.size() && !fields.at(energyIdx).isEmpty())
            entry.energy = fields.at(energyIdx).toDouble();
        if (bpmIdx >= 0 && bpmIdx < fields.size() && !fields.at(bpmIdx).isEmpty())
            entry.bpm = fields.at(bpmIdx).toDouble();

        m_manifest.insert(fields.at(relativePathIdx), entry);
    }

    RS_LOG_INFO("library.scan", QStringLiteral("Loaded manifest.csv: %1 entries").arg(m_manifest.size()));
}

qint64 ImportDialog::resolveCategoryId(const QString& typeName)
{
    if (m_categoryIdsByName.isEmpty()) {
        for (const auto& category : RotationCategoryRepository::allCategories())
            m_categoryIdsByName.insert(category.name, category.id);
    }

    const auto it = m_categoryIdsByName.constFind(typeName);
    if (it == m_categoryIdsByName.constEnd()) {
        RS_LOG_WARN("library.scan", QStringLiteral("manifest.csv type \"%1\" doesn't match any rotation category -- leaving uncategorized").arg(typeName));
        return -1;
    }
    return it.value();
}

void ImportDialog::onTrackDiscovered(const TrackRecord& record)
{
    TrackRecord enriched = record;

    if (!m_manifest.isEmpty()) {
        const QString relative = QDir(m_importRoot).relativeFilePath(record.filePath);
        const auto it = m_manifest.constFind(relative);
        if (it != m_manifest.constEnd()) {
            const ManifestEntry& entry = it.value();
            if (enriched.year == 0)
                enriched.year = entry.year;
            if (enriched.energy < 0)
                enriched.energy = entry.energy;
            if (enriched.bpm < 0)
                enriched.bpm = entry.bpm;
            if (!entry.type.isEmpty())
                enriched.categoryId = resolveCategoryId(entry.type);
        }
    }

    const qint64 id = TrackRepository::upsertScannedTrack(enriched);
    if (id >= 0)
        emit trackImported(id);
    if (!m_missingSyncRoot.isEmpty())
        m_discoveredPaths.insert(record.filePath);
    m_progress->setValue(m_progress->value() + 1);
}

void ImportDialog::onFileFailed(const QString& /*filePath*/, const QString& /*reason*/)
{
    m_progress->setValue(m_progress->value() + 1);
}

void ImportDialog::onScanFinished(int discoveredCount, int failedCount)
{
    m_progress->close();
    m_progress->deleteLater();

    QString message = QStringLiteral("Imported %1 track(s).%2")
                           .arg(discoveredCount)
                           .arg(failedCount > 0 ? QStringLiteral(" %1 file(s) could not be read.").arg(failedCount) : QString());

    if (!m_missingSyncRoot.isEmpty()) {
        const int flagged = TrackRepository::syncMissingForRoot(m_missingSyncRoot, m_discoveredPaths);
        if (flagged > 0)
            message += QStringLiteral(" %1 previously-known track(s) are no longer found and were flagged missing.").arg(flagged);
    }

    // A blocking QMessageBox::information() here would freeze the whole
    // console (Play/Stop/Fade Now/cart clicks included) if a rescan
    // finishes while the station is live and unattended -- contradicting
    // the progress dialog above's own "deliberately non-modal" design.
    // Built explicitly with setModal(false) + show() instead, matching the
    // non-modal pattern already used correctly elsewhere in this app
    // (CrossfadeSettingsDialog, StreamingSettingsDialog, etc).
    auto* box = new QMessageBox(QMessageBox::Information,
        m_missingSyncRoot.isEmpty() ? QStringLiteral("Import Complete") : QStringLiteral("Refresh Complete"), message,
        QMessageBox::Ok, m_parentWidget);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->setModal(false);
    // WA_DeleteOnClose alone only covers the window-manager close path (the
    // X button) -- QDialog::done(), which the OK button funnels through,
    // calls hide() rather than close(), so it never sends the QCloseEvent
    // WA_DeleteOnClose reacts to. finished() fires either way.
    connect(box, &QMessageBox::finished, box, &QObject::deleteLater);
    box->show();

    emit importFinished();

    m_scanner->wait();
    m_scanner->deleteLater();
    deleteLater();
}

} // namespace radio::ui
