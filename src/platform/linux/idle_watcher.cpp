#include "idle_watcher.h"

#include "slc/common/logging.h"

#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusReply>

namespace slc::host {
namespace {

constexpr char kService[] = "org.freedesktop.ScreenSaver";
constexpr char kPath[] = "/org/freedesktop/ScreenSaver";
constexpr char kInterface[] = "org.freedesktop.ScreenSaver";

} // namespace

IdleWatcher::IdleWatcher(QObject* parent) : QObject(parent)
{
    connect(&m_timer, &QTimer::timeout, this, &IdleWatcher::pollIdleTime);
}

void IdleWatcher::start()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        SLC_LOG_WARN("IdleWatcher: no session bus; idle detection disabled");
        return;
    }
    m_serviceOk = bus.interface()->isServiceRegistered(
        QString::fromLatin1(kService));
    if (!m_serviceOk) {
        SLC_LOG_WARN(
            "IdleWatcher: {} not registered on session bus; screensaver will "
            "not auto-activate (Path A unsupported in this session)",
            kService);
        return;
    }

    // React to external blank/unblank (GNOME/KDE emit ActiveChanged when the
    // session screensaver kicks in — we take over rendering at that point).
    if (!bus.connect(QString::fromLatin1(kService), QString::fromLatin1(kPath),
                     QString::fromLatin1(kInterface),
                     QString::fromLatin1("ActiveChanged"), this,
                     SLOT(onActiveChanged(bool)))) {
        SLC_LOG_WARN("IdleWatcher: ActiveChanged subscription failed; "
                     "falling back to idle-time polling only");
    }

    m_timer.start();
    SLC_LOG_INFO("IdleWatcher: watching {} (poll={}ms, threshold={}s)",
                 kService, m_timer.interval(), m_thresholdSec);
}

void IdleWatcher::pollIdleTime()
{
    if (m_thresholdSec == 0) {
        return; // signal-driven mode
    }
    QDBusInterface iface(QString::fromLatin1(kService),
                         QString::fromLatin1(kPath),
                         QString::fromLatin1(kInterface),
                         QDBusConnection::sessionBus());
    const QDBusReply<quint32> reply = iface.call(QString::fromLatin1("GetSessionIdleTime"));
    if (!reply.isValid()) {
        return;
    }
    const quint32 idleSec = reply.value();
    if (m_armed && idleSec >= m_thresholdSec) {
        m_armed = false;
        emit activated();
    } else if (!m_armed && idleSec + 1 < m_thresholdSec) {
        m_armed = true; // user active again
    }
}

void IdleWatcher::onActiveChanged(bool active)
{
    if (active) {
        SLC_LOG_INFO("IdleWatcher: ActiveChanged(true)");
        emit activated();
    } else {
        m_armed = true; // session unblanked
    }
}

} // namespace slc::host
