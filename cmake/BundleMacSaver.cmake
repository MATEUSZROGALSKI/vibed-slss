# Doc section 6.1.2 — .saver bundle post-processing.
#
# The bundle layout itself is produced by MACOSX_BUNDLE +
# BUNDLE_EXTENSION=saver + MACOSX_BUNDLE_INFO_PLIST on the target; this
# function attaches the entitlements and the Developer-ID signing hook
# (slc_codesign, no-op unless SLC_CODESIGN_IDENTITY is set — see
# cmake/CodeSign.cmake).
include(CodeSign)

function(slc_bundle_mac_saver target)
    if(NOT APPLE)
        message(FATAL_ERROR "slc_bundle_mac_saver is only valid on macOS")
    endif()

    set_target_properties(${target} PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/mac/screensaver.entitlements"
    )

    slc_codesign(${target})
endfunction()
