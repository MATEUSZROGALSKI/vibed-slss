add_library(slc_enrichment STATIC
    transport_factory.cpp
)
add_library(slc::enrichment ALIAS slc_enrichment)

target_include_directories(slc_enrichment PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(slc_enrichment PUBLIC slc::content)
slc_set_warnings(slc_enrichment)

if(SLC_WITH_CPR)
    find_package(cpr CONFIG REQUIRED)
    target_sources(slc_enrichment PRIVATE cpr_transport.cpp)
    target_compile_definitions(slc_enrichment PRIVATE SLC_WITH_CPR)
    target_link_libraries(slc_enrichment PRIVATE cpr::cpr)
endif()
