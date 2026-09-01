#pragma once

#include <QDialog>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QPushButton;
class QLabel;

namespace radio::audio {
class AudioEngine;
}

namespace radio::ui {

// Non-modal (show(), not exec()) so the user can watch live status while
// adjusting settings. Settings persist via QSettings — server/format
// config, not library data.
//
// Two tabs, "Primary" and "Backup", each a fully independent form wired to
// AudioEngine's parallel connect/disconnect/isConnected trio for that mount
// (see AudioEngine::connectBackupStreaming()'s doc comment for why the
// backup mount is a separate API rather than an indexed parameter). Only
// the Primary tab's connection state is surfaced via connectionStateChanged
// — that signal drives MainWindow's single top-level "Streaming:" badge,
// which was already scoped to the primary mount before the backup mount
// existed; the Backup tab shows its own status label instead.
class StreamingSettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit StreamingSettingsDialog(radio::audio::AudioEngine* engine, QWidget* parent = nullptr);

signals:
    void connectionStateChanged(bool connected);

private:
    // Bundles one mount's form widgets so the (near-identical) Primary/
    // Backup tabs can be built and driven by the same code rather than
    // duplicating every field twice.
    struct MountTab {
        QLineEdit* hostEdit = nullptr;
        QSpinBox* portSpin = nullptr;
        QLineEdit* mountEdit = nullptr;
        QLineEdit* usernameEdit = nullptr;
        QLineEdit* passwordEdit = nullptr;
        QLineEdit* streamNameEdit = nullptr;
        QLineEdit* genreEdit = nullptr;
        QLineEdit* descriptionEdit = nullptr;
        QComboBox* formatCombo = nullptr;
        QPushButton* connectButton = nullptr;
        QLabel* statusLabel = nullptr;
        bool connected = false;
        QString settingsPrefix;
        QString errorSource; // "stream-sink" or "stream-sink-backup"
    };

    QWidget* buildTab(MountTab& tab, const QString& settingsPrefix, const QString& errorSource);
    void loadSettings(MountTab& tab);
    void saveSettings(MountTab& tab);
    void setStatus(MountTab& tab, const QString& text, const QString& styleSheet);
    void onConnectClicked(MountTab& tab, bool isPrimary);
    void onPipelineError(const QString& source, const QString& message);

    radio::audio::AudioEngine* m_engine;

    MountTab m_primary;
    MountTab m_backup;
};

} // namespace radio::ui
