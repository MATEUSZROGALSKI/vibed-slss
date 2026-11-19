# §6.1.3 — Qt daemon (IdleWatcher over org.freedesktop.ScreenSaver DBus)
# + a Cinder fullscreen session child process. Requires SLC_BUILD_QT_TOOLS.
# Gui is intentionally absent: the daemon never creates a window (the GL
# session child is pure Cinder), and Qt6Gui's config requires system OpenGL
# dev files that minimal/headless environments may not have.
find_package(Qt6 6.5 QUIET COMPONENTS Core DBus)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 (Core/DBus) not found; skipping slc_host_linux")
    return()
endif()

add_executable(slc_host_linux
    linux_main.cpp
    idle_watcher.cpp
)
set_target_properties(slc_host_linux PROPERTIES AUTOMOC ON)

if(SLC_BUILD_CINDER)
    target_sources(slc_host_linux PRIVATE session_cinder.cpp control_dbus.cpp)
    target_link_libraries(slc_host_linux PRIVATE slc::app cinder)
    # session_cinder reparents into $XSCREENSAVER_WINDOW for --xss mode and
    # drives the X11 live-wallpaper desktop window.
    find_package(X11)
    if(X11_FOUND)
        target_include_directories(slc_host_linux SYSTEM PRIVATE
            ${X11_INCLUDE_DIR})
        target_link_libraries(slc_host_linux PRIVATE
            ${X11_LIBRARIES} ${X11_Xext_LIB})
    else()
        message(WARNING "X11 dev files not found; --xss mode will fail to build")
    endif()

    # Wayland live wallpaper: wlr-layer-shell background surface fed by a
    # wl_shm buffer ring. Needs only libwayland-client + the scanner; the
    # protocol XML is vendored under wayland/.
    # HINTS cover sysroot-style dev setups (no sudo, libs extracted
    # under $HOME); standard toolchain paths still work via defaults.
    find_program(WAYLAND_SCANNER NAMES wayland-scanner
        PATHS /home/ubuntu/sysroot/usr/bin /usr/bin /usr/local/bin)
    find_library(WAYLAND_CLIENT_LIB NAMES wayland-client
        HINTS /home/ubuntu/sysroot/usr/lib/x86_64-linux-gnu)
    find_path(WAYLAND_CLIENT_INCLUDE wayland-client.h
        HINTS /home/ubuntu/sysroot/usr/include)
    if(WAYLAND_SCANNER AND WAYLAND_CLIENT_LIB AND WAYLAND_CLIENT_INCLUDE)
        enable_language(C) # wayland-scanner emits C
        set(_ls_xml ${CMAKE_CURRENT_SOURCE_DIR}/wayland/wlr-layer-shell-unstable-v1.xml)
        set(_ls_hdr ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-client-protocol.h)
        set(_ls_src ${CMAKE_CURRENT_BINARY_DIR}/wlr-layer-shell-unstable-v1-protocol.c)
        # layer-shell references xdg_popup for nested popups — its interface
        # symbol must be generated as well.
        set(_xdg_xml ${CMAKE_CURRENT_SOURCE_DIR}/wayland/xdg-shell.xml)
        set(_xdg_src ${CMAKE_CURRENT_BINARY_DIR}/xdg-shell-protocol.c)
        # §6.1.3 Path C: compositor-driven idle notification (replaces
        # the DBus idle poll under Wayland sessions).
        set(_in_xml ${CMAKE_CURRENT_SOURCE_DIR}/wayland/ext-idle-notify-v1.xml)
        set(_in_hdr ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-client-protocol.h)
        set(_in_src ${CMAKE_CURRENT_BINARY_DIR}/ext-idle-notify-v1-protocol.c)
        add_custom_command(OUTPUT ${_ls_hdr} ${_ls_src} ${_xdg_src} ${_in_hdr} ${_in_src}
            COMMAND ${WAYLAND_SCANNER} client-header ${_ls_xml} ${_ls_hdr}
            COMMAND ${WAYLAND_SCANNER} private-code ${_ls_xml} ${_ls_src}
            COMMAND ${WAYLAND_SCANNER} private-code ${_xdg_xml} ${_xdg_src}
            COMMAND ${WAYLAND_SCANNER} client-header ${_in_xml} ${_in_hdr}
            COMMAND ${WAYLAND_SCANNER} private-code ${_in_xml} ${_in_src}
            DEPENDS ${_ls_xml} ${_xdg_xml} ${_in_xml}
            VERBATIM)
        target_sources(slc_host_linux PRIVATE
            wayland/layer_wallpaper.cpp wayland/wayland_idle.cpp
            ${_ls_hdr} ${_ls_src} ${_xdg_src} ${_in_hdr} ${_in_src})
        target_include_directories(slc_host_linux SYSTEM PRIVATE
            ${CMAKE_CURRENT_BINARY_DIR} ${WAYLAND_CLIENT_INCLUDE})
        target_compile_definitions(slc_host_linux PRIVATE SLC_WAYLAND_WALLPAPER SLC_WAYLAND_IDLE)
        target_link_libraries(slc_host_linux PRIVATE ${WAYLAND_CLIENT_LIB})
        message(STATUS "Wayland live wallpaper: enabled (layer-shell)")
    else()
        message(WARNING "wayland-client/scanner not found; --wallpaper is X11-only")
    endif()
else()
    target_sources(slc_host_linux PRIVATE session_stub.cpp)
endif()

target_link_libraries(slc_host_linux PRIVATE
    Qt6::Core Qt6::DBus
    slc::appconfig
    slc::common
    slc::content
    slc::platform_iface
)
slc_set_warnings(slc_host_linux)

# systemd user unit (§6.1.3 Path A): session daemon started with the DE.
install(FILES systemd/slc-screensaverd.service
        DESTINATION lib/systemd/user)
install(TARGETS slc_host_linux RUNTIME DESTINATION bin)
