#include "wayland_idle.h"

#include "slc/common/logging.h"

#include <wayland-client-core.h>
#include <wayland-client-protocol.h>

#include "ext-idle-notify-v1-client-protocol.h"

namespace slc::platform {
namespace {

void onRegistryGlobal(void* data, wl_registry* registry, std::uint32_t name,
                      const char* interface, std::uint32_t version)
{
    auto* self = static_cast<WaylandIdleNotifier*>(data);
    if (std::string_view(interface) == ext_idle_notifier_v1_interface.name) {
        self->bindNotifier(registry, name,
                           std::min(version, 2u));
    } else if (std::string_view(interface) == wl_seat_interface.name &&
               !self->hasSeat()) {
        self->bindSeat(registry, name, std::min(version, 1u));
    }
}

void onRegistryRemove(void*, wl_registry*, std::uint32_t) {}

const wl_registry_listener kRegistryListener = {onRegistryGlobal,
                                                onRegistryRemove};

void onIdled(void* data, ext_idle_notification_v1*)
{
    static_cast<WaylandIdleNotifier*>(data)->notifyIdled();
}

void onResumed(void* data, ext_idle_notification_v1*)
{
    static_cast<WaylandIdleNotifier*>(data)->notifyResumed();
}

const ext_idle_notification_v1_listener kNotificationListener = {onIdled,
                                                               onResumed};

} // namespace

// Registry binding needs access to the private members — kept as small
// public-for-callback helpers (documented, not part of the public API).
bool WaylandIdleNotifier::hasSeat() const { return m_seat != nullptr; }
void WaylandIdleNotifier::bindNotifier(wl_registry* registry,
                                       std::uint32_t name,
                                       std::uint32_t version)
{
    m_notifier = static_cast<ext_idle_notifier_v1*>(
        wl_registry_bind(registry, name, &ext_idle_notifier_v1_interface,
                         version));
}
void WaylandIdleNotifier::bindSeat(wl_registry* registry,
                                   std::uint32_t name,
                                   std::uint32_t version)
{
    m_seat = static_cast<wl_seat*>(
        wl_registry_bind(registry, name, &wl_seat_interface, version));
}

WaylandIdleNotifier::~WaylandIdleNotifier()
{
    if (m_notification) {
        ext_idle_notification_v1_destroy(m_notification);
    }
    if (m_notifier) {
        ext_idle_notifier_v1_destroy(m_notifier);
    }
    if (m_seat) {
        wl_seat_destroy(m_seat);
    }
    if (m_registry) {
        wl_registry_destroy(m_registry);
    }
    if (m_display) {
        wl_display_disconnect(m_display);
    }
}

bool WaylandIdleNotifier::start(std::uint32_t timeoutMs,
                                std::function<void()> onIdle,
                                std::function<void()> onResumed)
{
    m_display = wl_display_connect(nullptr);
    if (!m_display) {
        return false;
    }
    m_registry = wl_display_get_registry(m_display);
    wl_registry_add_listener(m_registry, &kRegistryListener, this);
    wl_display_roundtrip(m_display); // globals
    if (!m_notifier || !m_seat) {
        wl_display_disconnect(m_display);
        m_display = nullptr;
        m_registry = nullptr;
        return false;
    }

    m_onIdle = std::move(onIdle);
    m_onResumed = std::move(onResumed);
    m_notification = ext_idle_notifier_v1_get_idle_notification(
        m_notifier, timeoutMs, m_seat);
    ext_idle_notification_v1_add_listener(m_notification,
                                          &kNotificationListener, this);
    wl_display_roundtrip(m_display); // bind the notification object
    SLC_LOG_INFO("wayland idle: ext-idle-notify-v1 armed at {} ms",
                 timeoutMs);
    return true;
}

int WaylandIdleNotifier::fd() const
{
    return m_display ? wl_display_get_fd(m_display) : -1;
}

void WaylandIdleNotifier::dispatch()
{
    if (!m_display) {
        return;
    }
    while (wl_display_prepare_read(m_display) != 0) {
        wl_display_dispatch_pending(m_display);
    }
    wl_display_flush(m_display);
    if (wl_display_read_events(m_display) != -1) {
        wl_display_dispatch_pending(m_display);
    }
}

void WaylandIdleNotifier::notifyIdled()
{
    if (m_onIdle) {
        m_onIdle();
    }
}

void WaylandIdleNotifier::notifyResumed()
{
    if (m_onResumed) {
        m_onResumed();
    }
}

} // namespace slc::platform
