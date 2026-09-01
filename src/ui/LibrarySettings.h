#pragma once

#include <QString>

namespace radio::ui::library_settings {

// Shared between LibrarySettingsDialog (writes this) and MainWindow's
// "Refresh Library" action (reads it) -- kept in one place so the two never
// drift apart on the key name, same convention as station_settings.

const QString kRootPath = QStringLiteral("library/rootPath");

} // namespace radio::ui::library_settings
