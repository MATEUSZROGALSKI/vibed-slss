#pragma once

#include <QAction>
#include <QActionGroup>
#include <QDateTime>
#include <QMenu>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QString>
#include <QSystemTrayIcon>
#include <QTimer>

#include "slc/wallpaper/backend.h"

#include <memory>

namespace slc::tray {

/// Tray control plane: owns the wallpaper mode radio
/// (Running = GL session child, Paused = wallpaperd child doing static
/// swaps, Stopped = neither), screensaver controls, settings launcher and
/// exit-with-restore.
class TrayController final : public QObject {
    Q_OBJECT
  public:
    enum class WallpaperMode { Stopped, Paused, Running };
    Q_ENUM(WallpaperMode)

    explicit TrayController(const QString& exeDir, QObject* parent = nullptr);
    ~TrayController() override;

  private slots:
    void onModeAction(QAction* action);
    void onAutoTick();
    void onWallpaperFinished(int exitCode);
    void openSettings();
    void quitAndRestore();

  private:
    void syncProcesses();
    /// Stores the pre-session wallpaper path captured on the ctor's
    /// helper thread (queued back to the UI thread via invokeMethod).
    void storeOriginalWallpaper(QString path);
    void killChild(QProcess*& proc);

    QString m_exeDir;
    QString m_hostExe;
    QString m_wallpaperdExe;
    QString m_configuratorExe;
    QString m_scrPath;

    QSystemTrayIcon m_icon;
    QMenu m_menu;
    QActionGroup m_wpGroup{this};
    QAction* m_actRunning = nullptr;
    QAction* m_actPaused = nullptr;
    QAction* m_actStopped = nullptr;
    QAction* m_actSaverEnabled = nullptr;

    QProcess* m_wallpaperProc = nullptr;
    QProcess* m_wallpaperdProc = nullptr;
    QTimer m_autoTimer;
    QSettings m_settings;

    WallpaperMode m_requested = WallpaperMode::Stopped;
    bool m_autoPaused = false;
    // Auto-pause hysteresis: the fullscreen-notification state flaps for
    // single 2s ticks on this machine; require the new state to hold for
    // several ticks before killing/respawning the live host.
    int m_restStreak = 0;
    /// While valid-and-future, fullscreen detection is ignored — the
    /// wallpaper host's own window setup trips it briefly after spawn.
    QDateTime m_fullscreenGraceUntil;

    /// shared_ptr: detached restore/capture threads outlive the
    /// controller in edge cases (quit while a broadcast is blocked).
    std::shared_ptr<slc::wallpaper::IWallpaperBackend> m_backend;
    QString m_originalWallpaper;
};

} // namespace slc::tray
