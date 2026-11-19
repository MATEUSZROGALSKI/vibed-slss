#include "tray_controller.h"

#include "platform_helpers.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QMetaObject>
#include <QPainter>
#include <QPixmap>
#include <QSignalBlocker>
#include <QStyle>

#include <filesystem>
#include <thread>

#include "slc/common/logging.h"

namespace slc::tray {
namespace {

QIcon makeIcon()
{
    // Placeholder icon: dark rounded square + "S". Replace with a real
    // .ico resource when branding lands.
    QPixmap px(32, 32);
    px.fill(Qt::transparent);
    QPainter p(&px);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x1b, 0x28, 0x3b));
    p.drawRoundedRect(px.rect().adjusted(1, 1, -1, -1), 6, 6);
    p.setPen(QColor(0x66, 0xc0, 0xf4));
    QFont f = p.font();
    f.setPixelSize(18);
    f.setBold(true);
    p.setFont(f);
    p.drawText(px.rect(), Qt::AlignCenter, QStringLiteral("S"));
    return QIcon(px);
}

QString stateFilePath()
{
    return QDir::temp().filePath(QStringLiteral("slc_tray_wallpaper.txt"));
}

} // namespace

TrayController::TrayController(const QString& exeDir, QObject* parent)
    : QObject(parent),
      m_exeDir(exeDir),
#if defined(Q_OS_WIN)
      m_hostExe(exeDir + QStringLiteral("/slc_host_win.exe")),
      m_wallpaperdExe(exeDir + QStringLiteral("/slc_wallpaperd.exe")),
      m_configuratorExe(exeDir +
                        QStringLiteral("/slc_configurator.exe")),
      m_scrPath(exeDir + QStringLiteral("/SteamLibraryScreensaver.scr")),
#else
      m_hostExe(exeDir + QStringLiteral("/slc_host_linux")),
      m_wallpaperdExe(exeDir + QStringLiteral("/slc_wallpaperd")),
      m_configuratorExe(exeDir + QStringLiteral("/slc_configurator")),
      m_scrPath(exeDir + QStringLiteral("/slc-screensaverd.service")),
#endif
      m_settings(QStringLiteral("SteamLibraryScreensaver"),
                   QStringLiteral("Tray")),
      m_backend(slc::wallpaper::makeWallpaperBackend())
{
    // Crash recovery: a leftover state file means the previous session
    // died before restoring — do it now, then capture afresh. Runs on a
    // helper thread: IDesktopWallpaper is an out-of-proc COM call into
    // explorer and the SPI fallback broadcasts WM_SETTINGCHANGE, which
    // stalls indefinitely on a hung top-level window (a ghost left by a
    // killed wallpaper host is exactly that).
    std::thread([this, backend = m_backend] {
        {
            QFile state(stateFilePath());
            if (state.exists() && state.open(QIODevice::ReadOnly)) {
                const QString prior =
                    QString::fromUtf8(state.readAll()).trimmed();
                state.close();
                if (!prior.isEmpty() && backend) {
                    if (auto r = backend->setWallpaper(
                            std::filesystem::path(prior.toStdWString()));
                        !r) {
                        SLC_LOG_WARN(
                            "tray: stale wallpaper restore failed: {}",
                            r.error().message);
                    }
                }
                QFile::remove(stateFilePath());
            }
        }
        if (!backend) {
            return;
        }
        if (auto cur = backend->currentWallpaper()) {
            // Queued back to the UI thread; safely dropped if this
            // controller is already gone.
            QMetaObject::invokeMethod(
                this, &TrayController::storeOriginalWallpaper,
                Qt::QueuedConnection,
                QString::fromStdWString(cur->wstring()));
        }
    }).detach();

    m_icon.setIcon(makeIcon());
    m_icon.setToolTip(QStringLiteral("Steam Library Screensaver"));

    auto* wpMenu = m_menu.addMenu(QStringLiteral("Wallpaper"));
    m_wpGroup.setExclusive(true);
    auto addMode = [&](const QString& label, WallpaperMode mode) {
        QAction* a = wpMenu->addAction(label);
        a->setCheckable(true);
        a->setData(static_cast<int>(mode));
        m_wpGroup.addAction(a);
        return a;
    };
    m_actRunning = addMode(QStringLiteral("Running (animated)"),
                           WallpaperMode::Running);
    m_actPaused = addMode(QStringLiteral("Paused (static swaps)"),
                          WallpaperMode::Paused);
    m_actStopped =
        addMode(QStringLiteral("Stopped"), WallpaperMode::Stopped);
    m_actRunning->setEnabled(tray_plat::supportsLiveWallpaper());
    connect(&m_wpGroup, &QActionGroup::triggered, this,
            &TrayController::onModeAction);
    if (!tray_plat::supportsLiveWallpaper()) {
        wpMenu->setToolTip(QStringLiteral(
            "Live wallpaper is not supported on this platform"));
    }

    auto* svMenu = m_menu.addMenu(QStringLiteral("Screensaver"));
    svMenu->addAction(QStringLiteral("Start now"), this, [] {
        tray_plat::startScreensaverNow(
            QCoreApplication::applicationDirPath() +
            QStringLiteral("/slc_host_win.exe"));
    });
    svMenu->addAction(QStringLiteral("Stop active"), this,
                      [] { tray_plat::stopActiveScreensaver(); });
    m_actSaverEnabled =
        svMenu->addAction(QStringLiteral("Enabled (system saver)"));
    m_actSaverEnabled->setCheckable(true);
    m_actSaverEnabled->setChecked(tray_plat::screensaverEnabled(m_scrPath));
    connect(m_actSaverEnabled, &QAction::toggled, this, [this](bool on) {
        QString prev;
        const QString saved =
            m_settings.value(QStringLiteral("previousSaver")).toString();
        if (tray_plat::setScreensaverEnabled(m_scrPath, on,
                                             on ? &prev : nullptr,
                                             on ? QString() : saved)) {
            if (on) {
                m_settings.setValue(QStringLiteral("previousSaver"), prev);
            }
        } else {
            QSignalBlocker b(*m_actSaverEnabled);
            m_actSaverEnabled->setChecked(!on);
        }
    });

    m_menu.addSeparator();
    m_menu.addAction(QStringLiteral("Settings…"), this,
                     &TrayController::openSettings);
    m_menu.addAction(QStringLiteral("Exit"), this,
                     &TrayController::quitAndRestore);

    m_icon.setContextMenu(&m_menu);
    m_icon.show();

    connect(&m_autoTimer, &QTimer::timeout, this,
            &TrayController::onAutoTick);
    m_autoTimer.start(2000);

    // Restore the mode the user last left us in.
    const int last = m_settings.value(QStringLiteral("wallpaperMode"),
                                      static_cast<int>(WallpaperMode::Stopped))
                         .toInt();
    m_requested = static_cast<WallpaperMode>(last);
    if (m_requested == WallpaperMode::Running &&
        !tray_plat::supportsLiveWallpaper()) {
        m_requested = WallpaperMode::Paused;
    }
    QAction* check =
        m_requested == WallpaperMode::Running   ? m_actRunning
        : m_requested == WallpaperMode::Paused  ? m_actPaused
                                                : m_actStopped;
    check->setChecked(true);
    syncProcesses();
    SLC_LOG_INFO("tray: started, wallpaperMode={}", last);
}

TrayController::~TrayController()
{
    killChild(m_wallpaperProc);
    killChild(m_wallpaperdProc);
}

void TrayController::storeOriginalWallpaper(QString path)
{
    m_originalWallpaper = std::move(path);
    QFile state(stateFilePath());
    if (state.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        state.write(m_originalWallpaper.toUtf8());
    }
}

void TrayController::killChild(QProcess*& proc)
{
    if (proc == nullptr) {
        return;
    }
    proc->disconnect(this);
    proc->kill();
    proc->waitForFinished(3000);
    delete proc;
    proc = nullptr;
}

void TrayController::syncProcesses()
{
    const WallpaperMode effective =
        (m_autoPaused && m_requested == WallpaperMode::Running)
            ? WallpaperMode::Paused
            : m_requested;

    switch (effective) {
    case WallpaperMode::Running: {
        killChild(m_wallpaperdProc);
        if (m_wallpaperProc == nullptr) {
            auto* p = new QProcess(this);
            connect(p, &QProcess::finished, this,
                    &TrayController::onWallpaperFinished);
            p->setProgram(m_hostExe);
            p->setArguments({QStringLiteral("--wallpaper")});
            p->start();
            m_wallpaperProc = p;
            // The host's window setup briefly reads as a fullscreen app to
            // SHQueryUserNotificationState — suppress fullscreen-triggered
            // auto-pause while it embeds into the desktop.
            m_fullscreenGraceUntil = QDateTime::currentDateTime().addSecs(15);
            SLC_LOG_INFO("tray: live wallpaper started");
        }
        break;
    }
    case WallpaperMode::Paused: {
        killChild(m_wallpaperProc);
        if (m_wallpaperdProc == nullptr) {
            auto* p = new QProcess(this);
            p->setProgram(m_wallpaperdExe);
            p->start();
            m_wallpaperdProc = p;
            SLC_LOG_INFO("tray: static wallpaper rotation started");
        }
        break;
    }
    case WallpaperMode::Stopped:
        killChild(m_wallpaperProc);
        killChild(m_wallpaperdProc);
        break;
    }
}

void TrayController::onModeAction(QAction* action)
{
    m_requested = static_cast<WallpaperMode>(action->data().toInt());
    m_autoPaused = false; // manual choice wins over auto-pause
    m_settings.setValue(QStringLiteral("wallpaperMode"),
                        static_cast<int>(m_requested));
    syncProcesses();
}

void TrayController::onAutoTick()
{
    if (m_requested != WallpaperMode::Running) {
        m_restStreak = 0;
        return;
    }
    const bool fsGrace =
        m_fullscreenGraceUntil.isValid() &&
        QDateTime::currentDateTime() < m_fullscreenGraceUntil;
    const bool shouldRest =
        tray_plat::onBattery() || tray_plat::sessionLocked() ||
        (tray_plat::fullscreenAppBusy() && !fsGrace);
    if (shouldRest == m_autoPaused) {
        m_restStreak = 0;
        return;
    }
    // Debounce: a state that doesn't persist for a few ticks is a flap,
    // not a real fullscreen app — don't restart the wallpaper for it.
    constexpr int kRestStreakNeeded = 3; // ~6s at the 2s tick
    if (++m_restStreak < kRestStreakNeeded) {
        return;
    }
    m_restStreak = 0;
    m_autoPaused = shouldRest;
    SLC_LOG_INFO(
        "tray: auto-{} (battery={}, fullscreen={}, locked={} cause={})",
        shouldRest ? "pause" : "resume", tray_plat::onBattery(),
        tray_plat::fullscreenAppBusy(), tray_plat::sessionLocked(),
        tray_plat::describeFullscreenCause().toStdString());
    syncProcesses();
}

void TrayController::onWallpaperFinished(int exitCode)
{
    if (m_wallpaperProc != nullptr) {
        m_wallpaperProc->deleteLater();
        m_wallpaperProc = nullptr;
    }
    // The GL session died unexpectedly (driver reset, crash): fall back to
    // paused rather than leaving the desktop blank or crash-looping.
    if (m_requested == WallpaperMode::Running && !m_autoPaused) {
        SLC_LOG_WARN("tray: wallpaper session exited (code={}); falling "
                     "back to static swaps",
                     exitCode);
        m_autoPaused = true;
        syncProcesses();
    }
}

void TrayController::openSettings()
{
    QProcess::startDetached(m_configuratorExe, {}, m_exeDir);
}

void TrayController::quitAndRestore()
{
    killChild(m_wallpaperProc);
    killChild(m_wallpaperdProc);
    if (!m_originalWallpaper.isEmpty() && m_backend) {
        // Restore can block on a hung broadcast (see ctor note); run it
        // detached so Exit always exits. The call typically completes in
        // milliseconds — the process just doesn't wait for stragglers.
        const auto path =
            std::filesystem::path(m_originalWallpaper.toStdWString());
        std::thread([backend = m_backend, path] {
            if (auto r = backend->setWallpaper(path); !r) {
                SLC_LOG_WARN("tray: wallpaper restore failed: {}",
                             r.error().message);
            }
        }).detach();
    }
    QFile::remove(stateFilePath());
    SLC_LOG_INFO("tray: exit, wallpaper restored");
    QApplication::quit();
}

} // namespace slc::tray
