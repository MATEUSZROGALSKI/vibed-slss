find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

add_executable(slc_unit_tests
    unit/test_appinfo_names.cpp
    unit/test_artwork_cache.cpp
    unit/test_artwork_pick.cpp
    unit/test_config.cpp
    unit/test_hltb.cpp
    unit/test_image_quality.cpp
    unit/test_pick_queue.cpp
    unit/test_sgdb.cpp
    unit/test_steam_local_provider.cpp
    unit/test_steam_locator.cpp
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

# Windows host contract test (doc 9.3): real parent HWND + `/p` child.
if(WIN32 AND TARGET slc_host_win)
    add_executable(slc_preview_harness host_win/preview_parent.cpp)
    add_test(NAME WinPreviewContract
             COMMAND slc_preview_harness $<TARGET_FILE:slc_host_win>)
    set_tests_properties(WinPreviewContract PROPERTIES TIMEOUT 90)
endif()
