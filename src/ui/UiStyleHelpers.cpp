#include "UiStyleHelpers.h"

#include <QFontDatabase>
#include <QStringList>

namespace radio::ui {

QString checkableActiveButtonStyle()
{
    return QStringLiteral(
        "QPushButton:checked { background-color: #15803d; border: 2px solid #4ade80; color: white; font-weight: bold; }");
}

QString lcdFontFamily()
{
    static const QString family = []() -> QString {
        const int id = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/lcd_att_phone_time_date.ttf"));
        if (id < 0)
            return QString();
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        return families.isEmpty() ? QString() : families.first();
    }();
    return family;
}

} // namespace radio::ui
