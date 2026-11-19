find_package(nlohmann_json CONFIG REQUIRED)

add_library(slc_content STATIC
    src/appinfo_names.cpp
    src/artwork.cpp
    src/content_service.cpp
    src/hltb.cpp
    src/image_size.cpp
    src/steam_local_provider.cpp
    src/steam_locator.cpp
    src/steamgriddb_provider.cpp
    src/token_bucket.cpp
    src/vdf.cpp
)
add_library(slc::content ALIAS slc_content)

target_include_directories(slc_content PUBLIC include)
target_link_libraries(slc_content PUBLIC
    slc::common
    slc::platform_iface
    slc::cache
    nlohmann_json::nlohmann_json
)
slc_set_warnings(slc_content)
