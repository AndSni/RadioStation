#include "core/Logging.h"
#include "db/Database.h"
#include "db/TrackRecord.h"
#include "ui/ConsoleTheme.h"
#include "ui/MainWindow.h"

#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMessageBox>

#include <gst/gst.h>

int main(int argc, char* argv[])
{
    gst_init(&argc, &argv);

    QApplication app(argc, argv);

    // Required because resources.qrc is compiled into rs_ui, a STATIC
    // library: nothing else references the generated resource-registration
    // symbol, so without this the linker silently strips that whole object
    // file and every ":/..." resource path (e.g. the clock's embedded LCD
    // font) resolves to nothing at runtime, with no error — it just quietly
    // falls back to a default font.
    Q_INIT_RESOURCE(resources);
    QCoreApplication::setOrganizationName(QStringLiteral("RadioStation"));
    QCoreApplication::setApplicationName(QStringLiteral("RadioStation"));
    QCoreApplication::setApplicationVersion(QStringLiteral(RS_APP_VERSION));
    // Human-readable name shown by window managers / global menu bars (KDE,
    // GNOME appmenu) — without this they fall back to the "rs_app" binary
    // name. MainWindow sets its own explicit window title, so this only fills
    // the menu-bar / taskbar identity slot.
    QGuiApplication::setApplicationDisplayName(
        QStringLiteral("RadioStation %1").arg(QStringLiteral(RS_APP_VERSION)));
    QGuiApplication::setDesktopFileName(QStringLiteral("radiostation"));

    // Embedded SVG (see src/ui/resources.qrc); rendered by Qt's qsvgicon
    // engine plugin — rs_app links Qt6::Svg to guarantee it's deployed.
    app.setWindowIcon(QIcon(QStringLiteral(":/app/icon.svg")));

    // Warm console tone for every plain QPushButton / toolbar / status bar,
    // so stock Qt widgets don't clash with the hand-painted console
    // controls. See ConsoleTheme::appStyleSheet().
    app.setStyleSheet(radio::ui::theme::appStyleSheet());

    qRegisterMetaType<radio::core::LogEntry>("radio::core::LogEntry");
    qRegisterMetaType<radio::db::TrackRecord>("radio::db::TrackRecord");

    RS_LOG_INFO("app", QStringLiteral("RadioStation starting (GStreamer %1)")
                            .arg(QString::fromUtf8(gst_version_string())));
    RS_LOG_INFO("app",
        QStringLiteral("Live log: %1 (tail -f this file to watch events in real time)")
            .arg(radio::core::Logger::liveLogFilePath()));

    if (!radio::db::Database::open()) {
        QMessageBox::critical(nullptr, QStringLiteral("RadioStation"),
            QStringLiteral("Failed to open the library database. See the debug console for details."));
        return 1;
    }

    int rc = 0;
    {
        // Scoped deliberately: window is stack-allocated, so without this
        // block it wouldn't be destroyed until main() itself returns —
        // AFTER gst_deinit() below. Closing the window only makes
        // app.exec() return; the MainWindow object (and everything it
        // owns — AudioEngine, every deck/cart GstSourcePipeline) stays
        // fully alive until its destructor actually runs. Confirmed via
        // gdb on a real hung process: gst_deinit() -> gst_task_cleanup_all()
        // -> g_thread_pool_free() blocked forever in g_cond_wait(), because
        // GStreamer's shared internal thread pool still had live decode
        // threads (typefind/id3demux/etc.) that nothing had ever told to
        // stop — AudioEngine::shutdown() (which does exactly that, via
        // MainWindow's destructor) hadn't run yet and, with the old
        // ordering, never would. This scope forces that teardown to
        // complete before gst_deinit() ever runs.
        radio::ui::MainWindow window;
        window.show();
        rc = app.exec();
    }

    RS_LOG_INFO("app", QStringLiteral("RadioStation shutting down (exit %1)").arg(rc));

    gst_deinit();
    return rc;
}
