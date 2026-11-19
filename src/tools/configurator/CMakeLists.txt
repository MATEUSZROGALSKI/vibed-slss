# §5.8 — Qt Widgets settings app (the real /c target).
find_package(Qt6 6.5 QUIET COMPONENTS Widgets)
if(NOT Qt6_FOUND)
    message(WARNING "Qt6 Widgets not found; skipping slc_configurator")
    return()
endif()

add_executable(slc_configurator
    main.cpp
    config_window.cpp
)
set_target_properties(slc_configurator PROPERTIES AUTOMOC ON WIN32_EXECUTABLE TRUE)

target_link_libraries(slc_configurator PRIVATE
    Qt6::Widgets
    slc::appconfig
    slc::common
    slc::content
    slc::platform_iface
)
slc_set_warnings(slc_configurator)

install(TARGETS slc_configurator RUNTIME DESTINATION bin)
