// §6.5 control channel, Linux side: plain D-Bus slots on the session bus
// dispatch into ScreensaverApp::controlCommand (shared with the Windows
// named-pipe transport). D-Bus calls arrive on the Qt event thread;
// requestConfigReload/statusJson are thread-safe by design.

#include "control_dbus.h"

#include "slc/app/screensaver_app.h"
#include "slc/common/logging.h"

#include <QDBusConnection>
#include <QObject>

namespace slc::platform {
namespace {

class ControlAdaptor : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "io.github.slc.Control")

  public:
    using QObject::QObject;

  public slots:
    Q_SCRIPTABLE void Reload()
    {
        (void)app::ScreensaverApp::controlCommand("RELOAD");
    }
    Q_SCRIPTABLE QString GetStatus()
    {
        return QString::fromStdString(
            app::ScreensaverApp::controlCommand("STATUS"));
    }
    Q_SCRIPTABLE bool SetConfig(const QString& json)
    {
        const std::string reply = app::ScreensaverApp::controlCommand(
            "SETCONFIG " + json.toStdString());
        return reply == "ok";
    }
};

} // namespace

void registerControlAdaptor()
{
    auto bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        SLC_LOG_WARN("control: no session bus — control channel off");
        return;
    }
    // Leaked on purpose: outlives all sessions in this process.
    auto* adaptor = new ControlAdaptor;
    bus.registerObject("/io/github/slc/Control", adaptor,
                       QDBusConnection::ExportAllSlots);
    if (!bus.registerService("io.github.slc.Control")) {
        SLC_LOG_WARN("control: service name already owned");
    } else {
        SLC_LOG_INFO("control: io.github.slc.Control on session bus");
    }
}

} // namespace slc::platform

#include "control_dbus.moc"
