#include "DeckWidget.h"
#include "AudioObjectMime.h"
#include "ConsoleButton.h"
#include "ConsoleFader.h"
#include "ConsoleTheme.h"
#include "DotMatrixDisplay.h"
#include "FadingLabel.h"
#include "LcdReadout.h"
#include "StarRatingWidget.h"
#include "StationSettings.h"
#include "TrackLabel.h"

#include "core/Logging.h"
#include "db/PlaylistRepository.h"
#include "db/TrackRepository.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFont>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMimeData>
#include <QPalette>
#include <QSettings>
#include <QStringList>
#include <QVBoxLayout>
#include <utility>

namespace radio::ui {

using radio::audio::AudioEngine;
using radio::audio::DeckState;

namespace {
// gst_discoverer_audio_info_get_bitrate() (MetadataScanner.cpp) reports bits
// per second, so this is 128 kbps expressed the same way.
constexpr int kBadBitrateThresholdBps = 128000;
}

DeckWidget::DeckWidget(QString deckId, AudioEngine* engine, QWidget* parent)
    : QWidget(parent)
    , m_deckId(std::move(deckId))
    , m_engine(engine)
{
    setAcceptDrops(true);
    m_displayColourByState = QSettings()
                                 .value(station_settings::kDeckDisplayColourByState,
                                     station_settings::kDefaultDeckDisplayColourByState)
                                 .toBool();

    m_box = new QGroupBox(QStringLiteral("Deck %1").arg(m_deckId), this);
    m_box->setStyleSheet(theme::consoleBezelStyle()); // warm metal bezel, same as the Mixer/Crossfader strips
    auto* layout = new QVBoxLayout(m_box);

    m_display = new DotMatrixDisplay(m_box);
    m_display->setObjectName(QStringLiteral("titleLabel")); // stable lookup handle for tests (was the old pill title)
    m_display->setText(QStringLiteral("No track loaded"));
    layout->addWidget(m_display);

    m_seekSlider = new ConsoleFader(Qt::Horizontal, m_box);
    m_seekSlider->setRange(0, 0);
    layout->addWidget(m_seekSlider);

    auto* timeRow = new QHBoxLayout();
    m_timeReadout = new LcdReadout(m_box);
    m_timeReadout->setObjectName(QStringLiteral("timeReadout"));
    m_timeReadout->setCaption(QStringLiteral("Duration"));
    m_timeReadout->setMinimumWidth(170); // fits "12:34 - 45:67" without shrinking the digits away
    m_timeReadout->setValue(QStringLiteral("--:-- - --:--"));
    timeRow->addWidget(m_timeReadout);
    timeRow->addStretch(1);
    layout->addLayout(timeRow);

    // Two-column metadata block under the Duration readout. Small muted
    // label font, column 1 left-aligned, column 2 (rating + play count)
    // pinned to the right edge.
    const QString metaStyle = QStringLiteral("color: #a89f8f; font-size: 11px;");
    const QColor metaColour(0xa8, 0x9f, 0x8f);
    auto* metaRow = new QHBoxLayout();
    metaRow->setContentsMargins(2, 0, 2, 0);
    auto* metaCol1 = new QVBoxLayout();
    metaCol1->setSpacing(1);
    m_metaArtist = new FadingLabel(m_box);
    m_metaTitle = new FadingLabel(m_box);
    m_metaAlbum = new FadingLabel(m_box);
    m_metaGenre = new FadingLabel(m_box);
    for (FadingLabel* l : { m_metaArtist, m_metaTitle, m_metaAlbum, m_metaGenre }) {
        // FadingLabel paints its own text, so set colour/size directly
        // rather than via QSS (which its custom paintEvent wouldn't see).
        QFont mf = l->font();
        mf.setPixelSize(11);
        l->setFont(mf);
        QPalette pal = l->palette();
        pal.setColor(QPalette::WindowText, metaColour);
        l->setPalette(pal);
        metaCol1->addWidget(l);
    }
    auto* metaCol2 = new QVBoxLayout();
    metaCol2->setSpacing(1);
    auto* ratingRow = new QHBoxLayout();
    ratingRow->setSpacing(4);
    auto* ratingCaption = new QLabel(QStringLiteral("Rating:"), m_box);
    ratingCaption->setStyleSheet(metaStyle);
    m_starRating = new StarRatingWidget(m_box);
    m_starRating->setEnabled(false); // nothing loaded yet — no track to rate
    ratingRow->addStretch(1);
    ratingRow->addWidget(ratingCaption, 0, Qt::AlignVCenter);
    ratingRow->addWidget(m_starRating, 0, Qt::AlignVCenter);
    m_metaPlayCount = new QLabel(m_box);
    m_metaPlayCount->setStyleSheet(metaStyle);
    metaCol2->addLayout(ratingRow);
    metaCol2->addWidget(m_metaPlayCount, 0, Qt::AlignRight);
    metaRow->addLayout(metaCol1);
    metaRow->addStretch(1);
    metaRow->addLayout(metaCol2);
    layout->addLayout(metaRow);

    applyTrackInfoDisplay(QString(), QString(), radio::db::TrackRecord(), /*known=*/false); // prime the dashes

    auto* buttonRow = new QHBoxLayout();
    // DJ-console transport: CUE (blinks while a track is cued and waiting),
    // PLAY (its light bar shows the deck's play state — no separate "State:"
    // label), STOP. No Load button — drag-and-drop from Library/Queue/
    // Playlists is the only load path (see dropEvent()).
    m_cueButton = new ConsoleButton(QStringLiteral("CUE"), m_box);
    m_cueButton->setAccent(QColor(0xff, 0xb4, 0x3c)); // amber "cued" light, distinct from PLAY's engaged red
    m_playPauseButton = new ConsoleButton(QStringLiteral("PLAY"), m_box);
    m_stopButton = new ConsoleButton(QStringLiteral("STOP"), m_box);
    for (ConsoleButton* button : { m_cueButton, m_playPauseButton, m_stopButton }) {
        button->setEnabled(false); // nothing loaded yet
        buttonRow->addWidget(button);
    }
    layout->addLayout(buttonRow);

    m_box->setLayout(layout);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(m_box);
    setLayout(outer);

    connect(m_playPauseButton, &QPushButton::clicked, this, &DeckWidget::onPlayPauseClicked);
    connect(m_cueButton, &QPushButton::clicked, this, &DeckWidget::onCueClicked);
    connect(m_stopButton, &QPushButton::clicked, this, &DeckWidget::onStopClicked);
    connect(m_seekSlider, &QSlider::sliderReleased, this, &DeckWidget::onSeekSliderReleased);
    connect(m_starRating, &StarRatingWidget::ratingChanged, this, &DeckWidget::onStarRatingChanged);

    connect(m_engine, &AudioEngine::positionsChanged, this, &DeckWidget::onPositionsChanged);
    connect(m_engine, &AudioEngine::deckStateChanged, this, &DeckWidget::onDeckStateChanged);
    connect(m_engine, &AudioEngine::deckEos, this, &DeckWidget::onDeckEos);
    connect(m_engine, &AudioEngine::pipelineError, this, &DeckWidget::onPipelineError);

    // Reflect whatever the deck is already doing at construction time — the
    // engine only emits deckStateChanged on a CHANGE, so a deck already
    // loaded/playing before this widget existed would otherwise leave the
    // transport buttons stuck disabled.
    onDeckStateChanged(m_deckId, m_engine->state(m_deckId));
}

void DeckWidget::onPlayPauseClicked()
{
    const bool playing = m_engine->state(m_deckId) == DeckState::Playing;
    RS_LOG_INFO("ui", QStringLiteral("Deck %1: user clicked %2").arg(m_deckId, playing ? QStringLiteral("Pause") : QStringLiteral("Play")));
    if (playing)
        m_engine->pause(m_deckId);
    else
        m_engine->play(m_deckId);
    emit manualActionTaken(m_deckId);
}

void DeckWidget::onCueClicked()
{
    RS_LOG_INFO("ui", QStringLiteral("Deck %1: user clicked Cue").arg(m_deckId));
    m_engine->seek(m_deckId, 0);
    m_engine->pause(m_deckId);
    m_playedSinceLoad = false; // re-armed: cued and waiting again
    refreshCueBlink();
    emit manualActionTaken(m_deckId);
}

void DeckWidget::onStopClicked()
{
    RS_LOG_INFO("ui", QStringLiteral("Deck %1: user clicked Stop").arg(m_deckId));
    m_engine->stopDeck(m_deckId);
    emit manualActionTaken(m_deckId);
}

void DeckWidget::onSeekSliderReleased()
{
    RS_LOG_INFO("ui", QStringLiteral("Deck %1: user seeked to %2ms").arg(m_deckId).arg(m_seekSlider->value()));
    m_engine->seek(m_deckId, m_seekSlider->value());
    emit manualActionTaken(m_deckId);
}

void DeckWidget::onTrackLoaded(
    const QString& deckId, const QString& title, const QString& artist, qint64 trackId, qint64 /*playlistItemId*/)
{
    if (deckId != m_deckId)
        return;
    applyLoadedTrackDisplay(title, artist, trackId);
}

void DeckWidget::onStarRatingChanged(int newRating)
{
    if (m_currentTrackId < 0)
        return;
    radio::db::TrackRepository::setRating(m_currentTrackId, newRating);
}

void DeckWidget::onPositionsChanged()
{
    const qint64 pos = m_engine->position(m_deckId);
    const qint64 dur = m_engine->duration(m_deckId);

    if (dur > 0) {
        if (!m_seekSlider->isSliderDown()) {
            m_seekSlider->setRange(0, static_cast<int>(dur));
            if (pos >= 0)
                m_seekSlider->setValue(static_cast<int>(pos));
        }
    }

    // " - " separator, not " / " -- the embedded LCD font has no real
    // slash glyph (it renders garbled, same problem MixerPanelWidget.cpp
    // notes for '%'); the hyphen renders cleanly.
    m_timeReadout->setValue(QStringLiteral("%1 - %2").arg(formatMs(pos), formatMs(dur)));
}

void DeckWidget::onDeckStateChanged(const QString& deckId, DeckState state)
{
    if (deckId != m_deckId)
        return;

    const bool loaded = state != DeckState::Null;
    m_hasTrack = loaded;
    if (state == DeckState::Playing)
        m_playedSinceLoad = true;

    m_playPauseButton->setLit(state == DeckState::Playing);
    for (ConsoleButton* button : { m_cueButton, m_playPauseButton, m_stopButton })
        button->setEnabled(loaded);

    refreshCueBlink();
}

void DeckWidget::onDeckEos(const QString& deckId)
{
    if (deckId != m_deckId)
        return;
    m_playPauseButton->setLit(false);
    refreshCueBlink(); // played-since-load stays true, so this just stops any blink
}

void DeckWidget::onPipelineError(const QString& source, const QString& message)
{
    // source is either a deck id or "stream-sink" (see AudioEngine::
    // handleError) — only surface errors that actually belong to this deck.
    if (source != m_deckId)
        return;

    // Detailed error already logged to the debug console by AudioEngine;
    // show a short summary on the display's tag line + tooltip.
    m_display->setTagLine(QStringLiteral("ERROR: %1").arg(message));
    m_display->setToolTip(message);
    RS_LOG_DEBUG("ui", QStringLiteral("DeckWidget %1 observed error from %2").arg(m_deckId, source));
}

void DeckWidget::refreshCueBlink()
{
    const bool cued = m_hasTrack && !m_playedSinceLoad;
    m_cueButton->setBlinking(cued);

    // Deck display glows yellow while cued, red while playing/played -- an
    // at-a-glance "which deck is on air". Off by setting -> always red.
    const QColor red(0xff, 0x38, 0x24);
    const QColor amber(0xff, 0xb4, 0x3c); // matches the CUE button's accent
    m_display->setLampColor(m_displayColourByState && cued ? amber : red);
}

void DeckWidget::reloadDisplaySettings()
{
    m_displayColourByState = QSettings()
                                 .value(station_settings::kDeckDisplayColourByState,
                                     station_settings::kDefaultDeckDisplayColourByState)
                                 .toBool();
    m_display->reloadMetricsFromSettings();
    refreshCueBlink(); // re-applies the lamp colour
}

void DeckWidget::applyLoadedTrackDisplay(const QString& title, const QString& artist, qint64 trackId)
{
    m_display->setText(formatTrackLabel(artist, title));
    m_display->setToolTip(QString());
    emit trackDisplayed(m_deckId, artist, title, trackId);

    m_currentTrackId = trackId;
    m_hasTrack = true;
    m_playedSinceLoad = false; // freshly loaded — cued and waiting

    // trackId -1 (an arbitrary filesystem pick) has no DB row — one fetch
    // here covers the star rating, the tag line and the metadata block.
    const auto track = trackId >= 0 ? radio::db::TrackRepository::trackById(trackId) : radio::db::TrackRecord();
    m_starRating->setEnabled(trackId >= 0);
    m_starRating->setRating(trackId >= 0 ? track.rating : 0);
    applyTrackInfoDisplay(artist, title, track, trackId >= 0);

    refreshCueBlink();
}

void DeckWidget::applyTrackInfoDisplay(
    const QString& artist, const QString& title, const radio::db::TrackRecord& track, bool known)
{
    const QString dash = QStringLiteral("--");
    const auto orDash = [&dash](const QString& s) { return s.isEmpty() ? dash : s; };

    // --- Dot-matrix tag line: the technical strip only (bitrate / BPM /
    //     gain). ASCII only -- DotMatrixDisplay's character ROM covers
    //     0x20..0x7E, no '·' / em-dash.
    const QString sep = QStringLiteral("  /  ");
    if (!known) {
        m_display->setTagLine(QStringList({ dash, dash, dash }).join(sep));
    } else {
        const QString bitrateText = track.bitrate > 0
            ? QStringLiteral("%1k%2").arg(track.bitrate / 1000)
                  .arg(track.bitrate < kBadBitrateThresholdBps ? QStringLiteral(" LOW") : QString())
            : dash;
        const QString bpmText
            = track.bpm >= 0.0 ? QStringLiteral("%1 BPM").arg(QString::number(track.bpm, 'f', 0)) : dash;
        const QString gainText = track.hasReplayGain
            ? QStringLiteral("%1 dB").arg(QString::number(track.replayGainDb, 'f', 1))
            : dash;
        m_display->setTagLine(QStringList({ bitrateText, bpmText, gainText }).join(sep));
    }

    // --- Metadata block ------------------------------------------------
    const QString artistText = orDash(known && !track.artist.isEmpty() ? track.artist : artist);
    const QString titleText = orDash(known && !track.title.isEmpty() ? track.title : title);
    QString albumText = orDash(known ? track.album : QString());
    if (known && track.year > 0)
        albumText = (track.album.isEmpty() ? dash : track.album) + QStringLiteral(", %1").arg(track.year);
    const QString genreText = orDash(known ? track.genre : QString());

    m_metaArtist->setText(QStringLiteral("Artist  -  %1").arg(artistText));
    m_metaTitle->setText(QStringLiteral("Title  -  %1").arg(titleText));
    m_metaAlbum->setText(QStringLiteral("Album  -  %1").arg(albumText));
    m_metaGenre->setText(QStringLiteral("Genre  -  %1").arg(genreText));
    m_metaPlayCount->setText(QStringLiteral("Play count: %1").arg(known ? QString::number(track.playCount) : dash));
}

void DeckWidget::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasFormat(AudioObjectMime::kMimeType))
        event->acceptProposedAction();
}

void DeckWidget::dropEvent(QDropEvent* event)
{
    const AudioObjectDragPayload payload = AudioObjectMime::decode(event->mimeData());
    if (payload.trackId < 0) {
        event->ignore();
        return;
    }

    // Always re-read current DB state rather than trust anything cached at
    // drag time — the track (or its metadata) may have changed, or been
    // deleted, in the meantime.
    const auto track = radio::db::TrackRepository::trackById(payload.trackId);
    if (track.id < 0) {
        event->ignore();
        return;
    }

    RS_LOG_INFO("ui", QStringLiteral("Deck %1: track dropped (%2)").arg(m_deckId, track.filePath));

    if (payload.fromQueue) {
        // The one destructive pairing — dragging out of the Queue removes it
        // from there, same as Auto-DJ's own queue-pull consumption.
        radio::db::PlaylistRepository::removeItem(payload.playlistItemId);
    }

    applyLoadedTrackDisplay(track.title, track.artist, track.id);
    m_engine->loadTrack(m_deckId, track.filePath);
    m_engine->pause(m_deckId); // preroll
    emit manualActionTaken(m_deckId);

    event->acceptProposedAction();
}

QString DeckWidget::formatMs(qint64 ms)
{
    if (ms < 0)
        return QStringLiteral("--:--");
    const qint64 totalSeconds = ms / 1000;
    return QStringLiteral("%1:%2")
        .arg(totalSeconds / 60)
        .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
}

} // namespace radio::ui
