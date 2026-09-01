#include "AudioPillDelegate.h"
#include "AudioPillPainter.h"

#include <QPainter>

namespace radio::ui {

AudioPillDelegate::AudioPillDelegate(QObject* parent, std::function<QColor(const QModelIndex&)> colorProvider)
    : QStyledItemDelegate(parent)
    , m_colorProvider(std::move(colorProvider))
{
}

void AudioPillDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    const QColor color = m_colorProvider ? m_colorProvider(index) : AudioPillPainter::kNeutralColor;
    const QString text = index.data(Qt::DisplayRole).toString();

    painter->save();
    if (option.state & QStyle::State_Selected)
        painter->fillRect(option.rect, option.palette.highlight());

    AudioPillPainter::paint(*painter, option.rect, text, color);

    painter->restore();
}

QSize AudioPillDelegate::sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const
{
    QSize hint = QStyledItemDelegate::sizeHint(option, index);
    hint.setHeight(qMax(hint.height(), 28));
    return hint;
}

} // namespace radio::ui
