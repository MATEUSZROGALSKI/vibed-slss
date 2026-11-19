add_library(slc_common STATIC
    src/env.cpp
    src/error.cpp
    src/logging.cpp
)
add_library(slc::common ALIAS slc_common)

target_include_directories(slc_common PUBLIC include)
target_link_libraries(slc_common PUBLIC slc::platform_iface spdlog::spdlog)
slc_set_warnings(slc_common)
