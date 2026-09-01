#include "MixerSliderHelpers.h"
#include "ConsoleFader.h"
#include "ConsoleTheme.h"
#include "LcdReadout.h"

#include <QLabel>
#include <QSizePolicy>
#include <QSlider>
#include <QVBoxLayout>

namespace radio::ui {

namespace {
const QString kScaleLabelStyle = theme::scaleLabelStyle();

QLabel* makeScaleLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setAlignment(Qt::AlignHCenter);
    label->setStyleSheet(kScaleLabelStyle);
    return label;
}
}

QSlider* makeExpandingSlider(int minValue, int maxValue, int defaultValue, QWidget* parent, bool zeroCenteredTicks)
{
    auto* slider = new ConsoleFader(Qt::Vertical, parent);
    slider->setRange(minValue, maxValue);
    slider->setValue(defaultValue);
    slider->setFixedWidth(44); // room for the knurled cap + the etched scale strip; column stays kMixerColumnWidth
    slider->setMinimumHeight(80);
    slider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    // ConsoleFader paints its own etched scale: minor ticks every ~1/12 of
    // the span, plus a heavier centre detent tick on the zero-centred
    // faders (EQ bands, master Bass/Treble). A 0 interval just means "no
    // minor ticks" (small ranges like the -3..0 limiter ceiling).
    slider->setTickInterval((maxValue - minValue) / 12);
    slider->setCenterDetent(zeroCenteredTicks);
    return slider;
}

QWidget* makeScaledColumn(
    QWidget* control, const QString& maxLabel, const QString& minLabel, const QString& name, QWidget* parent)
{
    auto* column = new QWidget(parent);
    column->setFixedWidth(kMixerColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    layout->addWidget(makeScaleLabel(maxLabel, column));
    layout->addWidget(control, 1, Qt::AlignHCenter); // stretch=1: this is what actually expands
    layout->addWidget(makeScaleLabel(minLabel, column));

    auto* nameLabelWidget = new QLabel(name, column);
    nameLabelWidget->setAlignment(Qt::AlignHCenter);
    layout->addWidget(nameLabelWidget);

    return column;
}

ScaledColumn makeScaledColumnWithReadout(
    QWidget* control, const QString& maxLabel, const QString& minLabel, const QString& caption, QWidget* parent)
{
    auto* column = new QWidget(parent);
    column->setFixedWidth(kMixerColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    layout->addWidget(makeScaleLabel(maxLabel, column));
    layout->addWidget(control, 1, Qt::AlignHCenter); // stretch=1: this is what actually expands
    layout->addWidget(makeScaleLabel(minLabel, column));

    auto* readout = new LcdReadout(column);
    readout->setCaption(caption);
    layout->addWidget(readout, 0, Qt::AlignHCenter);

    return { column, readout };
}

QWidget* wrapWithCaption(QWidget* control, const QString& caption, QWidget* parent)
{
    auto* column = new QWidget(parent);
    column->setFixedWidth(kMixerColumnWidth);
    auto* layout = new QVBoxLayout(column);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(4);
    // No internal stretch here -- this is stacked (possibly several at a
    // time, see each strip's button-column construction) inside a taller
    // outer column that adds its own single leading stretch to push the
    // whole stack down to the sliders' baseline; a stretch on every
    // individual item here as well would fight that instead of stacking
    // tightly.

    layout->addWidget(control, 0, Qt::AlignHCenter);

    auto* captionLabel = new QLabel(caption, column);
    captionLabel->setAlignment(Qt::AlignHCenter);
    captionLabel->setStyleSheet(kScaleLabelStyle);
    layout->addWidget(captionLabel);

    return column;
}

} // namespace radio::ui
