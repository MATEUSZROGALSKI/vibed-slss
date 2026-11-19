add_executable(slc_wallpaperd
    main.cpp
)

target_link_libraries(slc_wallpaperd PRIVATE
    slc::appconfig
    slc::common
    slc::content
    slc::wallpaper
)
slc_set_warnings(slc_wallpaperd)

install(TARGETS slc_wallpaperd RUNTIME DESTINATION bin)
