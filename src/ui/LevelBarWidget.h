#pragma once

#include <QWidget>

namespace radio::ui {

// A single-channel, dB-scaled bar meter -- same visual language as
// VuMeterWidget (color-thresholded fill, dark background, thin border) but
// mono and mapped over an explicit [minDb, maxDb] range instead of linear
// 0..1+ peak. Used for measurements that are already a single windowed/
// smoothed value from the engine (LUFS momentary/short-term/integrated,
// true peak) rather than a raw instantaneous peak -- unlike VuMeterWidget,
// there's no decay ballistics here: the value already IS the ballistics
// (see e.g. MixEngine's own doc comment on momentary/short-term windowing),
// so this just tracks whatever setLevelDb() reports each tick directly.
class LevelBarWidget : public QWidget {
    Q_OBJECT

public:
    // minDb/maxDb: the bar's full-scale range -- e.g. (-40, 0) for a LUFS
    // meter, (-12, 0) for a gain-reduction meter. A value outside this
    // range is clamped, not extrapolated past the bar's ends.
    LevelBarWidget(double minDb, double maxDb, QWidget* parent = nullptr);

    void setLevelDb(double db);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    double m_minDb;
    double m_maxDb;
    double m_currentDb;
};

} // namespace radio::ui
