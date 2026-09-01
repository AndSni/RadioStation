#include "ui/DotMatrixDisplay.h"

#include <QCoreApplication>
#include <QPixmap>
#include <QTest>

using namespace radio::ui;

class DotMatrixDisplayTest : public QObject {
    Q_OBJECT

private slots:
    void textAndTagRoundTrip();
    void rendersWithAndWithoutTagsAndOverflow();
};

void DotMatrixDisplayTest::textAndTagRoundTrip()
{
    DotMatrixDisplay display;
    QCOMPARE(display.text(), QString());

    display.setText(QStringLiteral("Artist — Title"));
    QCOMPARE(display.text(), QStringLiteral("Artist — Title"));

    display.setTagLine(QStringLiteral("HOUSE · 2013 · 320k"));
    QCOMPARE(display.tagLine(), QStringLiteral("HOUSE · 2013 · 320k"));
}

void DotMatrixDisplayTest::rendersWithAndWithoutTagsAndOverflow()
{
    DotMatrixDisplay display;
    for (const QSize size : { QSize(320, 60), QSize(120, 52), QSize(600, 80), QSize(10, 10) }) {
        display.resize(size);
        for (const QString& text :
            { QString(), QStringLiteral("Short"),
                QStringLiteral("A very long artist name — and an equally long track title that must scroll") }) {
            display.setText(text);
            for (const QString& tags : { QString(), QStringLiteral("HOUSE · 2013 · 320k · 124 BPM · -8.1 dB") }) {
                display.setTagLine(tags);
                QPixmap pm(display.size());
                pm.fill(Qt::transparent);
                display.render(&pm); // must not crash / assert
                QVERIFY(!pm.isNull());
            }
        }
    }
}

QTEST_MAIN(DotMatrixDisplayTest)
#include "DotMatrixDisplayTest.moc"
