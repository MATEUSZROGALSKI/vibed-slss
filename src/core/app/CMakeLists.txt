add_library(slc_app STATIC
    src/debug_ui.cpp
    src/pick_queue.cpp
    src/screensaver_app.cpp
    src/screensaver_app_draw.cpp
    src/screensaver_app_pipeline.cpp
    src/screensaver_app_scenes.cpp
)
add_library(slc::app ALIAS slc_app)

target_include_directories(slc_app PUBLIC include)
target_link_libraries(slc_app PUBLIC slc::appconfig slc::cache slc::content
                                     slc::enrichment slc::layout slc::render
                                     cinder cinder_headers)

if(SLC_WITH_IMGUI)
    find_package(imgui CONFIG REQUIRED)
    target_link_libraries(slc_app PUBLIC imgui::imgui)
    target_compile_definitions(slc_app PUBLIC SLC_WITH_IMGUI=1)
endif()

slc_set_warnings(slc_app)
