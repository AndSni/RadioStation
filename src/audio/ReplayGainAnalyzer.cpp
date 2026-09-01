#include "ReplayGainAnalyzer.h"

#include "core/Logging.h"

#include <QFileInfo>
#include <QUrl>

#include <gst/gst.h>

#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/opusfile.h>
#include <taglib/textidentificationframe.h>
#include <taglib/tstring.h>
#include <taglib/vorbisfile.h>
#include <taglib/xiphcomment.h>

namespace radio::audio {

using radio::db::TrackRecord;

namespace {

// rganalysis's sink caps only accept a fixed, discrete list of sample rates
// (8000-48000, confirmed via gst-inspect-1.0 rganalysis) -- an explicit
// capsfilter forces a rate from that list regardless of the source file's
// own rate, matching GstSourcePipeline's own "avoid on-the-fly
// renegotiation" precedent rather than relying on caps negotiation to
// always land somewhere valid.
constexpr int kAnalysisSampleRate = 44100;
constexpr guint64 kBusPollTimeoutNs = 500 * GST_MSECOND;

const char* const kGainKey = "REPLAYGAIN_TRACK_GAIN";
const char* const kPeakKey = "REPLAYGAIN_TRACK_PEAK";

// Mirrors GstSourcePipeline::onPadAdded exactly (audio-only stream filter,
// link-once guard) -- a free function here rather than a class method since
// this pipeline is built and torn down entirely within one function call,
// not owned by a long-lived object.
void onAnalysisPadAdded(GstElement* /*element*/, GstPad* pad, gpointer userData)
{
    auto* sinkTarget = static_cast<GstElement*>(userData);

    GstCaps* caps = gst_pad_get_current_caps(pad);
    if (!caps)
        caps = gst_pad_query_caps(pad, nullptr);
    const GstStructure* structure = gst_caps_get_structure(caps, 0);
    const gchar* name = gst_structure_get_name(structure);
    const bool isAudio = name && g_str_has_prefix(name, "audio/");
    gst_caps_unref(caps);
    if (!isAudio)
        return;

    GstPad* sinkPad = gst_element_get_static_pad(sinkTarget, "sink");
    if (!gst_pad_is_linked(sinkPad))
        gst_pad_link(pad, sinkPad);
    gst_object_unref(sinkPad);
}

// Builds a headless `uridecodebin ! audioconvert ! audioresample !
// capsfilter ! rganalysis ! fakesink` pipeline, runs it to completion, and
// extracts the REPLAYGAIN_TRACK_GAIN/_PEAK tags rganalysis posts on the bus
// at EOS (fakesink's default event handling turns the TAG event it receives
// into a GST_MESSAGE_TAG bus message -- no special wiring needed). Blocks
// synchronously -- safe here since this always runs on ReplayGainAnalyzer's
// own dedicated worker thread, never GstEngineThread.
bool analyzeOneTrack(const QString& filePath, double& outGainDb, double& outPeak, QString& outError)
{
    GstElement* pipeline = gst_pipeline_new(nullptr);
    GstElement* uridecodebin = gst_element_factory_make("uridecodebin", nullptr);
    GstElement* audioconvert = gst_element_factory_make("audioconvert", nullptr);
    GstElement* audioresample = gst_element_factory_make("audioresample", nullptr);
    GstElement* capsfilter = gst_element_factory_make("capsfilter", nullptr);
    GstElement* rganalysis = gst_element_factory_make("rganalysis", nullptr);
    GstElement* fakesink = gst_element_factory_make("fakesink", nullptr);

    if (!pipeline || !uridecodebin || !audioconvert || !audioresample || !capsfilter || !rganalysis || !fakesink) {
        outError = QStringLiteral("failed to create GStreamer elements");
        for (GstElement* element : { uridecodebin, audioconvert, audioresample, capsfilter, rganalysis, fakesink }) {
            if (element)
                gst_object_unref(element);
        }
        if (pipeline)
            gst_object_unref(pipeline);
        return false;
    }

    GstCaps* caps = gst_caps_new_simple("audio/x-raw", "rate", G_TYPE_INT, kAnalysisSampleRate, nullptr);
    g_object_set(capsfilter, "caps", caps, nullptr);
    gst_caps_unref(caps);

    // forced=TRUE (also the element default): always (re)compute rather
    // than skipping a file whose decoded stream happens to already carry a
    // gain tag from somewhere upstream -- this analyzer is the source of
    // truth here, not a passthrough.
    g_object_set(rganalysis, "forced", TRUE, nullptr);

    const QString uri = QUrl::fromLocalFile(filePath).toString();
    g_object_set(uridecodebin, "uri", uri.toUtf8().constData(), nullptr);

    gst_bin_add_many(
        GST_BIN(pipeline), uridecodebin, audioconvert, audioresample, capsfilter, rganalysis, fakesink, nullptr);
    if (!gst_element_link_many(audioconvert, audioresample, capsfilter, rganalysis, fakesink, nullptr)) {
        outError = QStringLiteral("failed to link analysis pipeline");
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
        return false;
    }

    g_signal_connect(uridecodebin, "pad-added", G_CALLBACK(&onAnalysisPadAdded), audioconvert);

    GstBus* bus = gst_element_get_bus(pipeline);
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    GstTagList* accumulatedTags = nullptr;
    bool gotError = false;
    bool interrupted = false;
    bool running = true;
    while (running) {
        GstMessage* msg = gst_bus_timed_pop_filtered(
            bus, kBusPollTimeoutNs, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR | GST_MESSAGE_TAG));
        if (!msg) {
            // 500ms poll rather than an unbounded wait so a canceled batch
            // (see ReplayGainAnalyzer::run()) doesn't have to wait out a
            // slow or stuck file before noticing.
            if (QThread::currentThread()->isInterruptionRequested()) {
                interrupted = true;
                break;
            }
            continue;
        }

        switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_TAG: {
            GstTagList* tags = nullptr;
            gst_message_parse_tag(msg, &tags);
            if (accumulatedTags) {
                GstTagList* merged = gst_tag_list_merge(accumulatedTags, tags, GST_TAG_MERGE_REPLACE);
                gst_tag_list_unref(accumulatedTags);
                gst_tag_list_unref(tags);
                accumulatedTags = merged;
            } else {
                accumulatedTags = tags;
            }
            break;
        }
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* debug = nullptr;
            gst_message_parse_error(msg, &err, &debug);
            outError = err ? QString::fromUtf8(err->message) : QStringLiteral("unknown GStreamer error");
            g_clear_error(&err);
            g_free(debug);
            gotError = true;
            running = false;
            break;
        }
        case GST_MESSAGE_EOS:
            running = false;
            break;
        default:
            break;
        }
        gst_message_unref(msg);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(bus);
    gst_object_unref(pipeline);

    if (interrupted) {
        if (accumulatedTags)
            gst_tag_list_unref(accumulatedTags);
        outError = QStringLiteral("interrupted");
        return false;
    }
    if (gotError) {
        if (accumulatedTags)
            gst_tag_list_unref(accumulatedTags);
        return false;
    }

    bool ok = false;
    if (accumulatedTags) {
        gdouble gain = 0.0;
        gdouble peak = 0.0;
        if (gst_tag_list_get_double(accumulatedTags, GST_TAG_TRACK_GAIN, &gain)) {
            outGainDb = gain;
            outPeak = gst_tag_list_get_double(accumulatedTags, GST_TAG_TRACK_PEAK, &peak) ? peak : 0.0;
            ok = true;
        }
        gst_tag_list_unref(accumulatedTags);
    }
    if (!ok)
        outError = QStringLiteral("no ReplayGain data computed");
    return ok;
}

TagLib::String toTagLibString(const QString& s)
{
    return { s.toUtf8().constData(), TagLib::String::UTF8 };
}

// Matches the long-established REPLAYGAIN_TRACK_GAIN/_PEAK value
// conventions ("-3.20 dB", "0.987654") used by every other ReplayGain
// implementation (mp3gain, beets, foobar2000, etc) -- not a RadioStation-
// specific format, so any other tool reading these files sees the same
// thing they would from any other tagger.
QString formatGainValue(double gainDb)
{
    return QString::number(gainDb, 'f', 2) + QStringLiteral(" dB");
}

QString formatPeakValue(double peak)
{
    return QString::number(peak, 'f', 6);
}

bool writeId3ReplayGain(const QString& filePath, double gainDb, double peak)
{
    TagLib::MPEG::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return false;
    TagLib::ID3v2::Tag* tag = file.ID3v2Tag(true);

    // Re-running analysis (a rescan, or the user re-triggering "Analyze
    // Loudness") must replace the previous value, not accumulate a second
    // TXXX frame with the same description alongside it.
    if (auto* existingGain = TagLib::ID3v2::UserTextIdentificationFrame::find(tag, kGainKey))
        tag->removeFrame(existingGain);
    if (auto* existingPeak = TagLib::ID3v2::UserTextIdentificationFrame::find(tag, kPeakKey))
        tag->removeFrame(existingPeak);

    tag->addFrame(new TagLib::ID3v2::UserTextIdentificationFrame(
        TagLib::String(kGainKey), TagLib::StringList(toTagLibString(formatGainValue(gainDb))), TagLib::String::Latin1));
    tag->addFrame(new TagLib::ID3v2::UserTextIdentificationFrame(
        TagLib::String(kPeakKey), TagLib::StringList(toTagLibString(formatPeakValue(peak))), TagLib::String::Latin1));

    return file.save();
}

// addField(..., replace=true) already overwrites any existing value for the
// same key, so unlike the ID3 TXXX path above there's no separate removal
// step needed here.
void writeXiphReplayGainFields(TagLib::Ogg::XiphComment* comment, double gainDb, double peak)
{
    comment->addField(TagLib::String(kGainKey), toTagLibString(formatGainValue(gainDb)), true);
    comment->addField(TagLib::String(kPeakKey), toTagLibString(formatPeakValue(peak)), true);
}

bool writeFlacReplayGain(const QString& filePath, double gainDb, double peak)
{
    TagLib::FLAC::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return false;
    TagLib::Ogg::XiphComment* comment = file.xiphComment(true);
    if (!comment)
        return false;
    writeXiphReplayGainFields(comment, gainDb, peak);
    return file.save();
}

bool writeVorbisReplayGain(const QString& filePath, double gainDb, double peak)
{
    TagLib::Ogg::Vorbis::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return false;
    writeXiphReplayGainFields(file.tag(), gainDb, peak);
    return file.save();
}

bool writeOpusReplayGain(const QString& filePath, double gainDb, double peak)
{
    TagLib::Ogg::Opus::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return false;
    writeXiphReplayGainFields(file.tag(), gainDb, peak);
    return file.save();
}

bool writeMp4ReplayGain(const QString& filePath, double gainDb, double peak)
{
    TagLib::MP4::File file(filePath.toUtf8().constData());
    if (!file.isValid())
        return false;
    TagLib::MP4::Tag* tag = file.tag();
    if (!tag)
        return false;

    // "com.apple.iTunes" -- not this pipeline's own vendor namespace (see
    // embed.py's TYPE_FREEFORM/ENERGY_FREEFORM, "com.radiostation.libcleanup").
    // ReplayGain already has a real, widely-recognized MP4 convention that
    // other players/taggers (foobar2000, JRiver, mp4tags) already read;
    // inventing a RadioStation-specific key here would make the tag
    // unreadable everywhere else for no reason.
    tag->setItem(TagLib::String("----:com.apple.iTunes:replaygain_track_gain"),
        TagLib::MP4::Item(TagLib::StringList(toTagLibString(formatGainValue(gainDb)))));
    tag->setItem(TagLib::String("----:com.apple.iTunes:replaygain_track_peak"),
        TagLib::MP4::Item(TagLib::StringList(toTagLibString(formatPeakValue(peak)))));

    return file.save();
}

// Returns true if a tag was actually written to disk. false is not
// necessarily a failure -- see ReplayGainAnalyzer.h's WAV/AAC doc comment;
// the caller still records the computed value in the DB either way.
bool writeReplayGainTag(const QString& filePath, double gainDb, double peak)
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix == QStringLiteral("mp3"))
        return writeId3ReplayGain(filePath, gainDb, peak);
    if (suffix == QStringLiteral("flac"))
        return writeFlacReplayGain(filePath, gainDb, peak);
    if (suffix == QStringLiteral("ogg"))
        return writeVorbisReplayGain(filePath, gainDb, peak);
    if (suffix == QStringLiteral("opus"))
        return writeOpusReplayGain(filePath, gainDb, peak);
    if (suffix == QStringLiteral("m4a"))
        return writeMp4ReplayGain(filePath, gainDb, peak);
    return false; // wav/aac/unrecognized -- DB-only, see header doc comment
}

} // namespace

ReplayGainAnalyzer::ReplayGainAnalyzer(QVector<TrackRecord> tracks, QObject* parent)
    : QThread(parent)
    , m_tracks(std::move(tracks))
{
    setObjectName(QStringLiteral("ReplayGainAnalyzer"));
}

void ReplayGainAnalyzer::run()
{
    int analyzed = 0;
    int failed = 0;

    for (const TrackRecord& track : std::as_const(m_tracks)) {
        if (isInterruptionRequested())
            break;

        double gainDb = 0.0;
        double peak = 0.0;
        QString errorReason;
        if (!analyzeOneTrack(track.filePath, gainDb, peak, errorReason)) {
            RS_LOG_WARN("library.replaygain", QStringLiteral("Skipping %1: %2").arg(track.filePath, errorReason));
            emit analysisFailed(track.id, errorReason);
            ++failed;
            continue;
        }

        // A false return here (WAV/AAC/unrecognized format) is an accepted
        // gap, not an error -- see writeReplayGainTag()'s doc comment. The
        // DB value is recorded via trackAnalyzed() below regardless.
        writeReplayGainTag(track.filePath, gainDb, peak);

        emit trackAnalyzed(track.id, gainDb, peak);
        ++analyzed;
    }

    RS_LOG_INFO("library.replaygain",
        QStringLiteral("ReplayGain analysis finished: %1 analyzed, %2 failed").arg(analyzed).arg(failed));
    emit analysisFinished(analyzed, failed);
}

} // namespace radio::audio
