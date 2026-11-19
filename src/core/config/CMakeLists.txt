add_library(slc_appconfig STATIC
    src/app_config.cpp
)
add_library(slc::appconfig ALIAS slc_appconfig)

target_include_directories(slc_appconfig PUBLIC include)
target_link_libraries(slc_appconfig PUBLIC slc::common slc::platform_iface
                                           nlohmann_json::nlohmann_json)
slc_set_warnings(slc_appconfig)
