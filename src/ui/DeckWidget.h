#pragma once

#include "audio/AudioEngine.h"

#include <QWidget>

class QDragEnterEvent;
class QDropEvent;
class QGroupBox;
class QLabel;
class QSlider;

namespace radio::db {
struct TrackRecord;
}

namespace radio::ui {

class ConsoleButton;
class DotMatrixDisplay;
class FadingLabel;
class LcdReadout;
class StarRatingWidget;

class DeckWidget : public QWidget {
    Q_OBJECT

public:
    DeckWidget(QString deckId, radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

    QString deckId() const { return m_deckId; }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

public slots:
    // Reflects a track loaded by a queue-pull path (Auto-DJ, block
    // transitions) so the display doesn't stay stuck on "No track loaded"
    // while the deck is actually playing (ignored if deckId doesn't match).
    // Deliberately NOT wired to a CrossfadeController type directly — this
    // widget only knows about AudioEngine; MainWindow wires the controller's
    // trackLoaded signal to this slot, keeping DeckWidget unit-testable
    // standalone. trackId drives the star rating; playlistItemId is unused
    // (it fed the removed per-queue-entry pill colour) but kept so the slot
    // still matches CrossfadeController::trackLoaded's signature.
    void onTrackLoaded(
        const QString& deckId, const QString& title, const QString& artist, qint64 trackId, qint64 playlistItemId);

    // Re-reads the Deck Display settings (dot sizes + colour-by-state) and
    // re-applies them. MainWindow calls this on every deck when Station
    // Settings > Deck Display changes, so tuning is live.
    void reloadDisplaySettings();

signals:
    // Emitted on every direct user transport action (play/pause/stop/cue/
    // seek), regardless of whether the underlying engine call succeeds —
    // "a human touched this deck" is true the instant they click. MainWindow
    // forwards it to CrossfadeController so auto-advance backs off the deck
    // being handled.
    void manualActionTaken(const QString& deckId);

    // Emitted on every load path (queue-pull, block transition, manual
    // drag) whenever the deck's shown track changes -- MainWindow uses it
    // for the "Now Playing" line on the Controls bar. trackId is -1 for an
    // arbitrary filesystem pick with no DB row.
    void trackDisplayed(const QString& deckId, const QString& artist, const QString& title, qint64 trackId);

private slots:
    void onPlayPauseClicked();
    void onCueClicked();
    void onStopClicked();
    void onSeekSliderReleased();
    void onPositionsChanged();
    void onDeckStateChanged(const QString& deckId, radio::audio::DeckState state);
    void onDeckEos(const QString& deckId);
    void onPipelineError(const QString& source, const QString& message);
    void onStarRatingChanged(int newRating);

private:
    static QString formatMs(qint64 ms);

    // "A track just landed on this deck" display update, shared by
    // onTrackLoaded() (queue-pull — the engine load already happened
    // elsewhere), dropEvent() (a manual drag — this only updates the
    // display, the caller still does its own engine loadTrack()/pause()).
    void applyLoadedTrackDisplay(const QString& title, const QString& artist, qint64 trackId);

    // Fills the dot-matrix tag line (bitrate · BPM · gain) and the two-column
    // metadata block under the Duration readout (Artist / Title / Album,year
    // / Genre on the left; Rating / Play count on the right). known=false (a
    // track with no DB row) shows dashes.
    void applyTrackInfoDisplay(const QString& artist, const QString& title, const radio::db::TrackRecord& track, bool known);

    // CUE blinks whenever a track is loaded but has not been played since it
    // was loaded — the "cued and waiting" signal. Called whenever the
    // loaded/played state changes. Also re-tints the display (yellow = cued,
    // red = playing) when that option is on.
    void refreshCueBlink();

    QString m_deckId;
    radio::audio::AudioEngine* m_engine;
    bool m_displayColourByState = true; // from station_settings::kDeckDisplayColourByState
    qint64 m_currentTrackId = -1; // -1 = unknown (e.g. a manually-loaded file) — star rating disabled
    bool m_playedSinceLoad = false; // false once a track is loaded, true after playback first starts — drives the CUE blink
    bool m_hasTrack = false;

    QGroupBox* m_box;

    DotMatrixDisplay* m_display; // objectName "titleLabel" — stable test handle, kept from the old pill title
    LcdReadout* m_timeReadout; // 7-segment "position / duration", caption "Duration" — same readout style as the Mixer's EQ
    StarRatingWidget* m_starRating;

    // Two-column metadata block under the Duration readout, small label
    // font. The left column is FadingLabels so an over-long artist/title/
    // album/genre fades under the right column instead of stretching the
    // deck.
    FadingLabel* m_metaArtist;
    FadingLabel* m_metaTitle;
    FadingLabel* m_metaAlbum;
    FadingLabel* m_metaGenre;
    QLabel* m_metaPlayCount;

    ConsoleButton* m_playPauseButton;
    ConsoleButton* m_cueButton;
    ConsoleButton* m_stopButton;
    QSlider* m_seekSlider; // a ConsoleFader, held as QSlider* so the header stays light
};

} // namespace radio::ui
