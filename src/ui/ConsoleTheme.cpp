#include "ConsoleTheme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>

namespace radio::ui::theme {

QString appStyleSheet()
{
    return QStringLiteral(
        "QPushButton:!flat {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #48423a, stop:0.5 #34302b, stop:1 #22201b);"
        "  border: 1px solid #4a453c; border-radius: 4px; color: #e6e0d4; padding: 5px 14px; }"
        "QPushButton:!flat:hover {"
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #554e44, stop:0.5 #3e3931, stop:1 #2a2620); }"
        "QPushButton:!flat:pressed { background: #201e19; }"
        "QPushButton:!flat:checked { border: 1px solid #b5794f; color: #ffe9d8; "
        "  background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #4e3a2c, stop:1 #2a2018); }"
        "QPushButton:!flat:disabled { color: #776f63; border-color: #3a352d; background: #2a2723; }"
        "QToolBar { background: #1c1a16; border: none; spacing: 6px; padding: 4px 6px; }"
        "QStatusBar { background: #1c1a16; color: #b8ae9a; }");
}

QString consoleBezelStyle()
{
    return QStringLiteral(
        "QGroupBox { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #2a2620, stop:1 #1c1a16); "
        "border: 1px solid #4a453c; border-radius: 6px; margin-top: 14px; padding-top: 6px; } "
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top center; padding: 0 8px; "
        "color: #cfc6b4; font-weight: 600; }");
}

QString consolePanelStyle(const QString& objectName)
{
    return QStringLiteral("QFrame#%1 { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, "
                          "stop:0 #2a2620, stop:1 #1c1a16); border: 1px solid #4a453c; border-radius: 6px; }")
        .arg(objectName);
}

QString lcdScreenFrameStyle(const QString& objectName)
{
    return QStringLiteral("QFrame#%1 { background-color: %2; border: 1px solid %3; border-radius: 6px; }")
        .arg(objectName, kLcdScreen.name(), kLcdRim.name());
}

QString scaleLabelStyle()
{
    return QStringLiteral("color: #8a8377; font-size: 10px;");
}

void paintConsoleCap(QPainter& painter, const QRectF& rect, bool hovered, bool pressed, bool horizontal)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    // Leave 2px at the bottom of `rect` for the drop shadow so the cap body
    // never touches the rect edge on the lit side.
    const QRectF body = rect.adjusted(0.5, 0.5, -0.5, -2.5);
    const double radius = 3.5;

    // Soft shadow: same "one low-alpha pass behind the real shape" trick the
    // LED/LCD glows use, no QGraphicsEffect.
    QColor shadow(0, 0, 0, 90);
    painter.setPen(Qt::NoPen);
    painter.setBrush(shadow);
    painter.drawRoundedRect(body.translated(0, 2.0), radius, radius);

    // Body gradient -- light source at the top for either orientation.
    QColor top = kCapTop.lighter(114);
    QColor mid = kCapMid;
    QColor bottom = kCapBottom.darker(112);
    if (pressed) {
        top = kCapPressed.lighter(118);
        mid = kCapPressed;
        bottom = kCapPressed.darker(118);
    } else if (hovered) {
        top = kCapTop.lighter(124);
        mid = kCapHover;
        bottom = kCapBottom.darker(105);
    }
    QLinearGradient grad(body.topLeft(), body.bottomLeft());
    grad.setColorAt(0.0, top);
    grad.setColorAt(0.5, mid);
    grad.setColorAt(1.0, bottom);
    painter.setBrush(grad);
    painter.setPen(QPen(kCapEdgeDark, 1.0));
    painter.drawRoundedRect(body, radius, radius);

    // Bevel: a light edge just inside the top, a dark one just inside the
    // bottom -- the two 1px lines that sell "moulded cap" rather than "flat
    // rectangle".
    QPainterPath clip;
    clip.addRoundedRect(body, radius, radius);
    painter.setClipPath(clip);
    painter.setPen(QPen(kCapEdgeLight, 1.0));
    painter.drawLine(body.topLeft() + QPointF(1.5, 1.0), body.topRight() + QPointF(-1.5, 1.0));
    painter.setPen(QPen(kCapEdgeDark, 1.0));
    painter.drawLine(body.bottomLeft() + QPointF(1.5, -1.0), body.bottomRight() + QPointF(-1.5, -1.0));

    // Faint vertical sheen a third of the way across -- a moving highlight
    // like light catching the ridge of a real cap.
    QLinearGradient sheen(body.left(), 0, body.right(), 0);
    sheen.setColorAt(0.0, QColor(255, 255, 255, 0));
    sheen.setColorAt(0.30, QColor(255, 255, 255, 22));
    sheen.setColorAt(0.42, QColor(255, 255, 255, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(sheen);
    painter.drawRect(body);

    // Engraved knurl grooves -- one subtle pair (dark line + light line
    // under it) each side of centre, well clear of it so a caller's bright
    // position line / label sits on clean metal.
    painter.setBrush(Qt::NoBrush);
    QColor knurlDark = kCapEdgeDark;
    knurlDark.setAlpha(150);
    QColor knurlLight = kCapEdgeLight;
    knurlLight.setAlpha(120);
    const double along = horizontal ? body.width() : body.height();
    const double centre = horizontal ? body.center().x() : body.center().y();
    const double off = along * 0.27;
    for (double s : { -off, off }) {
        if (horizontal) {
            const double x = centre + s;
            painter.setPen(QPen(knurlDark, 1.0));
            painter.drawLine(QPointF(x, body.top() + 4), QPointF(x, body.bottom() - 4));
            painter.setPen(QPen(knurlLight, 1.0));
            painter.drawLine(QPointF(x + 1, body.top() + 4), QPointF(x + 1, body.bottom() - 4));
        } else {
            const double y = centre + s;
            painter.setPen(QPen(knurlDark, 1.0));
            painter.drawLine(QPointF(body.left() + 4, y), QPointF(body.right() - 4, y));
            painter.setPen(QPen(knurlLight, 1.0));
            painter.drawLine(QPointF(body.left() + 4, y + 1), QPointF(body.right() - 4, y + 1));
        }
    }

    painter.restore();
}

} // namespace radio::ui::theme
