# Doc section 6.1.2 — produces SteamLibraryScreensaver.saver:
#   Contents/Info.plist   (NSPrincipalClass = SLCScreenSaverView)
#   Contents/MacOS/SteamLibraryScreensaver
#   Contents/Resources/
#
# The .saver is a BNDL loaded by legacyScreenSaver / ScreenSaverEngine —
# never launched directly, so this is a MACOSX_BUNDLE, not an app.

# Core-only mac builds (ci-mac without SLC_BUILD_CINDER) still configure —
# the .saver target simply isn't defined.
if(NOT SLC_BUILD_CINDER)
    message(STATUS "SLC_BUILD_CINDER=OFF; skipping .saver host")
    return()
endif()

add_executable(slc_host_mac MACOSX_BUNDLE SLCScreenSaverView.mm)

set_target_properties(slc_host_mac PROPERTIES
    OUTPUT_NAME SteamLibraryScreensaver
    BUNDLE_EXTENSION saver
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_BUNDLE_NAME "SteamLibraryScreensaver"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.steamlibraryscreensaver.saver"
    MACOSX_BUNDLE_EXECUTABLE_NAME "SteamLibraryScreensaver"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
)

target_link_libraries(slc_host_mac PRIVATE
    slc::app
    slc::platform_iface
    cinder
    "-framework ScreenSaver"
    "-framework Cocoa"
)

slc_set_warnings(slc_host_mac)

include(BundleMacSaver)
slc_bundle_mac_saver(slc_host_mac)

install(TARGETS slc_host_mac BUNDLE DESTINATION .)
