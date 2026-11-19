#pragma once

// §6.1.3 Path C — ext-idle-notify-v1 idle watcher for Wayland sessions.
// Owns a private wl_display connection (the daemon's Qt loop never
// creates one); driven from the main thread via QSocketNotifier on the
// wayland fd — dispatch() pumps events, the idled/resumed callbacks are
// invoked synchronously on that thread.

#include <cstdint>
#include <functional>

struct ext_idle_notification_v1;
struct ext_idle_notifier_v1;
struct wl_display;
struct wl_registry;
struct wl_seat;

namespace slc::platform {

class WaylandIdleNotifier {
  public:
    WaylandIdleNotifier() = default;
    ~WaylandIdleNotifier();
    WaylandIdleNotifier(const WaylandIdleNotifier&) = delete;
    WaylandIdleNotifier& operator=(const WaylandIdleNotifier&) = delete;

    /// Connects, binds ext_idle_notifier_v1 + a seat, and registers one
    /// notification at `timeoutMs`. Returns false when the compositor
    /// lacks the protocol (caller falls back to the DBus watcher).
    [[nodiscard]] bool start(std::uint32_t timeoutMs,
                             std::function<void()> onIdle,
                             std::function<void()> onResumed);

    /// Wayland connection fd — watch with QSocketNotifier(Activated).
    [[nodiscard]] int fd() const;

    /// Pumps pending wayland events; invoke when fd() is readable.
    void dispatch();

    /// Called when the seat goes idle / resumes.
    void notifyIdled();
    void notifyResumed();

    /// Registry-callback helpers — invoked from the C listener, not part
    /// of the public API.
    [[nodiscard]] bool hasSeat() const;
    void bindNotifier(wl_registry* registry, std::uint32_t name,
                      std::uint32_t version);
    void bindSeat(wl_registry* registry, std::uint32_t name,
                  std::uint32_t version);

  private:
    wl_display* m_display = nullptr;
    wl_registry* m_registry = nullptr;
    ext_idle_notifier_v1* m_notifier = nullptr;
    ext_idle_notification_v1* m_notification = nullptr;
    wl_seat* m_seat = nullptr;
    std::function<void()> m_onIdle;
    std::function<void()> m_onResumed;
};

} // namespace slc::platform
