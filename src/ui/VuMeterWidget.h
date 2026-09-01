#pragma once

#include <QWidget>

namespace radio::ui {

// Compact vertical stereo VU meter: two thin bars (L/R), instant attack /
// slow decay (real VU meters don't snap down instantly on every quiet
// sample — a hard-instant readout looks jittery and is hard to read), with
// a clip indicator segment at the top when a channel's peak crosses
// kClipThreshold. Purely a display widget — setLevel() is meant to be
// called periodically (see MixerPanelWidget's meter timer) with whatever
// AudioEngine::deckPeakLevel()/masterPeakLevel() reports; this widget owns
// no timer or engine reference of its own.
//
// Fixed width, expanding height (see the constructor's size policy) — it's
// meant to sit alongside sliders in a row that fills whatever vertical
// space the containing panel has, not a fixed pixel size.
class VuMeterWidget : public QWidget {
    Q_OBJECT

public:
    explicit VuMeterWidget(QWidget* parent = nullptr);

    // left/right: linear peak, 0..1+ (values above 1 are drawn clamped to
    // full height, still counted for the clip indicator).
    void setLevel(float left, float right);

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void timerEvent(QTimerEvent* event) override;

private:
    static constexpr float kClipThreshold = 0.98f;
    static constexpr float kDecayPerTick = 0.03f; // linear decay applied every ~40ms timer tick

    float m_displayLeft = 0.0f;
    float m_displayRight = 0.0f;
    bool m_clipLeft = false;
    bool m_clipRight = false;
    int m_decayTimerId = 0;
};

} // namespace radio::ui
