add_executable(slc_host_win WIN32 win_main.cpp control_pipe.cpp)

target_link_libraries(slc_host_win PRIVATE
    slc::appconfig
    slc::common
    slc::content
    slc::platform_iface
)

if(SLC_BUILD_CINDER)
    target_sources(slc_host_win PRIVATE win_cinder.cpp control_server.cpp)
    target_link_libraries(slc_host_win PRIVATE slc::app cinder)
else()
    target_sources(slc_host_win PRIVATE win_cinder_stub.cpp)
endif()
slc_set_warnings(slc_host_win)

# The shipped artifact is the same binary renamed with a .scr extension
# (doc section 6.1.1). GUI subsystem is already set via WIN32.
add_custom_command(TARGET slc_host_win POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:slc_host_win>
            $<TARGET_FILE_DIR:slc_host_win>/SteamLibraryScreensaver.scr
)

# Authenticode via CI secrets when configured (cmake/CodeSign.cmake);
# unsigned otherwise. The .scr copy is produced before this hook runs, so
# sign the .scr as well — it is the shipped artifact.
include(CodeSign)
slc_codesign(slc_host_win)

# The .scr is the shipped artifact (doc 6.1.1); slc_host_win.exe stays for
# command-line diagnostics (--list-artwork) and as the configurator host.
install(FILES
        "$<TARGET_FILE_DIR:slc_host_win>/SteamLibraryScreensaver.scr"
        DESTINATION bin)
install(TARGETS slc_host_win RUNTIME DESTINATION bin)
