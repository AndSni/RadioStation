#pragma once

#include <QStyledItemDelegate>

#include <functional>

namespace radio::ui {

// Paints a Qt::DisplayRole cell as an Audio-object pill (via
// AudioPillPainter), for item views: Library's TrackTableModel::TitleColumn,
// QueueWidget's list, and PlaylistEditorWidget's track list. One delegate
// class covers all three — they differ only in where the pill's color comes
// from, injected as a callback rather than hardcoded per source, so this
// stays reusable instead of growing three near-identical subclasses.
class AudioPillDelegate : public QStyledItemDelegate {
    Q_OBJECT

public:
    // colorProvider: returns this row's pill color; unset (default) means
    // every row paints AudioPillPainter::kNeutralColor (Library, Playlist
    // Editor). QueueWidget supplies one that reads a per-item color role.
    explicit AudioPillDelegate(QObject* parent = nullptr, std::function<QColor(const QModelIndex&)> colorProvider = {});

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override;

private:
    std::function<QColor(const QModelIndex&)> m_colorProvider;
};

} // namespace radio::ui
