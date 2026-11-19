// slc_tray — system-tray control plane (doc 6.1.4 + settings UI).
// Menu: wallpaper Running/Paused/Stopped, screensaver Start/Stop/Enabled,
// Settings…, Exit (restores the wallpaper that was set before we touched it).
#include "tray_controller.h"

#include <QApplication>
#include <QLockFile>
#include <QStandardPaths>
#include <QSystemTrayIcon>

#include "slc/common/logging.h"
#include "slc/platform/paths.h"

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("slc-tray"));
    QCoreApplication::setOrganizationName(
        QStringLiteral("SteamLibraryScreensaver"));

    // Tray apps must not die when the last window (e.g. configurator we
    // spawned) closes.
    QApplication::setQuitOnLastWindowClosed(false);

    slc::logging::initFileLogging(slc::platform::logDir());
    slc::logging::applyLevelFromEnv();

    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        SLC_LOG_ERROR("tray: no system tray available");
        return 1;
    }

    QLockFile lock(QStandardPaths::writableLocation(
                       QStandardPaths::TempLocation) +
                   QStringLiteral("/slc_tray.lock"));
    lock.setStaleLockTime(0);
    if (!lock.tryLock(100)) {
        SLC_LOG_INFO("tray: already running");
        return 0;
    }

    slc::tray::TrayController controller(
        QCoreApplication::applicationDirPath());
    return QApplication::exec();
}
