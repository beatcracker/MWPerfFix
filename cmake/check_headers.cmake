# Compile each owned header first and alone. Generated TUs stay in the build
# directory; no executable or game files are needed for these checks.
file(GLOB_RECURSE checked_headers CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/*.hpp"
    "${PROJECT_SOURCE_DIR}/src/*.hpp"
    "${PROJECT_SOURCE_DIR}/tests/*.hpp"
)
list(APPEND checked_headers "${MWPERFFIX_GENERATED_INCLUDE_DIR}/mwperffix/build_info.hpp")
if(MWPERFFIX_DIAGNOSTICS)
    file(GLOB_RECURSE diagnostic_headers CONFIGURE_DEPENDS
        "${PROJECT_SOURCE_DIR}/diagnostics/include/*.hpp")
    list(APPEND checked_headers ${diagnostic_headers}
        "${PROJECT_BINARY_DIR}/diagnostics/generated/mwperffix_tracelogging.hpp")
endif()

set(header_sources)
foreach(header IN LISTS checked_headers)
    file(RELATIVE_PATH relative_header "${PROJECT_SOURCE_DIR}" "${header}")
    string(MAKE_C_IDENTIFIER "${relative_header}" header_id)
    set(source "${PROJECT_BINARY_DIR}/header-checks/${header_id}.cpp")
    if(header MATCHES "mwperffix_tracelogging.hpp$")
        # Include through the same SYSTEM path as capture.cpp so SDK internals
        # are not checked as owned code.
        file(GENERATE OUTPUT "${source}" CONTENT "#include <mwperffix_tracelogging.hpp>\n")
        set_source_files_properties("${source}" PROPERTIES COMPILE_OPTIONS -fms-extensions)
    else()
        file(GENERATE OUTPUT "${source}" CONTENT "#include \"${header}\"\n")
    endif()
    list(APPEND header_sources "${source}")
endforeach()

# Check both orders at the Windows/D3D boundary as well as standalone inclusion.
foreach(header include/mwperffix/relay.hpp src/reflection_publication.hpp
        src/adaptive_reflections_runtime_private.hpp src/screen_messages.hpp)
    string(MAKE_C_IDENTIFIER "${header}" header_id)
    foreach(order sdk_first header_first)
        set(owned_include "#include \"${PROJECT_SOURCE_DIR}/${header}\"\n")
        set(sdk_include "#include <windows.h>\n#include <d3d9.h>\n")
        if(order STREQUAL "sdk_first")
            set(content "${sdk_include}${owned_include}")
        else()
            set(content "${owned_include}${sdk_include}")
        endif()
        set(source "${PROJECT_BINARY_DIR}/header-checks/${header_id}_${order}.cpp")
        file(GENERATE OUTPUT "${source}" CONTENT "${content}")
        list(APPEND header_sources "${source}")
    endforeach()
endforeach()

add_library(mwperffix_header_checks OBJECT ${header_sources})
add_dependencies(mwperffix_header_checks mwperffix_build_info)
target_compile_features(mwperffix_header_checks PRIVATE cxx_std_23)
target_include_directories(mwperffix_header_checks PRIVATE
    "${PROJECT_SOURCE_DIR}/include"
    "${MWPERFFIX_GENERATED_INCLUDE_DIR}")
if(MWPERFFIX_DIAGNOSTICS)
    target_compile_definitions(mwperffix_header_checks PRIVATE MWPERFFIX_DIAGNOSTICS=1)
    target_include_directories(mwperffix_header_checks PRIVATE
        "${PROJECT_SOURCE_DIR}/diagnostics/include")
    target_include_directories(mwperffix_header_checks SYSTEM PRIVATE
        "${PROJECT_BINARY_DIR}/diagnostics/generated")
endif()
