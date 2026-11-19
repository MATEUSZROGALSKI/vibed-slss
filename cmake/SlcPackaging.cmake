# CPack packaging (doc 8.4): .deb/.AppImage-staging on Linux, NSIS/WiX MSI
# on Windows, DragNDrop staging for the .saver on macOS. Release signing is
# handled in release.yml, not here — packages built on PRs stay unsigned.

set(CPACK_PACKAGE_NAME "SteamLibraryScreensaver")
set(CPACK_PACKAGE_VENDOR "SteamLibraryScreensaver contributors")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
    "Animated Steam-library collage screensaver")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/cognition/steam-library-screensaver")
set(CPACK_PACKAGE_CONTACT "dev@example.invalid")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/packages")
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
endif()

if(WIN32)
    # NSIS64 produces the .exe installer; WiX MSI is available via
    # `cpack -G WIX` when the WiX toolset is installed.
    set(CPACK_GENERATOR "NSIS64")
    set(CPACK_NSIS_DISPLAY_NAME "Steam Library Screensaver")
    set(CPACK_NSIS_PACKAGE_NAME "Steam Library Screensaver")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "SteamLibraryScreensaver")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_VOLUME_NAME "SteamLibraryScreensaver")
elseif(UNIX)
    set(CPACK_GENERATOR "DEB;TGZ")
    set(CPACK_DEBIAN_PACKAGE_MAINTAINER
        "SteamLibraryScreensaver contributors")
    set(CPACK_DEBIAN_PACKAGE_SECTION "x11")
    set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
    set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
    set(CPACK_DEBIAN_PACKAGE_DEPENDS
        "libgl1, libx11-6, libqt6core6 (>= 6.5), libqt6dbus6 (>= 6.5)")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst")
        set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
            "${CMAKE_CURRENT_SOURCE_DIR}/packaging/linux/postinst")
    endif()
endif()

include(CPack)
