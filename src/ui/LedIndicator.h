#pragma once

#include <QColor>
#include <QWidget>

namespace radio::ui {

// A small standalone status LED -- the same lit dot + concentric glow
// IndicatorButton paints above its cap, factored out as its own widget for
// places that need the indicator without a button under it (the Crossfader's
// A/B deck-active lights). Hand-painted, ConsoleTheme palette, no QSS.
class LedIndicator : public QWidget {
    Q_OBJECT

public:
    explicit LedIndicator(QWidget* parent = nullptr);

    void setOn(bool on);
    bool isOn() const { return m_on; }

    // Lit colour. Defaults to ConsoleTheme::kLedRed (this app's
    // "engaged / live" colour); the dim/off colour is derived from it.
    void setColor(const QColor& colour);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    bool m_on = false;
    QColor m_colour;
};

} // namespace radio::ui
