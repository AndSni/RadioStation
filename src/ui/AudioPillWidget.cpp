#include "AudioPillWidget.h"

#include <QPainter>
#include <QPaintEvent>

namespace radio::ui {

AudioPillWidget::AudioPillWidget(QWidget* parent)
    : QWidget(parent)
{
    setMinimumHeight(28);
}

void AudioPillWidget::setText(const QString& text)
{
    if (m_text == text)
        return;
    m_text = text;
    update();
}

void AudioPillWidget::setColor(const QColor& color)
{
    if (m_color == color)
        return;
    m_color = color;
    update();
}

QSize AudioPillWidget::sizeHint() const
{
    return QSize(200, 28);
}

void AudioPillWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);
    QPainter painter(this);
    AudioPillPainter::paint(painter, rect(), m_text, m_color);
}

} // namespace radio::ui
