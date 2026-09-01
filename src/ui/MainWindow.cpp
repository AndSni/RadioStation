#include "MainWindow.h"
#include "AnalyzeLoudnessDialog.h"
#include "AudioDeviceSettings.h"
#include "AudioDeviceSettingsDialog.h"
#include "AutoDjPanelWidget.h"
#include "BlockTransitionController.h"
#include "ConsoleButton.h"
#include "ConsoleTheme.h"
#include "CartGridWidget.h"
#include "ClocksPanelWidget.h"
#include "CrossfadeSettingsDialog.h"
#include "CrossfaderWidget.h"
#include "DebugConsoleDockWidget.h"
#include "DeckWidget.h"
#include "DotMatrixDisplay.h"
#include "ImportDialog.h"
#include "LibraryBrowserWidget.h"
#include "LibrarySettings.h"
#include "LibrarySettingsDialog.h"
#include "MasterProcessingSettingsDialog.h"
#include "MixerPanelWidget.h"
#include "PlaylistEditorWidget.h"
#include "QueueWidget.h"
#include "RadioStatisticsPanel.h"
#include "SmartPlaylistPanelWidget.h"
#include "StationSettings.h"
#include "StationSettingsDialog.h"
#include "StreamingSettingsDialog.h"

#include "audio/AudioEngine.h"
#include "audio/CartAutomationEngine.h"
#include "audio/CrossfadeController.h"
#include "core/Logging.h"
#include "db/ScheduleBlockRepository.h"
#include "db/TrackRepository.h"
#include "scheduler/AutoDjEngine.h"
#include "scheduler/BlockTimeResolver.h"
#include "scheduler/ClockEngine.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDateTime>
#include <QDockWidget>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

#include <gst/gst.h>

#include <algorithm>

namespace radio::ui {

namespace {
const QString kSettingsDebugConsoleVisible = QStringLiteral("ui/debugConsoleVisible");
const QString kSettingsWindowGeometry = QStringLiteral("ui/windowGeometry");
const QString kSettingsWindowState = QStringLiteral("ui/windowState");

// Same shape as RadioStatisticsPanel's own formatRemaining() -- kept
// separate for the same reason (a handful of lines, not worth a shared dep).
QString formatRemaining(qint64 totalSeconds)
{
    const qint64 h = totalSeconds / 3600;
    const qint64 m = (totalSeconds % 3600) / 60;
    const qint64 s = totalSeconds % 60;
    if (h > 0)
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("RadioStation %1").arg(QCoreApplication::applicationVersion()));
    resize(1200, 800);

    // Off by default in Qt. Without it, dock widgets can still be tabbed
    // together (drop one's title bar onto another) or floated fully outside
    // the window, but dragging a tab back OUT of a tab group to drop it as
    // its own split panel within the layout silently does nothing — exactly
    // the "could tab things together, couldn't drag them back out" gap.
    setDockNestingEnabled(true);

    m_audioEngine = new radio::audio::AudioEngine(this);
    // The air-device preference has to be read and applied BEFORE start()
    // (MixEngine::setPreferredPlaybackDeviceId() is a no-op once already
    // running — see its doc comment) — AudioDeviceSettingsDialog is what
    // writes this key, live, via switchPlaybackDevice() instead, once the
    // dialog and engine both already exist.
    {
        QSettings settings;
        const QByteArray airId
            = QByteArray::fromHex(settings.value(radio::ui::audio_device_settings::kAirDeviceId).toByteArray());
        m_audioEngine->setPreferredPlaybackDeviceId(airId);
    }
    m_audioEngine->start();
    {
        QSettings settings;
        const QByteArray monitorId
            = QByteArray::fromHex(settings.value(radio::ui::audio_device_settings::kMonitorDeviceId).toByteArray());
        if (!monitorId.isEmpty())
            m_audioEngine->startMonitorDevice(monitorId);
    }
    m_audioEngine->setDeadAirThresholdSeconds(
        QSettings().value(station_settings::kDeadAirThresholdSeconds, station_settings::kDefaultDeadAirThresholdSeconds).toDouble());
    connect(m_audioEngine, &radio::audio::AudioEngine::deadAirDetected, this, &MainWindow::onDeadAirDetected);
    connect(m_audioEngine, &radio::audio::AudioEngine::deadAirCleared, this, &MainWindow::onDeadAirCleared);


    m_crossfadeController = new radio::audio::CrossfadeController(m_audioEngine, this);
    m_cartAutomationEngine
        = new radio::audio::CartAutomationEngine(m_audioEngine, m_crossfadeController, this);
    connect(m_crossfadeController, &radio::audio::CrossfadeController::manualOverrideChanged, this,
        &MainWindow::onManualOverrideChangedForPanicBanner);

    m_autoDjEngine = new radio::scheduler::AutoDjEngine(this);
    m_clockEngine = std::make_unique<radio::scheduler::ClockEngine>();
    m_autoDjEngine->setClockEngine(m_clockEngine.get());
    m_cartAutomationEngine->setClockEngine(m_clockEngine.get());
    m_autoDjEngine->start();

    // Every panel in this window is a QDockWidget — no fixed central
    // layout — so the whole window is movable/rearrangeable/hideable tiles
    // rather than a mix of fixed-and-dockable areas. See the View menu
    // below. QMainWindow::saveState()/restoreState() (see
    // saveWindowState()/restoreWindowState()) already persists every dock's
    // visibility/geometry/floating state automatically; no per-widget
    // bookkeeping is needed beyond giving each a stable objectName. With no
    // central widget set, the dock areas simply fill the whole window.

    auto* decksContainer = new QWidget(this);
    auto* decksLayout = new QHBoxLayout(decksContainer);
    auto* deckA = new DeckWidget(QStringLiteral("A"), m_audioEngine, decksContainer);
    auto* deckB = new DeckWidget(QStringLiteral("B"), m_audioEngine, decksContainer);
    decksLayout->addWidget(deckA);
    decksLayout->addWidget(deckB);

    for (DeckWidget* deckWidget : { deckA, deckB }) {
        // A human touching a deck still tells the crossfader to back its
        // auto-advance off that deck — but the deck no longer shows or
        // drives any Auto-DJ status itself (that moved to the always-visible
        // Controls toolbar; see docs/console-ui-proposal.md §6).
        connect(deckWidget, &DeckWidget::manualActionTaken, m_crossfadeController,
            &radio::audio::CrossfadeController::notifyManualAction);
        connect(m_crossfadeController, &radio::audio::CrossfadeController::trackLoaded, deckWidget,
            &DeckWidget::onTrackLoaded);
        connect(deckWidget, &DeckWidget::trackDisplayed, this, &MainWindow::onDeckTrackDisplayed);
    }

    connect(m_crossfadeController, &radio::audio::CrossfadeController::handsOffResumed, m_autoDjEngine,
        &radio::scheduler::AutoDjEngine::resetQueue);

    m_blockTransitionController = new BlockTransitionController(m_crossfadeController, m_autoDjEngine, this);
    m_blockTransitionController->setClockEngine(m_clockEngine.get());

    auto* decksDock = new QDockWidget(QStringLiteral("Decks"), this);
    decksDock->setObjectName(QStringLiteral("DecksDockWidget"));
    decksDock->setWidget(decksContainer);
    addDockWidget(Qt::TopDockWidgetArea, decksDock);

    auto* crossfaderDock = new QDockWidget(QStringLiteral("Crossfader"), this);
    crossfaderDock->setObjectName(QStringLiteral("CrossfaderDockWidget"));
    crossfaderDock->setWidget(new CrossfaderWidget(m_crossfadeController, this));
    addDockWidget(Qt::TopDockWidgetArea, crossfaderDock);
    splitDockWidget(decksDock, crossfaderDock, Qt::Vertical); // crossfader below decks, not side-by-side

    // At-a-glance on-air/clock/block-status strip — sits beside Decks rather
    // than stacked with it, since it's meant to stay visible as a thin band
    // rather than compete for the same vertical space.
    m_radioStatisticsPanel = new RadioStatisticsPanel(m_audioEngine, this);
    auto* radioStatisticsDock = new QDockWidget(QStringLiteral("Radio Statistics"), this);
    radioStatisticsDock->setObjectName(QStringLiteral("RadioStatisticsDockWidget"));
    radioStatisticsDock->setWidget(m_radioStatisticsPanel);
    addDockWidget(Qt::TopDockWidgetArea, radioStatisticsDock);
    splitDockWidget(decksDock, radioStatisticsDock, Qt::Horizontal); // beside Decks, not stacked

    // The master Auto DJ on/off switch this panel used to hold moved to the
    // toolbar below (see AutoDjPanelWidget's own doc comment) — this is now
    // just the schedule block list.
    m_autoDjPanel = new AutoDjPanelWidget(this);
    auto* autoDjDock = new QDockWidget(QStringLiteral("Auto DJ"), this);
    autoDjDock->setObjectName(QStringLiteral("AutoDjDockWidget"));
    autoDjDock->setWidget(m_autoDjPanel);
    addDockWidget(Qt::TopDockWidgetArea, autoDjDock);
    splitDockWidget(crossfaderDock, autoDjDock, Qt::Vertical); // below the crossfader

    m_libraryBrowser = new LibraryBrowserWidget(this);
    auto* libraryDock = new QDockWidget(QStringLiteral("Library"), this);
    libraryDock->setObjectName(QStringLiteral("LibraryDockWidget"));
    libraryDock->setWidget(m_libraryBrowser);
    addDockWidget(Qt::LeftDockWidgetArea, libraryDock);

    m_playlistEditor = new PlaylistEditorWidget(this);
    auto* playlistsDock = new QDockWidget(QStringLiteral("Playlists"), this);
    playlistsDock->setObjectName(QStringLiteral("PlaylistEditorDockWidget"));
    playlistsDock->setWidget(m_playlistEditor);
    addDockWidget(Qt::LeftDockWidgetArea, playlistsDock);
    tabifyDockWidget(libraryDock, playlistsDock);

    connect(m_libraryBrowser, &LibraryBrowserWidget::trackAddedToPlaylist, m_playlistEditor, &PlaylistEditorWidget::refresh);

    m_smartPlaylistPanel = new SmartPlaylistPanelWidget(this);
    auto* smartPlaylistDock = new QDockWidget(QStringLiteral("Smart Playlists"), this);
    smartPlaylistDock->setObjectName(QStringLiteral("SmartPlaylistDockWidget"));
    smartPlaylistDock->setWidget(m_smartPlaylistPanel);
    addDockWidget(Qt::LeftDockWidgetArea, smartPlaylistDock);
    tabifyDockWidget(playlistsDock, smartPlaylistDock);

    connect(m_smartPlaylistPanel, &SmartPlaylistPanelWidget::playlistCreated, m_playlistEditor, &PlaylistEditorWidget::refresh);

    m_clocksPanel = new ClocksPanelWidget(this);
    auto* clocksDock = new QDockWidget(QStringLiteral("Clocks"), this);
    clocksDock->setObjectName(QStringLiteral("ClocksDockWidget"));
    clocksDock->setWidget(m_clocksPanel);
    addDockWidget(Qt::LeftDockWidgetArea, clocksDock);
    tabifyDockWidget(smartPlaylistDock, clocksDock);

    // AutoDjPanelWidget's block-row summaries show an assigned clock's name
    // -- keep them current whenever a clock itself is renamed/edited/added/
    // deleted, not just on this panel's own block edits.
    connect(m_clocksPanel, &ClocksPanelWidget::clocksChanged, m_autoDjPanel, &AutoDjPanelWidget::refreshBlockList);

    m_queueWidget = new QueueWidget(m_autoDjEngine, this);
    auto* queueDock = new QDockWidget(QStringLiteral("Queue"), this);
    queueDock->setObjectName(QStringLiteral("QueueDockWidget"));
    queueDock->setWidget(m_queueWidget);
    addDockWidget(Qt::RightDockWidgetArea, queueDock);

    connect(m_libraryBrowser, &LibraryBrowserWidget::trackQueued, m_queueWidget, &QueueWidget::refresh);
    connect(m_crossfadeController, &radio::audio::CrossfadeController::queueConsumed, m_queueWidget,
        &QueueWidget::refresh);

    m_cartGrid = new CartGridWidget(m_audioEngine, this);
    connect(m_cartAutomationEngine, &radio::audio::CartAutomationEngine::cartTriggered, m_cartGrid,
        &CartGridWidget::onExternalCartTriggered);
    auto* cartDock = new QDockWidget(QStringLiteral("Cart Wall"), this);
    cartDock->setObjectName(QStringLiteral("CartWallDockWidget"));
    cartDock->setWidget(m_cartGrid);
    addDockWidget(Qt::BottomDockWidgetArea, cartDock);

    // Volume+EQ+VU channel strips (per deck, plus a master strip) all living
    // together in one box — see MixerPanelWidget's own doc comment for why
    // they're never split apart — hosted in its own dockable panel,
    // movable/rearrangeable/hideable exactly like Library/Queue/Cart
    // Wall/Debug Console, rather than living in the fixed central layout.
    m_mixerPanel = new MixerPanelWidget(m_audioEngine, this);
    auto* mixerDock = new QDockWidget(QStringLiteral("Mixer"), this);
    mixerDock->setObjectName(QStringLiteral("MixerPanelDockWidget"));
    mixerDock->setWidget(m_mixerPanel);
    addDockWidget(Qt::BottomDockWidgetArea, mixerDock);

    m_debugConsole = new DebugConsoleDockWidget(this);
    addDockWidget(Qt::BottomDockWidgetArea, m_debugConsole);
    // All three bottom-area docks tabbed together by default — Cart Wall on
    // top; the user can drag any of them apart from there.
    tabifyDockWidget(cartDock, mixerDock);
    tabifyDockWidget(mixerDock, m_debugConsole);

    QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("&View"));

    QAction* toggleDecks = decksDock->toggleViewAction();
    toggleDecks->setText(QStringLiteral("Show Decks"));
    viewMenu->addAction(toggleDecks);

    QAction* toggleCrossfader = crossfaderDock->toggleViewAction();
    toggleCrossfader->setText(QStringLiteral("Show Crossfader"));
    viewMenu->addAction(toggleCrossfader);

    QAction* toggleAutoDj = autoDjDock->toggleViewAction();
    toggleAutoDj->setText(QStringLiteral("Show Auto DJ"));
    viewMenu->addAction(toggleAutoDj);

    QAction* toggleRadioStatistics = radioStatisticsDock->toggleViewAction();
    toggleRadioStatistics->setText(QStringLiteral("Show Radio Statistics"));
    viewMenu->addAction(toggleRadioStatistics);

    QAction* toggleLibrary = libraryDock->toggleViewAction();
    toggleLibrary->setText(QStringLiteral("Show Library"));
    viewMenu->addAction(toggleLibrary);

    QAction* togglePlaylists = playlistsDock->toggleViewAction();
    togglePlaylists->setText(QStringLiteral("Show Playlists"));
    viewMenu->addAction(togglePlaylists);

    QAction* toggleSmartPlaylists = smartPlaylistDock->toggleViewAction();
    toggleSmartPlaylists->setText(QStringLiteral("Show Smart Playlists"));
    viewMenu->addAction(toggleSmartPlaylists);

    QAction* toggleClocks = clocksDock->toggleViewAction();
    toggleClocks->setText(QStringLiteral("Show Clocks"));
    viewMenu->addAction(toggleClocks);

    QAction* toggleQueue = queueDock->toggleViewAction();
    toggleQueue->setText(QStringLiteral("Show Queue"));
    viewMenu->addAction(toggleQueue);

    QAction* toggleCartWall = cartDock->toggleViewAction();
    toggleCartWall->setText(QStringLiteral("Show Cart Wall"));
    viewMenu->addAction(toggleCartWall);

    QAction* toggleMixerPanel = mixerDock->toggleViewAction();
    toggleMixerPanel->setText(QStringLiteral("Show Mixer"));
    viewMenu->addAction(toggleMixerPanel);

    QAction* toggleDebugConsole = m_debugConsole->toggleViewAction();
    toggleDebugConsole->setText(QStringLiteral("Show Debug Console"));
    toggleDebugConsole->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
    viewMenu->addAction(toggleDebugConsole);

    // Escape hatch for a genuine, still-current Qt bug: dragging a tab back
    // OUT of a tabbed dock group is unreliable in Qt's own dock-widget
    // implementation (confirmed independent of this app's own dockOptions/
    // nesting setup, and independent of X11-vs-Wayland — widely reported,
    // including specifically against this app's Qt 6.11.1). setFloating()
    // pulls a panel out of whatever tab group it's currently in immediately,
    // no drag gesture involved — dragging the resulting floating window and
    // dropping it precisely where wanted is a different, much more reliable
    // Qt gesture than tab-tearing, so this combination gets the same result
    // the broken drag was trying to do.
    QMenu* floatMenu = viewMenu->addMenu(QStringLiteral("Float Panel"));
    for (QDockWidget* dock : { decksDock, crossfaderDock, autoDjDock, radioStatisticsDock, libraryDock, playlistsDock,
             smartPlaylistDock, clocksDock, queueDock, cartDock, mixerDock, static_cast<QDockWidget*>(m_debugConsole) }) {
        QAction* floatAction = floatMenu->addAction(QStringLiteral("Float %1").arg(dock->windowTitle()));
        connect(floatAction, &QAction::triggered, dock, [dock]() { dock->setFloating(true); });
    }

    m_streamingDialog = new StreamingSettingsDialog(m_audioEngine, this);
    connect(m_streamingDialog, &StreamingSettingsDialog::connectionStateChanged, this,
        &MainWindow::onStreamingConnectionStateChanged);
    connect(m_audioEngine, &radio::audio::AudioEngine::pipelineError, this, &MainWindow::onPipelineError);

    QMenu* streamingMenu = menuBar()->addMenu(QStringLiteral("&Streaming"));
    QAction* streamingSettingsAction = streamingMenu->addAction(QStringLiteral("Streaming Settings..."));
    connect(streamingSettingsAction, &QAction::triggered, m_streamingDialog, &QDialog::show);

    // Constructing this immediately applies any previously-saved lead/fade
    // timing to m_crossfadeController (see CrossfadeSettingsDialog::
    // loadSettings()) — settings take effect whether or not the user ever
    // opens this dialog this session.
    m_crossfadeSettingsDialog = new CrossfadeSettingsDialog(m_crossfadeController, this);

    QMenu* audioMenu = menuBar()->addMenu(QStringLiteral("&Audio"));
    QAction* crossfadeSettingsAction = audioMenu->addAction(QStringLiteral("Crossfade Settings..."));
    connect(crossfadeSettingsAction, &QAction::triggered, m_crossfadeSettingsDialog, &QDialog::show);

    m_audioDeviceSettingsDialog = new AudioDeviceSettingsDialog(m_audioEngine, this);
    QAction* audioDeviceSettingsAction = audioMenu->addAction(QStringLiteral("Audio Device Settings..."));
    connect(audioDeviceSettingsAction, &QAction::triggered, m_audioDeviceSettingsDialog, &QDialog::show);

    // Constructing this immediately applies any previously-saved compressor
    // settings to m_audioEngine's MixEngine (see
    // MasterProcessingSettingsDialog::loadSettings()), same "settings take
    // effect whether or not the user ever opens this dialog" precedent as
    // m_crossfadeSettingsDialog above.
    m_masterProcessingSettingsDialog = new MasterProcessingSettingsDialog(m_audioEngine, this);
    QAction* masterProcessingSettingsAction = audioMenu->addAction(QStringLiteral("Master Processing..."));
    connect(masterProcessingSettingsAction, &QAction::triggered, m_masterProcessingSettingsDialog, &QDialog::show);

    m_stationSettingsDialog = new StationSettingsDialog(this);
    // Pushes an appearance change to RadioStatisticsPanel the instant it's
    // made, rather than waiting up to 1s for that panel's own poll tick to
    // notice (see RadioStatisticsPanel::refreshAppearanceIfChanged()).
    connect(m_stationSettingsDialog, &StationSettingsDialog::appearanceSettingsChanged, m_radioStatisticsPanel,
        &RadioStatisticsPanel::refreshAppearanceIfChanged);
    // Live-apply Deck Display tuning (dot sizes + colour-by-state) to every
    // deck.
    connect(m_stationSettingsDialog, &StationSettingsDialog::deckDisplaySettingsChanged, this, [this]() {
        for (auto* deck : findChildren<DeckWidget*>())
            deck->reloadDisplaySettings();
    });

    QMenu* stationMenu = menuBar()->addMenu(QStringLiteral("&Station"));
    QAction* stationSettingsAction = stationMenu->addAction(QStringLiteral("Station Settings..."));
    connect(stationSettingsAction, &QAction::triggered, m_stationSettingsDialog, &QDialog::show);

    m_librarySettingsDialog = new LibrarySettingsDialog(this);

    QMenu* libraryMenu = menuBar()->addMenu(QStringLiteral("&Library"));
    QAction* librarySettingsAction = libraryMenu->addAction(QStringLiteral("Library Settings..."));
    connect(librarySettingsAction, &QAction::triggered, m_librarySettingsDialog, &QDialog::show);
    m_refreshLibraryAction = libraryMenu->addAction(QStringLiteral("Refresh Library"));
    connect(m_refreshLibraryAction, &QAction::triggered, this, &MainWindow::onRefreshLibraryClicked);
    m_analyzeLoudnessAction = libraryMenu->addAction(QStringLiteral("Analyze Loudness..."));
    connect(m_analyzeLoudnessAction, &QAction::triggered, this, &MainWindow::onAnalyzeLoudnessClicked);

    QMenu* helpMenu = menuBar()->addMenu(QStringLiteral("&Help"));
    QAction* aboutAction = helpMenu->addAction(QStringLiteral("About RadioStation"));
    connect(aboutAction, &QAction::triggered, this, [this]() {
        const QString version = QCoreApplication::applicationVersion();
        QMessageBox::about(this, QStringLiteral("About RadioStation"),
            QStringLiteral("<h3>RadioStation %1</h3>"
                           "<p>Radio automation &amp; playout.</p>"
                           "<p>Qt %2 &middot; GStreamer %3</p>"
                           "<p>Licensed under the GNU General Public License v3.0.</p>")
                .arg(version, QStringLiteral(QT_VERSION_STR), QString::fromUtf8(gst_version_string())));
    });
    QAction* aboutQtAction = helpMenu->addAction(QStringLiteral("About Qt"));
    connect(aboutQtAction, &QAction::triggered, qApp, &QApplication::aboutQt);

    m_streamingStatusLabel = new QLabel(QStringLiteral("Streaming: Disconnected"), this);
    statusBar()->addPermanentWidget(m_streamingStatusLabel);

    m_deadAirStatusLabel = new QLabel(this);
    m_deadAirStatusLabel->hide();
    statusBar()->addPermanentWidget(m_deadAirStatusLabel);

    m_panicStatusLabel = new QLabel(this);
    m_panicStatusLabel->hide();
    statusBar()->addPermanentWidget(m_panicStatusLabel);

    // A dedicated, always-visible toolbar for the station's core controls —
    // unlike everything else in this app, these have to stay reachable
    // regardless of which docks are currently shown/hidden. Auto DJ on/off
    // used to live only in the (hideable) Auto DJ dock; moved here so
    // there's exactly one place to toggle it, the same "impossible to
    // miss" reasoning Panic was already built around.
    auto* controlsToolBar = addToolBar(QStringLiteral("Controls"));
    controlsToolBar->setMovable(false);

    // Layout: [Auto DJ toggle] [PANIC] [status text].
    // The two buttons are ConsoleButtons (same cap as the deck CUE/PLAY),
    // lit in their own colour: green = Auto DJ running, red = PANIC armed.
    m_autoDjToggleButton = new ConsoleButton(QString(), this);
    m_autoDjToggleButton->setObjectName(QStringLiteral("autoDjToggleButton"));
    m_autoDjToggleButton->setCheckable(true);
    m_autoDjToggleButton->setAccent(QColor(0x2f, 0xd0, 0x6e)); // green light
    m_autoDjToggleButton->setToolTip(QStringLiteral("Turn Auto DJ on or off for both decks."));
    // Pin the size to fit the longer of the two texts ("Auto DJ: OFF"), so
    // toggling ON/OFF never resizes the button.
    m_autoDjToggleButton->setText(QStringLiteral("Auto DJ: OFF"));
    m_autoDjToggleButton->setFixedSize(m_autoDjToggleButton->sizeHint());
    connect(m_autoDjToggleButton, &QPushButton::clicked, this, &MainWindow::onAutoDjToggleClicked);
    connect(m_crossfadeController, &radio::audio::CrossfadeController::manualOverrideChanged, this,
        &MainWindow::onManualOverrideChangedForAutoDjToggle);
    controlsToolBar->addWidget(m_autoDjToggleButton);

    m_panicButton = new ConsoleButton(QStringLiteral("PANIC"), this);
    m_panicButton->setAccent(QColor(0xff, 0x3b, 0x3b)); // red light
    m_panicButton->setLit(true); // always armed
    m_panicButton->setToolTip(
        QStringLiteral("Immediately stop both decks and play the emergency cart configured in Station Settings > Failover."));
    m_panicButton->setFixedSize(m_panicButton->sizeHint());
    connect(m_panicButton, &QPushButton::clicked, this, &MainWindow::onPanicClicked);
    controlsToolBar->addWidget(m_panicButton);

    controlsToolBar->addSeparator();

    // The one always-visible station status line: Auto-DJ state, active
    // schedule block, Now Playing, streaming state. Rebuilt on a 1s timer
    // and immediately whenever any input changes (see refreshStationStatus).
    m_autoDjStatusLabel = new QLabel(this);
    m_autoDjStatusLabel->setObjectName(QStringLiteral("autoDjStatusLabel"));
    m_autoDjStatusLabel->setStyleSheet(QStringLiteral("color: #cfc6b4; padding: 0 8px;"));
    controlsToolBar->addWidget(m_autoDjStatusLabel);

    m_statusTimer = new QTimer(this);
    m_statusTimer->setInterval(1000);
    connect(m_statusTimer, &QTimer::timeout, this, &MainWindow::refreshStationStatus);
    m_statusTimer->start();

    refreshAutoDjToggleButton(); // reflect the default hands-off state immediately
    refreshStationStatus();

    QSettings settings;
    const bool visible = settings.value(kSettingsDebugConsoleVisible, true).toBool();
    m_debugConsole->setVisible(visible);

    restoreWindowState();

    RS_LOG_INFO("ui", QStringLiteral("MainWindow constructed"));
}

MainWindow::~MainWindow()
{
    RS_LOG_INFO("ui", QStringLiteral("MainWindow destroyed"));
}

void MainWindow::onStreamingConnectionStateChanged(bool connected)
{
    m_streamingStatusText = connected ? QStringLiteral("Streaming: Connecting...") : QStringLiteral("Streaming: Disconnected");
    if (connected) {
        m_streamingStatusLabel->setText(m_streamingStatusText);
        m_streamingStatusLabel->setStyleSheet(QStringLiteral("color: #b45309;"));
    } else {
        m_streamingStatusLabel->setText(m_streamingStatusText);
        m_streamingStatusLabel->setStyleSheet(QString());
    }
    refreshStationStatus();
}

void MainWindow::onPipelineError(const QString& source, const QString& message)
{
    // Exact match, not startsWith("stream-") -- Phase 3 added a second,
    // independent "stream-sink-backup" source (see AudioEngine::
    // drainEngineEvents()) for the redundant backup mount, which has its
    // own status indicator in StreamingSettingsDialog's Backup tab. This
    // top-level badge tracks the primary mount only, so a backup-only
    // error can't silently overwrite what it's showing about the primary.
    if (source != QStringLiteral("stream-sink"))
        return;

    m_streamingStatusText = QStringLiteral("Streaming: Error - %1").arg(message);
    m_streamingStatusLabel->setText(m_streamingStatusText);
    m_streamingStatusLabel->setStyleSheet(QStringLiteral("color: #dc2626; font-weight: bold;"));
    refreshStationStatus();
}

void MainWindow::onDeadAirDetected()
{
    m_deadAirStatusLabel->setText(QStringLiteral("⚠ DEAD AIR"));
    m_deadAirStatusLabel->setStyleSheet(QStringLiteral("color: #dc2626; font-weight: bold;"));
    m_deadAirStatusLabel->show();

    QSettings settings;
    if (!settings.value(station_settings::kAutoFailoverOnDeadAir, station_settings::kDefaultAutoFailoverOnDeadAir).toBool())
        return;

    const QString emergencyPath = settings.value(station_settings::kEmergencyCartPath).toString();
    if (emergencyPath.isEmpty()) {
        RS_LOG_ERROR("ui", QStringLiteral("Dead air detected and auto-failover is enabled, but no emergency cart is configured"));
        return;
    }
    RS_LOG_WARN("ui", QStringLiteral("Dead air detected — auto-triggering emergency cart"));
    m_audioEngine->triggerCart(emergencyPath);
}

void MainWindow::onDeadAirCleared()
{
    m_deadAirStatusLabel->hide();
}

void MainWindow::onPanicClicked()
{
    RS_LOG_WARN("ui", QStringLiteral("PANIC activated by operator"));

    for (const QString& deckId : { QStringLiteral("A"), QStringLiteral("B") }) {
        // Manual override first, so Auto DJ doesn't immediately try to
        // refill the deck this is about to hard-stop.
        m_crossfadeController->notifyManualAction(deckId);
        m_audioEngine->stopDeck(deckId);
    }

    const QString emergencyPath = QSettings().value(station_settings::kEmergencyCartPath).toString();
    if (emergencyPath.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Panic"),
            QStringLiteral("Both decks stopped. No emergency cart is configured (Station Settings > Failover) — the station is now silent."));
    } else {
        m_audioEngine->triggerCart(emergencyPath);
    }

    m_panicStatusLabel->setText(QStringLiteral("⚠ PANIC ACTIVE"));
    m_panicStatusLabel->setStyleSheet(QStringLiteral("color: white; background-color: #dc2626; font-weight: bold; padding: 2px 6px;"));
    m_panicStatusLabel->show();
}

void MainWindow::onManualOverrideChangedForPanicBanner(const QString& /*deckId*/, bool /*manual*/)
{
    if (!m_panicStatusLabel->isVisible())
        return;
    // Clears once the operator has taken back real control of both decks
    // (resumed Auto DJ, or otherwise) — reuses this signal rather than a
    // separate "acknowledge panic" affordance.
    if (!m_crossfadeController->isManualOverride(QStringLiteral("A")) && !m_crossfadeController->isManualOverride(QStringLiteral("B")))
        m_panicStatusLabel->hide();
}

void MainWindow::onAutoDjToggleClicked(bool checked)
{
    for (const QString& deckId : { QStringLiteral("A"), QStringLiteral("B") }) {
        if (checked)
            m_crossfadeController->resumeAutoAdvance(deckId);
        else
            m_crossfadeController->notifyManualAction(deckId);
    }
    refreshAutoDjToggleButton();
    refreshStationStatus();
}

void MainWindow::onManualOverrideChangedForAutoDjToggle(const QString& /*deckId*/, bool /*manual*/)
{
    refreshAutoDjToggleButton();
    refreshStationStatus();
}

void MainWindow::refreshAutoDjToggleButton()
{
    const bool autoRunning = !m_crossfadeController->isManualOverride(QStringLiteral("A"))
        && !m_crossfadeController->isManualOverride(QStringLiteral("B"));
    m_autoDjToggleButton->setChecked(autoRunning);
    m_autoDjToggleButton->setLit(autoRunning); // green light when Auto DJ is running hands-off
    m_autoDjToggleButton->setText(autoRunning ? QStringLiteral("Auto DJ: ON") : QStringLiteral("Auto DJ: OFF"));
}

void MainWindow::onDeckTrackDisplayed(
    const QString& deckId, const QString& artist, const QString& title, qint64 trackId)
{
    const int i = deckId == QStringLiteral("B") ? 1 : 0;
    m_deckArtist[i] = artist;
    m_deckTitle[i] = title;
    m_deckTrackId[i] = trackId;
    refreshStationStatus();
}

void MainWindow::refreshStationStatus()
{
    QStringList parts;

    // 1. Auto DJ
    const bool oA = m_crossfadeController->isManualOverride(QStringLiteral("A"));
    const bool oB = m_crossfadeController->isManualOverride(QStringLiteral("B"));
    if (!oA && !oB) {
        parts << QStringLiteral("AUTO DJ: hands-off");
    } else {
        QStringList decks;
        if (oA)
            decks << QStringLiteral("A");
        if (oB)
            decks << QStringLiteral("B");
        parts << QStringLiteral("AUTO DJ: MANUAL (%1)").arg(decks.join(QStringLiteral(", ")));
    }

    const QDateTime now = QDateTime::currentDateTime();

    // 2. Active schedule block
    const auto blocks = radio::db::ScheduleBlockRepository::allBlocks();
    const qint64 activeId = radio::scheduler::BlockTimeResolver::resolveActiveBlockId(blocks, now);
    if (activeId >= 0) {
        const auto it = std::find_if(
            blocks.begin(), blocks.end(), [activeId](const auto& b) { return b.id == activeId; });
        if (it != blocks.end()) {
            parts << QStringLiteral("Block: %1 (%2 left)")
                         .arg(it->name,
                             formatRemaining(radio::scheduler::BlockTimeResolver::secondsRemainingInBlock(*it, now)));
        }
    }

    // 3. Now Playing (whichever deck is on air)
    const QString active = m_crossfadeController->activeDeck();
    const int ai = active == QStringLiteral("B") ? 1 : 0;
    if (m_deckTrackId[ai] >= 0
        && m_audioEngine->state(active) == radio::audio::DeckState::Playing) {
        const auto t = radio::db::TrackRepository::trackById(m_deckTrackId[ai]);
        QString np = QStringLiteral("Now Playing: %1 - %2")
                         .arg(m_deckArtist[ai].isEmpty() ? QStringLiteral("--") : m_deckArtist[ai],
                             m_deckTitle[ai].isEmpty() ? QStringLiteral("--") : m_deckTitle[ai]);
        if (!t.album.isEmpty() || t.year > 0) {
            QString ay = t.album;
            if (t.year > 0)
                ay += ay.isEmpty() ? QString::number(t.year) : QStringLiteral(", %1").arg(t.year);
            np += QStringLiteral(" (%1)").arg(ay);
        }
        if (!t.genre.isEmpty())
            np += QStringLiteral(" %1").arg(t.genre);
        if (t.bitrate > 0)
            np += QStringLiteral(", %1k").arg(t.bitrate / 1000);
        parts << np;
    }

    // 4. Streaming
    parts << m_streamingStatusText;

    m_autoDjStatusLabel->setText(parts.join(QStringLiteral("      ·      ")));
}

void MainWindow::onRefreshLibraryClicked()
{
    using namespace radio::ui::library_settings;

    const QString rootPath = QSettings().value(kRootPath).toString();
    if (rootPath.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Refresh Library"),
            QStringLiteral("Set the main library folder in Library Settings first."));
        m_librarySettingsDialog->show();
        return;
    }

    auto* importer = ImportDialog::runRescan(this, rootPath);
    if (importer)
        connect(importer, &ImportDialog::importFinished, m_libraryBrowser, &LibraryBrowserWidget::refresh);
    beginLibraryOperation(importer);
}

void MainWindow::onAnalyzeLoudnessClicked()
{
    auto* analyzer = AnalyzeLoudnessDialog::runAnalysis(this);
    if (analyzer)
        connect(analyzer, &AnalyzeLoudnessDialog::analysisFinished, m_libraryBrowser, &LibraryBrowserWidget::refresh);
    beginLibraryOperation(analyzer);
}

void MainWindow::beginLibraryOperation(QObject* dialogObject)
{
    if (!dialogObject)
        return;

    m_refreshLibraryAction->setEnabled(false);
    m_analyzeLoudnessAction->setEnabled(false);

    // The rescan/analysis dialog already calls deleteLater() on itself once
    // it finishes -- destroyed() fires right before that deletion
    // completes, so this needs no new signal on ImportDialog/
    // AnalyzeLoudnessDialog.
    connect(dialogObject, &QObject::destroyed, this, [this]() {
        m_refreshLibraryAction->setEnabled(true);
        m_analyzeLoudnessAction->setEnabled(true);
    });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    saveWindowState();
    QMainWindow::closeEvent(event);
}

void MainWindow::restoreWindowState()
{
    QSettings settings;
    if (settings.contains(kSettingsWindowGeometry))
        restoreGeometry(settings.value(kSettingsWindowGeometry).toByteArray());
    if (settings.contains(kSettingsWindowState))
        restoreState(settings.value(kSettingsWindowState).toByteArray());
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue(kSettingsDebugConsoleVisible, m_debugConsole->isVisible());
    settings.setValue(kSettingsWindowGeometry, saveGeometry());
    settings.setValue(kSettingsWindowState, saveState());
}

} // namespace radio::ui
