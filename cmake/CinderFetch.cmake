# Cinder integration.
#
# Two levels of availability:
#   * cinder_headers (INTERFACE) — Cinder's include tree, enough for the
#     header-only geometry types (ci::Rectf, ci::vec2) used by slc_layout.
#   * SLC_BUILD_CINDER — compiles full libcinder so slc_anim / slc_render /
#     slc_app can link ci::Timeline, ci::gl::*, ci::app::AppScreenSaver.
#
# The full libcinder build is wired for M0/M1; it currently requires platform
# project files upstream and is therefore opt-in.

include(FetchContent)

set(SLC_CINDER_GIT_TAG "v0.9.3" CACHE STRING "Pinned Cinder tag")

if(SLC_FETCH_CINDER OR SLC_BUILD_CINDER)
    FetchContent_Declare(cinder
        GIT_REPOSITORY https://github.com/cinder/Cinder.git
        GIT_TAG ${SLC_CINDER_GIT_TAG}
        GIT_SHALLOW TRUE
        # Fetch the source tree only; when SLC_BUILD_CINDER is on we invoke
        # add_subdirectory ourselves below so options can be set first.
        SOURCE_SUBDIR "__no_such_dir__"
    )
    FetchContent_MakeAvailable(cinder)

    if(EXISTS "${cinder_SOURCE_DIR}/include/cinder/Cinder.h")
        add_library(cinder_headers INTERFACE)
        # SYSTEM include: Cinder/glm headers trigger MSVC warnings we don't own.
        target_include_directories(cinder_headers SYSTEM INTERFACE
            "${cinder_SOURCE_DIR}/include")
        # Cinder's math headers use glm GTX extensions.
        target_compile_definitions(cinder_headers INTERFACE GLM_ENABLE_EXPERIMENTAL)
        # ci::Rectf's non-inline members live in src/cinder/Rect.cpp; it is
        # dependency-free, so layout gets a real (not header-only) geometry lib.
        add_library(cinder_geom STATIC
            "${cinder_SOURCE_DIR}/src/cinder/Area.cpp"
            "${cinder_SOURCE_DIR}/src/cinder/Rect.cpp")
        target_link_libraries(cinder_geom PUBLIC cinder_headers)
        if(NOT EXISTS "${cinder_SOURCE_DIR}/include/glm/glm.hpp")
            find_package(glm CONFIG REQUIRED)
            target_link_libraries(cinder_headers INTERFACE glm::glm)
        endif()
        message(STATUS "Cinder headers: ${cinder_SOURCE_DIR}/include")
    else()
        message(WARNING "Cinder sources fetched but include/cinder/Cinder.h not found")
    endif()
endif()

if(SLC_BUILD_CINDER)
    if(NOT DEFINED cinder_SOURCE_DIR)
        message(FATAL_ERROR "SLC_BUILD_CINDER requires SLC_FETCH_CINDER")
    endif()

    # Cinder's own proj/cmake build. Audio/video are spec non-goals (doc 5.1);
    # disabling them skips WASAPI/MediaFoundation/vorbis/r8brain entirely.
    set(CINDER_DISABLE_AUDIO ON CACHE BOOL "" FORCE)
    set(CINDER_DISABLE_VIDEO ON CACHE BOOL "" FORCE)

    # libcinder is C++17-era code; building it at our global C++23 breaks under
    # the current STL. Its target also bakes cxx_std_<n> PUBLIC, but that is a
    # minimum — our modules still compile at C++23.
    set(_slc_saved_cxx_std ${CMAKE_CXX_STANDARD})
    set(CMAKE_CXX_STANDARD 17)

    # Match vcpkg's dynamic CRT (/MD) instead of Cinder's static-/MT default.
    if(MSVC AND NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
        set(CMAKE_MSVC_RUNTIME_LIBRARY
            "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
    endif()

    add_subdirectory(${cinder_SOURCE_DIR} ${cinder_BINARY_DIR}/cinder-subbuild)
    set(CMAKE_CXX_STANDARD ${_slc_saved_cxx_std})

    if(NOT TARGET cinder)
        message(FATAL_ERROR "Cinder subdirectory did not produce a 'cinder' target")
    endif()

    # On Linux Cinder compiles Capture.cpp unconditionally, and it
    # hard-includes gst/gst.h — with CINDER_DISABLE_VIDEO that dependency
    # should not exist. Nothing else in libcinder references ci::Capture, so
    # drop the TU rather than require GStreamer dev headers.
    if(CINDER_DISABLE_VIDEO AND UNIX AND NOT APPLE)
        get_target_property(_cinder_srcs cinder SOURCES)
        list(REMOVE_ITEM _cinder_srcs
             "${cinder_SOURCE_DIR}/src/cinder/Capture.cpp")
        set_target_properties(cinder PROPERTIES SOURCES "${_cinder_srcs}")
    endif()

    # Cinder's MSW cmake embeds its whole platform lib list into cinder.lib via
    # STATIC_LIBRARY_FLAGS (VS-style "merge into the archive" trick). That
    # forces every consumer to load wmvcore/Strmiids (Media Foundation +
    # DirectShow) at process start — heavy DLL stacks we never use, and their
    # first-load init can stall for minutes. Clear the flags and link only
    # what our code path actually touches.
    if(WIN32 AND TARGET cinder)
        set_target_properties(cinder PROPERTIES
            STATIC_LIBRARY_FLAGS_DEBUG ""
            STATIC_LIBRARY_FLAGS_RELEASE ""
            STATIC_LIBRARY_FLAGS_MINSIZEREL ""
            STATIC_LIBRARY_FLAGS_RELWITHDEBINFO "")
        target_link_libraries(cinder PUBLIC
            Ws2_32 shlwapi OpenGL32 Msimg32 wininet
            "${cinder_SOURCE_DIR}/lib/msw/x64/libpng.lib")
        # Unicode is already a PUBLIC define on the cinder target.
    endif()
endif()
