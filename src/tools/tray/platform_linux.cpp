// Linux tray backend: power/session probes via sysfs + loginctl,
// screensaver control via the slc-screensaverd systemd user unit, and an
// Xlib _NET_ACTIVE_WINDOW probe for the exclusive-fullscreen auto-pause.
#include "platform_helpers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

#include <cstring>
#include <string>

#include <X11/Xatom.h>
#include <X11/Xlib.h>

namespace slc::tray_plat {
namespace {

// Runs a tool, returns trimmed stdout on exit-0, nullopt otherwise.
QString runOut(const QString& prog, const QStringList& args)
{
    QProcess p;
    p.start(prog, args);
    if (!p.waitForFinished(3000) || p.exitCode() != 0) {
        return {};
    }
    return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
}

bool runOk(const QString& prog, const QStringList& args)
{
    QProcess p;
    p.start(prog, args);
    return p.waitForFinished(3000) && p.exitCode() == 0;
}

} // namespace

bool onBattery()
{
    // Any present battery discharging, or every AC adapter offline.
    const QDir ps(QStringLiteral("/sys/class/power_supply"));
    bool sawAc = false;
    bool acOnline = false;
    for (const auto& e : ps.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile typeFile(e.absoluteFilePath() + QStringLiteral("/type"));
        if (!typeFile.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString type = QString::fromUtf8(typeFile.readAll()).trimmed();
        if (type == QLatin1String("Mains")) {
            sawAc = true;
            QFile on(e.absoluteFilePath() + QStringLiteral("/online"));
            if (on.open(QIODevice::ReadOnly) &&
                QString::fromUtf8(on.readAll()).trimmed() == QLatin1String("1")) {
                acOnline = true;
            }
        } else if (type == QLatin1String("Battery")) {
            QFile st(e.absoluteFilePath() + QStringLiteral("/status"));
            if (st.open(QIODevice::ReadOnly) &&
                QString::fromUtf8(st.readAll()).trimmed() ==
                    QLatin1String("Discharging")) {
                return true;
            }
        }
    }
    return sawAc && !acOnline;
}

bool fullscreenAppBusy()
{
    // X11 only — Wayland compositors don't expose a cross-DE fullscreen
    // probe; the check degrades to false there.
    if (std::getenv("WAYLAND_DISPLAY") != nullptr &&
        std::getenv("DISPLAY") == nullptr) {
        return false;
    }
    ::Display* dpy = XOpenDisplay(nullptr);
    if (dpy == nullptr) {
        return false;
    }
    const ::Window root = DefaultRootWindow(dpy);
    const ::Atom activeAtom = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", True);
    const ::Atom stateAtom = XInternAtom(dpy, "_NET_WM_STATE", True);
    const ::Atom fsAtom =
        XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", True);
    bool busy = false;
    if (activeAtom != None && stateAtom != None && fsAtom != None) {
        ::Atom type = None;
        int fmt = 0;
        unsigned long n = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(dpy, root, activeAtom, 0, 1, False, XA_WINDOW,
                               &type, &fmt, &n, &after, &data) == Success &&
            data != nullptr && n == 1) {
            const ::Window active = *reinterpret_cast<::Window*>(data);
            XFree(data);
            data = nullptr;
            if (XGetWindowProperty(dpy, active, stateAtom, 0, 64, False,
                                   XA_ATOM, &type, &fmt, &n, &after,
                                   &data) == Success &&
                data != nullptr) {
                const auto* states = reinterpret_cast<::Atom*>(data);
                for (unsigned long i = 0; i < n; ++i) {
                    if (states[i] == fsAtom) {
                        busy = true;
                        break;
                    }
                }
                XFree(data);
            }
        } else if (data != nullptr) {
            XFree(data);
        }
    }
    XCloseDisplay(dpy);
    return busy;
}

QString describeFullscreenCause()
{
    return QStringLiteral("x11-fullscreen-check");
}

bool sessionLocked()
{
    // loginctl LockedHint for the current session.
    static const QString session = qEnvironmentVariable("XDG_SESSION_ID");
    const QString hint =
        runOut(QStringLiteral("loginctl"),
               {QStringLiteral("show-session"), session,
                QStringLiteral("-p"), QStringLiteral("LockedHint"),
                QStringLiteral("--value")});
    return hint == QLatin1String("yes");
}

bool screensaverEnabled(const QString& /*unitPath*/)
{
    return runOut(QStringLiteral("systemctl"),
                  {QStringLiteral("--user"), QStringLiteral("is-enabled"),
                   QStringLiteral("slc-screensaverd.service")}) ==
           QLatin1String("enabled");
}

bool setScreensaverEnabled(const QString& /*unitPath*/, bool enable,
                           QString* previousOut, const QString& previousIn)
{
    // No displaced saver to restore: the systemd unit model doesn't
    // conflict with other savers — remember nothing, disable stops us.
    if (previousOut != nullptr) {
        *previousOut = {};
    }
    return runOk(QStringLiteral("systemctl"),
                 {QStringLiteral("--user"),
                  enable ? QStringLiteral("enable")
                         : QStringLiteral("disable"),
                  QStringLiteral("--now"),
                  QStringLiteral("slc-screensaverd.service")});
}

void startScreensaverNow(const QString& exePath)
{
    QProcess::startDetached(exePath, {QStringLiteral("--run")});
}

void stopActiveScreensaver()
{
    // The daemon's --run child is the visible session.
    QProcess::startDetached(
        QStringLiteral("pkill"),
        {QStringLiteral("-f"), QStringLiteral("slc_host_linux --run")});
}

bool supportsLiveWallpaper()
{
    // SteamOS gaming mode runs under gamescope — no desktop layer at all.
    const QString desktop = qEnvironmentVariable("XDG_CURRENT_DESKTOP")
                                .toLower();
    if (desktop.contains(QLatin1String("gamescope"))) {
        return false;
    }
    return qEnvironmentVariableIsSet("WAYLAND_DISPLAY") ||
           qEnvironmentVariableIsSet("DISPLAY");
}

} // namespace slc::tray_plat
