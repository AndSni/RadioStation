#pragma once

#include <QMainWindow>

#include <memory>

class QLabel;
class QAction;
class QPushButton;
class QTimer;

namespace radio::audio {
class AudioEngine;
class CrossfadeController;
class CartAutomationEngine;
}

namespace radio::scheduler {
class AutoDjEngine;
class ClockEngine;
}

namespace radio::ui {

class DebugConsoleDockWidget;
class LibraryBrowserWidget;
class QueueWidget;
class CartGridWidget;
class MixerPanelWidget;
class AutoDjPanelWidget;
class PlaylistEditorWidget;
class SmartPlaylistPanelWidget;
class ClocksPanelWidget;
class StreamingSettingsDialog;
class CrossfadeSettingsDialog;
class MasterProcessingSettingsDialog;
class AudioDeviceSettingsDialog;
class StationSettingsDialog;
class LibrarySettingsDialog;
class RadioStatisticsPanel;
class BlockTransitionController;
class ConsoleButton;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onStreamingConnectionStateChanged(bool connected);
    void onPipelineError(const QString& source, const QString& message);
    void onRefreshLibraryClicked();
    void onAnalyzeLoudnessClicked();
    void onDeadAirDetected();
    void onDeadAirCleared();
    void onPanicClicked();
    // Watches both decks' manual-override state to clear the panic banner
    // once the operator has resumed Auto DJ (or otherwise taken back
    // control) on both — reuses CrossfadeController's existing signal
    // rather than inventing a separate "resume" affordance for Panic.
    void onManualOverrideChangedForPanicBanner(const QString& deckId, bool manual);
    void onAutoDjToggleClicked(bool checked);
    // Keeps the toolbar toggle's checked/text/style in sync with live
    // manual-override state on either deck — the toggle is the ONE place
    // this control lives now (see AutoDjPanelWidget's doc comment for why
    // it moved here), so it has to reflect state changed from anywhere
    // else (e.g. a deck's own "Resume Auto DJ" action), not just its own
    // last click.
    void onManualOverrideChangedForAutoDjToggle(const QString& deckId, bool manual);

    // Caches the track currently displayed on each deck (emitted by
    // DeckWidget on every load path) so refreshStationStatus() can show
    // "Now Playing" for whichever deck is on air.
    void onDeckTrackDisplayed(const QString& deckId, const QString& artist, const QString& title, qint64 trackId);

    // Rebuilds the always-visible Controls-bar status string: Auto-DJ
    // state, the active schedule block, Now Playing, and streaming state.
    // Driven by a 1s timer plus an immediate call whenever any input
    // changes.
    void refreshStationStatus();

private:
    void restoreWindowState();
    void saveWindowState();

    // Disables the library-refresh and analyze-loudness actions while
    // either is in flight (both read/write the tracks table) and re-enables
    // them once dialogObject is destroyed. A no-op if dialogObject is
    // nullptr (runRescan()/AnalyzeLoudnessDialog::runAnalysis() found
    // nothing to do, so nothing actually started).
    void beginLibraryOperation(QObject* dialogObject);
    void refreshAutoDjToggleButton();

    DebugConsoleDockWidget* m_debugConsole = nullptr;
    radio::audio::AudioEngine* m_audioEngine = nullptr;
    radio::audio::CrossfadeController* m_crossfadeController = nullptr;
    radio::audio::CartAutomationEngine* m_cartAutomationEngine = nullptr;
    radio::scheduler::AutoDjEngine* m_autoDjEngine = nullptr;
    std::unique_ptr<radio::scheduler::ClockEngine> m_clockEngine;
    BlockTransitionController* m_blockTransitionController = nullptr;
    LibraryBrowserWidget* m_libraryBrowser = nullptr;
    PlaylistEditorWidget* m_playlistEditor = nullptr;
    SmartPlaylistPanelWidget* m_smartPlaylistPanel = nullptr;
    ClocksPanelWidget* m_clocksPanel = nullptr;
    QueueWidget* m_queueWidget = nullptr;
    CartGridWidget* m_cartGrid = nullptr;
    MixerPanelWidget* m_mixerPanel = nullptr;
    AutoDjPanelWidget* m_autoDjPanel = nullptr;
    RadioStatisticsPanel* m_radioStatisticsPanel = nullptr;
    StreamingSettingsDialog* m_streamingDialog = nullptr;
    CrossfadeSettingsDialog* m_crossfadeSettingsDialog = nullptr;
    MasterProcessingSettingsDialog* m_masterProcessingSettingsDialog = nullptr;
    AudioDeviceSettingsDialog* m_audioDeviceSettingsDialog = nullptr;
    StationSettingsDialog* m_stationSettingsDialog = nullptr;
    LibrarySettingsDialog* m_librarySettingsDialog = nullptr;
    QLabel* m_streamingStatusLabel = nullptr;
    QLabel* m_deadAirStatusLabel = nullptr;
    QLabel* m_panicStatusLabel = nullptr;
    ConsoleButton* m_autoDjToggleButton = nullptr; // objectName "autoDjToggleButton"; green light = Auto DJ ON
    ConsoleButton* m_panicButton = nullptr; // red light, always armed
    QLabel* m_autoDjStatusLabel = nullptr; // the full Controls-bar status string (Auto-DJ / block / now-playing / streaming)
    QTimer* m_statusTimer = nullptr;
    QString m_streamingStatusText = QStringLiteral("Streaming: Disconnected");
    QString m_deckArtist[2];
    QString m_deckTitle[2];
    qint64 m_deckTrackId[2] = { -1, -1 };

    QAction* m_refreshLibraryAction = nullptr;
    QAction* m_analyzeLoudnessAction = nullptr;
};

} // namespace radio::ui
