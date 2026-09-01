#pragma once

#include <QWidget>

namespace radio::ui {

// A small hand-painted "real LCD screen" readout — a dark screen showing
// the current value in the app's embedded LCD font (see
// UiStyleHelpers::lcdFontFamily(), the same font RadioStatisticsPanel's
// studio clock uses) glowing red, with a smaller static caption printed
// beneath it (the unit/name — "60Hz", "Vol", "GR", ...). Same custom-
// paintEvent house style as VuMeterWidget rather than QSS/QLabel styling —
// this is a genuine analog-meter-style readout, not styled text.
//
// This widget doesn't know about dB, percent, or Hz — callers format
// setValue()'s string however is meaningful for that control ("+6", "72%",
// "-1 dB"). setValue()/setCaption() both no-op (skip repainting) when the
// new string matches what's already shown — the guard lives here, not in
// callers, the same shape as OnAirLabel::setOnAir()'s own no-op-if-
// unchanged check — since a single mixer panel places many of these side
// by side (every slider, plus GR/Leveler/LUFS), and their values are
// refreshed on a fast (~40ms) timer whether or not anything actually moved.
//
// The glow is faked directly in paintEvent() (a few low-alpha offset copies
// of the digit text behind the crisp final draw) rather than via
// QGraphicsDropShadowEffect — deliberately, for the same reason
// IndicatorButton's LED avoids it: a mixer panel places two dozen-plus of
// these at once, and a QGraphicsEffect forces its own offscreen rasterized
// backing store per instance, real cost multiplied across that many
// concurrent widgets for a steady (non-animated) glow that doesn't need it.
class LcdReadout : public QWidget {
    Q_OBJECT

public:
    explicit LcdReadout(QWidget* parent = nullptr);

    // Set once at construction time in practice (a slider's frequency band,
    // a meter's name) — still a plain setter, no restriction against
    // changing it later.
    void setCaption(const QString& caption);
    QString caption() const { return m_caption; }

    // Caller-formatted value text, e.g. "+6", "72%", "-1 dB". Shows "--"
    // when empty (nothing to display yet, e.g. before the first meter tick).
    void setValue(const QString& value);
    QString value() const { return m_value; }

    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    QString m_caption;
    QString m_value;
};

} // namespace radio::ui
