find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(slc_unit_tests
    unit/test_appinfo_names.cpp
    unit/test_artwork_cache.cpp
    unit/test_artwork_pick.cpp
    unit/test_config.cpp
    unit/test_content_service.cpp
    unit/test_credential.cpp
    unit/test_env.cpp
    unit/test_error.cpp
    unit/test_hltb.cpp
    unit/test_image_quality.cpp
    unit/test_logging.cpp
    unit/test_paths.cpp
    unit/test_pick_queue.cpp
    unit/test_sgdb.cpp
    unit/test_steam_local_provider.cpp
    unit/test_steam_locator.cpp
    unit/test_transport_factory.cpp
    unit/test_vdf.cpp
)

# PickQueue is tested without linking slc_app (which drags Cinder): the
# class is self-contained, so its TU and headers go straight in here.
target_sources(slc_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/app/src/pick_queue.cpp
)
target_include_directories(slc_unit_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/core/app/include
)

target_link_libraries(slc_unit_tests PRIVATE
    slc::appconfig
    slc::cache
    slc::common
    slc::content
    slc::platform_iface
    GTest::gtest
    GTest::gtest_main
)
if(TARGET slc_enrichment)
    target_link_libraries(slc_unit_tests PRIVATE slc::enrichment)
endif()
slc_set_warnings(slc_unit_tests)

target_compile_definitions(slc_unit_tests PRIVATE
    SLC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)

if(TARGET slc_layout)
    target_sources(slc_unit_tests PRIVATE unit/test_layout.cpp)
    target_link_libraries(slc_unit_tests PRIVATE slc::layout)
    target_compile_definitions(slc_unit_tests PRIVATE SLC_TEST_LAYOUT=1)
endif()

if(TARGET slc_anim)
    target_sources(slc_unit_tests PRIVATE
        unit/test_kenburns.cpp
        unit/test_collage_anim.cpp
        unit/test_matrix_choreo.cpp
        unit/test_overscan.cpp
        unit/test_scene_anim.cpp
    )
    target_link_libraries(slc_unit_tests PRIVATE slc::anim)
endif()

gtest_discover_tests(slc_unit_tests)

# doc §9.3 pipeline tests: fixture → content → layout → choreography.
if(TARGET slc_layout AND TARGET slc_anim)
    add_executable(slc_integration_tests integration/test_pipeline.cpp)
    target_link_libraries(slc_integration_tests PRIVATE
        slc::common
        slc::content
        slc::layout
        slc::anim
        GTest::gtest
        GTest::gtest_main
    )
    slc_set_warnings(slc_integration_tests)
    target_compile_definitions(slc_integration_tests PRIVATE
        SLC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )
    gtest_discover_tests(slc_integration_tests)
endif()

# doc §9.5 performance benchmarks (Google Benchmark, run manually:
# build/<preset>/tests/slc_benchmarks[_layout] --benchmark_out=bench.json).
find_package(benchmark CONFIG QUIET)
if(TARGET benchmark::benchmark)
    add_executable(slc_benchmarks bench/bench_parse.cpp)
    target_link_libraries(slc_benchmarks PRIVATE
        slc::appconfig
        slc::common
        slc::content
        benchmark::benchmark
        benchmark::benchmark_main
    )
    slc_set_warnings(slc_benchmarks)
    target_compile_definitions(slc_benchmarks PRIVATE
        SLC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    )

    if(TARGET slc_layout)
        add_executable(slc_benchmarks_layout bench/bench_layout.cpp)
        target_link_libraries(slc_benchmarks_layout PRIVATE
            slc::layout
            benchmark::benchmark
            benchmark::benchmark_main
        )
        slc_set_warnings(slc_benchmarks_layout)
    endif()
endif()

# doc §9.4 visual regression: deterministic seeded collage → PNG,
# compared against tests/visual/golden/. Needs a GL context — under CI
# that means Xvfb+llvmpipe on Linux or the GPU runner elsewhere.
if(TARGET cinder AND TARGET slc_layout)
    add_executable(slc_visual WIN32 visual/visual_harness.cpp)
    target_link_libraries(slc_visual PRIVATE slc::layout cinder)
    slc_set_warnings(slc_visual)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/visual/golden/collage.png)
        add_test(NAME VisualRegression
                 COMMAND slc_visual --compare
                     ${CMAKE_CURRENT_SOURCE_DIR}/visual/golden/collage.png)
        set_tests_properties(VisualRegression PROPERTIES TIMEOUT 60)
    endif()
endif()

# wallpaperd CLI coverage: --once exits cleanly against an empty library,
# unknown args print usage and exit non-zero. HOME is sandboxed so the
# daemon's log/cache dirs stay inside the build tree.
if(TARGET slc_wallpaperd)
    # SLC_STEAM_ROOT points at an empty dir and SLC_CONFIG_PATH at a
    # nonexistent file so discovery yields nothing — the daemon exits
    # before any backend->setWallpaper call on every OS.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/wallpaperd-home)
    set(WPD_ENV "HOME=${CMAKE_CURRENT_BINARY_DIR}/wallpaperd-home;SLC_STEAM_ROOT=${CMAKE_CURRENT_BINARY_DIR}/wallpaperd-home;SLC_CONFIG_PATH=${CMAKE_CURRENT_BINARY_DIR}/nonexistent.json;XDG_CURRENT_DESKTOP=test-none")
    add_test(NAME WallpaperdEmptyLibrary
             COMMAND slc_wallpaperd --once)
    set_tests_properties(WallpaperdEmptyLibrary PROPERTIES
        TIMEOUT 30
        ENVIRONMENT "${WPD_ENV}")
    add_test(NAME WallpaperdUsageError
             COMMAND slc_wallpaperd --bogus)
    set_tests_properties(WallpaperdUsageError PROPERTIES
        WILL_FAIL TRUE
        TIMEOUT 30
        ENVIRONMENT "${WPD_ENV}")
endif()

# Windows host contract test (doc 9.3): real parent HWND + `/p` child.
if(WIN32 AND TARGET slc_host_win)
    add_executable(slc_preview_harness host_win/preview_parent.cpp)
    add_test(NAME WinPreviewContract
             COMMAND slc_preview_harness $<TARGET_FILE:slc_host_win>)
    set_tests_properties(WinPreviewContract PROPERTIES TIMEOUT 90)
endif()
