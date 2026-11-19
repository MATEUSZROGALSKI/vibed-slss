add_library(slc_layout STATIC
    src/bsp_strategy.cpp
    src/grid_strategy.cpp
    src/layout_engine.cpp
    src/masonry_strategy.cpp
    src/mosaic_strategy.cpp
    src/rows_strategy.cpp
    src/spiral_strategy.cpp
    src/spotlight_strategy.cpp
)
add_library(slc::layout ALIAS slc_layout)

target_include_directories(slc_layout PUBLIC include)
target_link_libraries(slc_layout PUBLIC cinder_geom)
slc_set_warnings(slc_layout)
