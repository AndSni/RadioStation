#pragma once

#include "db/TrackRecord.h"

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>

class QWidget;
class QProgressDialog;

namespace radio::audio {
class MetadataScanner;
}

namespace radio::ui {

// One row of the library cleanup pipeline's manifest.csv (see
// tools/library_cleanup/libcleanup/manifest.py) -- year/energy/bpm/type
// only; title/artist/genre already come from the file's own tags via
// MetadataScanner, so the manifest doesn't need to repeat them.
struct ManifestEntry {
    int year = 0;
    double energy = -1.0;
    double bpm = -1.0;
    QString type;
};

// Picks (or is given) a folder, recursively finds audio files, and scans
// them via MetadataScanner, writing results to the library database as they
// arrive. Owns itself (deletes on completion) — fire-and-forget from the
// caller.
class ImportDialog : public QObject {
    Q_OBJECT

public:
    // Prompts for a folder and starts the import, returning the (self-
    // owning) ImportDialog so the caller can connect to importFinished, or
    // nullptr if the user canceled the folder picker or it had no
    // recognized audio files. Never marks anything missing -- an ad-hoc
    // "Import Folder..." pick may well be a subfolder or a one-off drop, not
    // the whole library, so absence from it says nothing about files
    // elsewhere.
    static ImportDialog* runImport(QWidget* parentWidget);

    // Rescans rootPath directly (no folder picker) -- the "Refresh Library"
    // action against the settings-configured main folder (see
    // LibrarySettingsDialog). Unlike runImport(), any previously-known,
    // not-yet-missing track under rootPath that this walk doesn't
    // rediscover gets flagged missing (TrackRepository::syncMissingForRoot),
    // so the library reflects files actually moved/deleted since the last
    // rescan. Returns nullptr if rootPath has no recognized audio files
    // (deliberately does NOT flag anything missing in that case either --
    // an unmounted drive or moved root shouldn't wipe out track state; see
    // onScanFinished).
    static ImportDialog* runRescan(QWidget* parentWidget, const QString& rootPath);

signals:
    void importFinished(); // library data changed — refresh views
    void trackImported(qint64 trackId); // emitted per file as it's discovered/upserted

private:
    explicit ImportDialog(QWidget* parentWidget, const QString& missingSyncRoot = QString());

    void start(const QStringList& files);
    void loadManifest(const QString& rootPath);
    qint64 resolveCategoryId(const QString& typeName);
    void onTrackDiscovered(const radio::db::TrackRecord& record);
    void onFileFailed(const QString& filePath, const QString& reason);
    void onScanFinished(int discoveredCount, int failedCount);

    QWidget* m_parentWidget;
    QProgressDialog* m_progress = nullptr;
    radio::audio::MetadataScanner* m_scanner = nullptr;
    QString m_missingSyncRoot; // empty == plain import, never sync missing flags
    QSet<QString> m_discoveredPaths;
    QString m_importRoot; // for resolving each discovered file's manifest.csv relative_path key
    QHash<QString, ManifestEntry> m_manifest; // empty if no manifest.csv was found -- plain import, unchanged behavior
    QHash<QString, qint64> m_categoryIdsByName; // lazily populated from RotationCategoryRepository::allCategories()
};

} // namespace radio::ui
