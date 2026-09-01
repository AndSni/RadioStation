#pragma once

#include "AudioObjectMime.h"

#include <QWidget>

namespace radio::scheduler {
class AutoDjEngine;
}

namespace radio::ui {

class QueueListWidget;

class QueueWidget : public QWidget {
    Q_OBJECT

public:
    explicit QueueWidget(radio::scheduler::AutoDjEngine* autoDj, QWidget* parent = nullptr);

public slots:
    void refresh();

private slots:
    void onRowsMoved();
    void onRemoveClicked();
    // A drag from Library or Playlist Editor landed here — a plain
    // reference add (never destructive, unlike a self-drag reorder, which
    // QueueListWidget handles itself and never reaches this slot).
    void onAudioObjectDropped(const AudioObjectDragPayload& payload);

private:
    QueueListWidget* m_list;
};

} // namespace radio::ui
