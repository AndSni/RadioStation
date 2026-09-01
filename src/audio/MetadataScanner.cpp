#include "MetadataScanner.h"

#include "core/Logging.h"

#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>
#include <gst/pbutils/pbutils.h>

namespace radio::audio {

using radio::db::TrackRecord;

namespace {

QString tagString(const GstTagList* tags, const char* tag)
{
    gchar* value = nullptr;
    QString result;
    if (tags && gst_tag_list_get_string(tags, tag, &value) && value) {
        result = QString::fromUtf8(value);
        g_free(value);
    }
    return result;
}

// Tries the full GST_TAG_DATE_TIME first (present on most modern tags),
// falling back to the plain GST_TAG_DATE (year-only frames on older/simpler
// files) -- see MetadataScanner.h doc comment. 0 means "unknown", matching
// TrackRecord::year's convention.
int tagYear(const GstTagList* tags)
{
    if (!tags)
        return 0;

    GstDateTime* dateTime = nullptr;
    if (gst_tag_list_get_date_time(tags, GST_TAG_DATE_TIME, &dateTime) && dateTime) {
        const int year = gst_date_time_get_year(dateTime);
        gst_date_time_unref(dateTime);
        return year;
    }

    GDate* date = nullptr;
    if (gst_tag_list_get_date(tags, GST_TAG_DATE, &date) && date) {
        const int year = g_date_get_year(date);
        g_date_free(date);
        return year;
    }

    return 0;
}

// BPM has a real standard tag (TBPM/Vorbis BPM/MP4 tmpo, all embedded by the
// library cleanup pipeline), so there's a real chance GstDiscoverer surfaces
// it directly without needing the manifest.csv merge (ImportDialog) at all.
// -1 means "not analyzed", matching TrackRecord::bpm's sentinel.
double tagBpm(const GstTagList* tags)
{
    gdouble bpm = 0.0;
    if (tags && gst_tag_list_get_double(tags, GST_TAG_BEATS_PER_MINUTE, &bpm))
        return bpm;
    return -1.0;
}

// Some containers/codecs (e.g. certain FLAC/VBR files) don't report a
// bitrate via gst_discoverer_audio_info_get_bitrate() -- approximate one
// from file size and duration so the UI never silently shows nothing.
int approximateBitrateFromFileSize(qint64 fileSizeBytes, qint64 durationMs)
{
    if (durationMs <= 0)
        return 0;
    return static_cast<int>((fileSizeBytes * 8 * 1000) / durationMs); // bits/sec
}

} // namespace

MetadataScanner::MetadataScanner(QStringList filePaths, QObject* parent)
    : QThread(parent)
    , m_filePaths(std::move(filePaths))
{
    setObjectName(QStringLiteral("MetadataScanner"));
}

void MetadataScanner::run()
{
    GError* initError = nullptr;
    GstDiscoverer* discoverer = gst_discoverer_new(5 * GST_SECOND, &initError);
    if (!discoverer) {
        RS_LOG_ERROR("library.scan",
            QStringLiteral("Failed to create GstDiscoverer: %1")
                .arg(initError ? QString::fromUtf8(initError->message) : QStringLiteral("unknown error")));
        g_clear_error(&initError);
        emit scanFinished(0, m_filePaths.size());
        return;
    }

    int discovered = 0;
    int failed = 0;

    for (const QString& path : std::as_const(m_filePaths)) {
        const QString uri = QUrl::fromLocalFile(path).toString();
        GError* err = nullptr;
        GstDiscovererInfo* info = gst_discoverer_discover_uri(discoverer, uri.toUtf8().constData(), &err);

        const GstDiscovererResult result = info ? gst_discoverer_info_get_result(info) : GST_DISCOVERER_ERROR;
        if (result != GST_DISCOVERER_OK) {
            const QString reason = err ? QString::fromUtf8(err->message) : QStringLiteral("discovery failed");
            RS_LOG_WARN("library.scan", QStringLiteral("Skipping %1: %2").arg(path, reason));
            emit fileFailed(path, reason);
            ++failed;
            g_clear_error(&err);
            if (info)
                gst_discoverer_info_unref(info);
            continue;
        }

        TrackRecord record;
        record.filePath = path;
        record.durationMs = static_cast<qint64>(gst_discoverer_info_get_duration(info)) / GST_MSECOND;

        const GstTagList* tags = gst_discoverer_info_get_tags(info);
        record.title = tagString(tags, GST_TAG_TITLE);
        record.artist = tagString(tags, GST_TAG_ARTIST);
        record.album = tagString(tags, GST_TAG_ALBUM);
        record.genre = tagString(tags, GST_TAG_GENRE);
        record.year = tagYear(tags);

        // BPM comes straight from whatever the file's own tags say --
        // RadioStation doesn't analyze or write it itself (that's
        // tools/library_cleanup's job). ReplayGain is different: it's read
        // the same way here (whatever tag the file already carries, trusted
        // unconditionally), but RadioStation's own ReplayGainAnalyzer (see
        // AnalyzeLoudnessDialog) is a second, legitimate writer of that same
        // tag -- a rescan after running it just re-reads the value it wrote.
        record.bpm = tagBpm(tags);

        gdouble replayGainDb = 0.0;
        record.hasReplayGain = tags && gst_tag_list_get_double(tags, GST_TAG_TRACK_GAIN, &replayGainDb);
        if (record.hasReplayGain) {
            record.replayGainDb = replayGainDb;
            gdouble replayGainPeak = 0.0;
            if (gst_tag_list_get_double(tags, GST_TAG_TRACK_PEAK, &replayGainPeak))
                record.replayGainPeak = replayGainPeak;
        }

        if (record.title.isEmpty())
            record.title = QFileInfo(path).completeBaseName();

        GList* audioStreams = gst_discoverer_info_get_audio_streams(info);
        if (audioStreams) {
            auto* audioInfo = reinterpret_cast<GstDiscovererAudioInfo*>(audioStreams->data);
            record.sampleRate = static_cast<int>(gst_discoverer_audio_info_get_sample_rate(audioInfo));
            record.channels = static_cast<int>(gst_discoverer_audio_info_get_channels(audioInfo));
            record.bitrate = static_cast<int>(gst_discoverer_audio_info_get_bitrate(audioInfo));
            if (record.bitrate <= 0)
                record.bitrate = approximateBitrateFromFileSize(QFileInfo(path).size(), record.durationMs);

            GstCaps* caps = gst_discoverer_stream_info_get_caps(
                reinterpret_cast<GstDiscovererStreamInfo*>(audioInfo));
            if (caps) {
                gchar* description = gst_pb_utils_get_codec_description(caps);
                if (description) {
                    record.codec = QString::fromUtf8(description);
                    g_free(description);
                }
                gst_caps_unref(caps);
            }
            gst_discoverer_stream_info_list_free(audioStreams);
        }

        record.fileMtime = QFileInfo(path).lastModified().toSecsSinceEpoch();

        gst_discoverer_info_unref(info);

        emit trackDiscovered(record);
        ++discovered;
    }

    gst_object_unref(discoverer);
    RS_LOG_INFO("library.scan",
        QStringLiteral("Scan finished: %1 discovered, %2 failed").arg(discovered).arg(failed));
    emit scanFinished(discovered, failed);
}

} // namespace radio::audio
