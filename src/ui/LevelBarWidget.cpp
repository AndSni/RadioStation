#include "LevelBarWidget.h"
#include "LedMeterPainter.h"

#include <QPainter>
#include <QSizePolicy>
#include <algorithm>

namespace radio::ui {

LevelBarWidget::LevelBarWidget(double minDb, double maxDb, QWidget* parent)
    : QWidget(parent)
    , m_minDb(minDb)
    , m_maxDb(maxDb)
    , m_currentDb(minDb)
{
    setMinimumSize(kMeterChannelWidth, 80);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
}

void LevelBarWidget::setLevelDb(double db)
{
    if (db == m_currentDb)
        return;
    m_currentDb = db;
    update();
}

QSize LevelBarWidget::sizeHint() const
{
    return QSize(kMeterChannelWidth, 100);
}

void LevelBarWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);

    const double span = m_maxDb - m_minDb;
    const double normalized = span > 0.0 ? std::clamp((m_currentDb - m_minDb) / span, 0.0, 1.0) : 0.0;

    // Discrete glowing LED segments, not one continuous fill -- see
    // LedMeterPainter's own doc comment; shared with VuMeterWidget's peak
    // meters so every bar meter in this panel reads as one consistent
    // "real hardware" language. No clip latch here (LevelBarWidget has no
    // separate clip concept beyond reaching the top of its own range).
    paintLedMeterBar(painter, rect(), static_cast<float>(normalized), false);
}

} // namespace radio::ui
