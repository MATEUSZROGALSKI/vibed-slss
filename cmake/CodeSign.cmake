# Code signing helpers (doc 8.x). Signing material comes from CI secrets
# only — never from the repo.
#
#   macOS   — codesign --options runtime + entitlements on the .saver when
#             SLC_CODESIGN_IDENTITY is set (e.g. "Developer ID Application:
#             Org (TEAMID)"). Notarization runs in release.yml on the
#             packaged .dmg/.zip, not on the build target.
#   Windows — signtool /Authenticode on the .scr/.exe when
#             SLC_SIGNTOOL_THUMBPRINT or SLC_SIGNTOOL_PFX is set.
function(slc_codesign target)
    if(APPLE)
        if(NOT DEFINED SLC_CODESIGN_IDENTITY OR SLC_CODESIGN_IDENTITY STREQUAL "")
            message(STATUS "slc_codesign(${target}): no SLC_CODESIGN_IDENTITY — unsigned build")
            return()
        endif()
        set(_entitlements
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../src/platform/mac/screensaver.entitlements")
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND codesign --force --deep --options runtime
                    --entitlements "${_entitlements}"
                    --sign "${SLC_CODESIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:${target}>"
            VERBATIM
            COMMENT "Signing $<TARGET_BUNDLE_DIR:${target}> (${SLC_CODESIGN_IDENTITY})")
    elseif(WIN32)
        if(NOT DEFINED SLC_SIGNTOOL_THUMBPRINT AND NOT DEFINED SLC_SIGNTOOL_PFX)
            message(STATUS "slc_codesign(${target}): no signing material — unsigned build")
            return()
        endif()
        find_program(SLC_SIGNTOOL signtool)
        if(NOT SLC_SIGNTOOL)
            message(WARNING "slc_codesign(${target}): signtool not found — unsigned build")
            return()
        endif()
        set(_args sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256)
        if(DEFINED SLC_SIGNTOOL_THUMBPRINT)
            list(APPEND _args /sha1 "${SLC_SIGNTOOL_THUMBPRINT}")
        else()
            list(APPEND _args /f "${SLC_SIGNTOOL_PFX}")
            if(DEFINED SLC_SIGNTOOL_PFX_PASSWORD)
                list(APPEND _args /p "${SLC_SIGNTOOL_PFX_PASSWORD}")
            endif()
        endif()
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND "${SLC_SIGNTOOL}" ${_args} "$<TARGET_FILE:${target}>"
            VERBATIM
            COMMENT "Authenticode-signing $<TARGET_FILE:${target}>")
    else()
        message(STATUS "slc_codesign(${target}): no signing on this platform")
    endif()
endfunction()
