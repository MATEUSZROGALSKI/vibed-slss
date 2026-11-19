cmake_minimum_required(VERSION 3.24)

project(steam-library-screensaver
    VERSION 0.1.0
    DESCRIPTION "Animated Steam-library collage screensaver"
    LANGUAGES CXX
)

if(APPLE)
    # The .saver principal class is Objective-C++.
    enable_language(OBJCXX)
endif()

# Doc mandates C++20 language features but also std::expected (section 7.4),
# which is a C++23 library facility on all three STL implementations.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(SLC_BUILD_TESTS "Build unit/integration tests" ON)
option(SLC_BUILD_HOSTS "Build platform host executables" ON)
option(SLC_FETCH_CINDER "Fetch Cinder sources (header-only targets)" ON)
option(SLC_BUILD_CINDER "Compile libcinder + GL-dependent modules" OFF)
option(SLC_BUILD_QT_TOOLS "Build Qt configurator / linux host" OFF)
option(SLC_WITH_CPR "Compile cpr-backed HTTP transport for SteamGridDB enrichment" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(SlcWarnings)
include(CinderFetch)

find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

add_subdirectory(src/core/platform_iface)
add_subdirectory(src/core/common)
add_subdirectory(src/core/content)
add_subdirectory(src/core/cache)
add_subdirectory(src/core/config)
add_subdirectory(src/enrichment)

if(TARGET cinder_headers)
    add_subdirectory(src/core/layout)
else()
    message(STATUS "Cinder headers unavailable; skipping slc_layout (set -DSLC_FETCH_CINDER=ON)")
endif()

if(SLC_BUILD_CINDER)
    add_subdirectory(src/core/anim)
    add_subdirectory(src/core/render)
    add_subdirectory(src/core/app)
else()
    message(STATUS "SLC_BUILD_CINDER=OFF; anim/render/app modules are source stubs only")
endif()

if(WIN32 AND SLC_BUILD_HOSTS)
    add_subdirectory(src/platform/win)
endif()
if(APPLE AND SLC_BUILD_HOSTS)
    add_subdirectory(src/platform/mac)
endif()
if(UNIX AND NOT APPLE AND SLC_BUILD_HOSTS AND SLC_BUILD_QT_TOOLS)
    add_subdirectory(src/platform/linux)
endif()

add_subdirectory(src/platform/wallpaper)
if(SLC_BUILD_HOSTS)
    add_subdirectory(src/tools/wallpaperd)
endif()
if(SLC_BUILD_QT_TOOLS)
    add_subdirectory(src/tools/configurator)
    add_subdirectory(src/tools/tray)
endif()

if(SLC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# CPack must be included last so every target/install rule already exists.
if(SLC_BUILD_HOSTS)
    include(SlcPackaging)
endif()
