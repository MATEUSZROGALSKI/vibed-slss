add_library(slc_platform_iface STATIC)
add_library(slc::platform_iface ALIAS slc_platform_iface)

target_include_directories(slc_platform_iface PUBLIC include)

if(WIN32)
    target_sources(slc_platform_iface PRIVATE src/paths_win.cpp src/credential_win.cpp)
    target_link_libraries(slc_platform_iface PUBLIC shell32 ole32 user32 advapi32)
elseif(APPLE)
    target_sources(slc_platform_iface PRIVATE src/paths_mac.cpp src/credential_mac.cpp)
    # paths_mac uses CFPreferences + CoreGraphics display enumeration.
    target_link_libraries(slc_platform_iface PUBLIC
        "-framework CoreFoundation" "-framework CoreGraphics"
        "-framework Security")
else()
    target_sources(slc_platform_iface PRIVATE src/paths_linux.cpp src/credential_linux.cpp)
endif()

slc_set_warnings(slc_platform_iface)
