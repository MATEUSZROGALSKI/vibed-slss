add_library(slc_app STATIC
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
slc_set_warnings(slc_app)
