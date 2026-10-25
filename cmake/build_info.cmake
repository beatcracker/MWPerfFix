set(MWPERFFIX_REVISION "unknown")
find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" rev-parse --short=12 HEAD
        OUTPUT_VARIABLE revision OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE revision_result
        ERROR_QUIET
    )
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" status --porcelain --untracked-files=no
        OUTPUT_VARIABLE tracked_changes OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE status_result
        ERROR_QUIET
    )
    if(revision_result STREQUAL "0" AND status_result STREQUAL "0" AND NOT revision STREQUAL "")
        set(MWPERFFIX_REVISION "${revision}")
        if(tracked_changes)
            string(APPEND MWPERFFIX_REVISION "-dirty")
        endif()
    endif()
endif()
configure_file("${SOURCE_DIR}/cmake/build_info.hpp.in"
    "${OUTPUT_DIR}/mwperffix/build_info.hpp" @ONLY)
