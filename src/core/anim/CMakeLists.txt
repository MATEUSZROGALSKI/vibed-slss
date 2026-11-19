add_library(slc_anim STATIC
    src/cell_animators.cpp
    src/collage_choreographer.cpp
    src/ken_burns_animator.cpp
    src/matrix_choreographer.cpp
    src/overscan.cpp
)
add_library(slc::anim ALIAS slc_anim)

target_include_directories(slc_anim PUBLIC include)
target_link_libraries(slc_anim PUBLIC cinder cinder_headers)
slc_set_warnings(slc_anim)
