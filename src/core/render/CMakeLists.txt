add_library(slc_render STATIC
    src/texture_pool.cpp
)
add_library(slc::render ALIAS slc_render)

target_include_directories(slc_render PUBLIC include)
target_link_libraries(slc_render PUBLIC slc::anim cinder cinder_headers)
slc_set_warnings(slc_render)
