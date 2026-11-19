add_library(slc_cache STATIC
    src/artwork_cache.cpp
)
add_library(slc::cache ALIAS slc_cache)

target_include_directories(slc_cache PUBLIC include)
target_link_libraries(slc_cache PUBLIC slc::common nlohmann_json::nlohmann_json)
slc_set_warnings(slc_cache)
