#include "StreamingSettingsDialog.h"

#include "audio/AudioEngine.h"
#include "audio/StreamEncoderBin.h"
#include "core/Logging.h"

#include <QComboBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTabWidget>
#include <QVBoxLayout>

namespace radio::ui {

using radio::audio::AudioEngine;
using radio::audio::StreamEncoderBin;

StreamingSettingsDialog::StreamingSettingsDialog(AudioEngine* engine, QWidget* parent)
    : QDialog(parent)
    , m_engine(engine)
{
    setWindowTitle(QStringLiteral("Streaming Settings"));

    auto* tabs = new QTabWidget(this);
    tabs->addTab(buildTab(m_primary, QStringLiteral("streaming/"), QStringLiteral("stream-sink")),
        QStringLiteral("Primary"));
    tabs->addTab(buildTab(m_backup, QStringLiteral("streaming-backup/"), QStringLiteral("stream-sink-backup")),
        QStringLiteral("Backup"));

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    setLayout(layout);

    connect(m_engine, &AudioEngine::pipelineError, this, &StreamingSettingsDialog::onPipelineError);

    loadSettings(m_primary);
    loadSettings(m_backup);
    setStatus(m_primary, QStringLiteral("Disconnected"), QString());
    setStatus(m_backup, QStringLiteral("Disconnected"), QString());
}

QWidget* StreamingSettingsDialog::buildTab(MountTab& tab, const QString& settingsPrefix, const QString& errorSource)
{
    tab.settingsPrefix = settingsPrefix;
    tab.errorSource = errorSource;

    auto* page = new QWidget(this);

    tab.hostEdit = new QLineEdit(page);
    tab.portSpin = new QSpinBox(page);
    tab.portSpin->setRange(1, 65535);
    tab.portSpin->setValue(8000);
    tab.mountEdit = new QLineEdit(page);
    tab.usernameEdit = new QLineEdit(page);
    tab.passwordEdit = new QLineEdit(page);
    tab.passwordEdit->setEchoMode(QLineEdit::Password);
    tab.streamNameEdit = new QLineEdit(page);
    tab.genreEdit = new QLineEdit(page);
    tab.descriptionEdit = new QLineEdit(page);
    tab.formatCombo = new QComboBox(page);
    tab.formatCombo->addItem(QStringLiteral("MP3"), static_cast<int>(StreamEncoderBin::Format::Mp3));
    tab.formatCombo->addItem(QStringLiteral("Ogg Vorbis"), static_cast<int>(StreamEncoderBin::Format::Ogg));
    tab.formatCombo->addItem(QStringLiteral("Opus"), static_cast<int>(StreamEncoderBin::Format::Opus));

    auto* form = new QFormLayout();
    form->addRow(QStringLiteral("Server host"), tab.hostEdit);
    form->addRow(QStringLiteral("Port"), tab.portSpin);
    form->addRow(QStringLiteral("Mount"), tab.mountEdit);
    form->addRow(QStringLiteral("Username"), tab.usernameEdit);
    form->addRow(QStringLiteral("Password"), tab.passwordEdit);
    form->addRow(QStringLiteral("Stream name"), tab.streamNameEdit);
    form->addRow(QStringLiteral("Genre"), tab.genreEdit);
    form->addRow(QStringLiteral("Description"), tab.descriptionEdit);
    form->addRow(QStringLiteral("Format"), tab.formatCombo);

    tab.statusLabel = new QLabel(QStringLiteral("Disconnected"), page);
    tab.connectButton = new QPushButton(QStringLiteral("Connect"), page);

    auto* pageLayout = new QVBoxLayout(page);
    pageLayout->addLayout(form);
    pageLayout->addWidget(tab.statusLabel);
    pageLayout->addWidget(tab.connectButton);
    page->setLayout(pageLayout);

    const bool isPrimary = (errorSource == QStringLiteral("stream-sink"));
    connect(tab.connectButton, &QPushButton::clicked, this,
        [this, &tab, isPrimary]() { onConnectClicked(tab, isPrimary); });

    return page;
}

void StreamingSettingsDialog::loadSettings(MountTab& tab)
{
    QSettings settings;
    tab.hostEdit->setText(settings.value(tab.settingsPrefix + "host", QStringLiteral("localhost")).toString());
    tab.portSpin->setValue(settings.value(tab.settingsPrefix + "port", 8000).toInt());
    tab.mountEdit->setText(settings.value(tab.settingsPrefix + "mount", QStringLiteral("/stream")).toString());
    tab.usernameEdit->setText(settings.value(tab.settingsPrefix + "username", QStringLiteral("source")).toString());
    tab.passwordEdit->setText(settings.value(tab.settingsPrefix + "password").toString());
    tab.streamNameEdit->setText(settings.value(tab.settingsPrefix + "streamName").toString());
    tab.genreEdit->setText(settings.value(tab.settingsPrefix + "genre").toString());
    tab.descriptionEdit->setText(settings.value(tab.settingsPrefix + "description").toString());
    tab.formatCombo->setCurrentIndex(settings.value(tab.settingsPrefix + "format", 0).toInt());
}

void StreamingSettingsDialog::saveSettings(MountTab& tab)
{
    QSettings settings;
    settings.setValue(tab.settingsPrefix + "host", tab.hostEdit->text());
    settings.setValue(tab.settingsPrefix + "port", tab.portSpin->value());
    settings.setValue(tab.settingsPrefix + "mount", tab.mountEdit->text());
    settings.setValue(tab.settingsPrefix + "username", tab.usernameEdit->text());
    settings.setValue(tab.settingsPrefix + "password", tab.passwordEdit->text());
    settings.setValue(tab.settingsPrefix + "streamName", tab.streamNameEdit->text());
    settings.setValue(tab.settingsPrefix + "genre", tab.genreEdit->text());
    settings.setValue(tab.settingsPrefix + "description", tab.descriptionEdit->text());
    settings.setValue(tab.settingsPrefix + "format", tab.formatCombo->currentIndex());
}

void StreamingSettingsDialog::onConnectClicked(MountTab& tab, bool isPrimary)
{
    if (tab.connected) {
        if (isPrimary)
            m_engine->disconnectStreaming();
        else
            m_engine->disconnectBackupStreaming();
        tab.connected = false;
        tab.connectButton->setText(QStringLiteral("Connect"));
        setStatus(tab, QStringLiteral("Disconnected"), QString());
        if (isPrimary)
            emit connectionStateChanged(false);
        return;
    }

    saveSettings(tab);

    StreamEncoderBin::Config config;
    config.host = tab.hostEdit->text();
    config.port = tab.portSpin->value();
    config.mount = tab.mountEdit->text();
    config.username = tab.usernameEdit->text();
    config.password = tab.passwordEdit->text();
    config.streamName = tab.streamNameEdit->text();
    config.genre = tab.genreEdit->text();
    config.description = tab.descriptionEdit->text();
    config.format = static_cast<StreamEncoderBin::Format>(tab.formatCombo->currentData().toInt());

    if (isPrimary)
        m_engine->connectStreaming(config);
    else
        m_engine->connectBackupStreaming(config);
    tab.connected = true;
    tab.connectButton->setText(QStringLiteral("Disconnect"));
    setStatus(tab, QStringLiteral("Connecting..."), QStringLiteral("color: #b45309;"));
    if (isPrimary)
        emit connectionStateChanged(true);

    RS_LOG_INFO("streaming.encoder",
        QStringLiteral("Streaming settings (%1): %2:%3%4")
            .arg(isPrimary ? QStringLiteral("primary") : QStringLiteral("backup"))
            .arg(config.host)
            .arg(config.port)
            .arg(config.mount));
}

void StreamingSettingsDialog::onPipelineError(const QString& source, const QString& message)
{
    MountTab* tab = nullptr;
    bool isPrimary = false;
    if (source == m_primary.errorSource) {
        tab = &m_primary;
        isPrimary = true;
    } else if (source == m_backup.errorSource) {
        tab = &m_backup;
    } else {
        return; // not a streaming-branch error for either mount
    }

    tab->connected = false;
    tab->connectButton->setText(QStringLiteral("Connect"));
    setStatus(*tab, QStringLiteral("Error: %1").arg(message), QStringLiteral("color: #dc2626; font-weight: bold;"));
    if (isPrimary)
        emit connectionStateChanged(false);
}

void StreamingSettingsDialog::setStatus(MountTab& tab, const QString& text, const QString& styleSheet)
{
    tab.statusLabel->setText(QStringLiteral("Status: %1").arg(text));
    tab.statusLabel->setStyleSheet(styleSheet);
}

} // namespace radio::ui
