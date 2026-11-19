# slc_tray — Qt control plane: wallpaper mode radio, screensaver controls,
# settings launcher, exit-with-restore.
find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 Widgets not found; skipping slc_tray")
    return()
endif()

add_executable(slc_tray
    main.cpp
    tray_controller.cpp
)
if(WIN32)
    target_sources(slc_tray PRIVATE platform_win.cpp)
    target_link_libraries(slc_tray PRIVATE shlwapi)
elseif(UNIX AND NOT APPLE)
    target_sources(slc_tray PRIVATE platform_linux.cpp)
    find_package(X11)
    if(X11_FOUND)
        target_include_directories(slc_tray SYSTEM PRIVATE
            ${X11_INCLUDE_DIR})
        target_link_libraries(slc_tray PRIVATE ${X11_LIBRARIES})
    endif()
else()
    target_sources(slc_tray PRIVATE platform_other.cpp)
endif()
set_target_properties(slc_tray PROPERTIES AUTOMOC ON WIN32_EXECUTABLE TRUE)

target_link_libraries(slc_tray PRIVATE
    Qt6::Widgets
    slc::common
    slc::platform_iface
    slc::wallpaper
)
slc_set_warnings(slc_tray)

install(TARGETS slc_tray RUNTIME DESTINATION bin)
