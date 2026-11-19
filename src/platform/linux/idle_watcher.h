#pragma once

// §6.1.3 Path A — DBus idle watcher for org.freedesktop.ScreenSaver.
// Polls GetSessionIdleTime() on a timer and subscribes to ActiveChanged;
// emits activated() when the session should start the screensaver.

#include <QDBusConnection>
#include <QObject>
#include <QTimer>

namespace slc::host {

class IdleWatcher final : public QObject {
    Q_OBJECT
  public:
    explicit IdleWatcher(QObject* parent = nullptr);

    /// 0 = rely solely on the ActiveChanged signal (recommended default).
    void setPollThresholdSeconds(quint32 seconds) { m_thresholdSec = seconds; }
    /// Poll period for GetSessionIdleTime.
    void setPollIntervalMs(int ms) { m_timer.setInterval(ms); }

    /// True when org.freedesktop.ScreenSaver is reachable on the session bus.
    [[nodiscard]] bool serviceAvailable() const { return m_serviceOk; }

  signals:
    void activated();

  public slots:
    void start();
    void onActiveChanged(bool active);

  private slots:
    void pollIdleTime();

  private:
    QTimer m_timer;
    quint32 m_thresholdSec = 0;
    bool m_serviceOk = false;
    bool m_armed = true; // hysteresis: re-arm when idle drops below threshold
};

} // namespace slc::host
