set(TRACELOGGING_HEADER "" CACHE FILEPATH "Path to Windows SDK TraceLoggingProvider.h")
if(NOT TRACELOGGING_HEADER)
    get_filename_component(sdk_root
        "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
        ABSOLUTE
    )
    file(GLOB tracelogging_candidates "${sdk_root}/Include/*/shared/TraceLoggingProvider.h")
    list(SORT tracelogging_candidates COMPARE NATURAL ORDER DESCENDING)
    if(NOT tracelogging_candidates)
        message(FATAL_ERROR "Install Windows SDK headers or set TRACELOGGING_HEADER")
    endif()
    list(GET tracelogging_candidates 0 TRACELOGGING_HEADER)
endif()
file(TO_CMAKE_PATH "${TRACELOGGING_HEADER}" TRACELOGGING_HEADER)
configure_file(
    tracelogging.hpp.in
    "${CMAKE_CURRENT_BINARY_DIR}/generated/mwperffix_tracelogging.hpp"
    @ONLY
)

add_library(mwperffix_diagnostics_etw STATIC src/capture.cpp)
target_include_directories(mwperffix_diagnostics_etw PUBLIC
    include
    "${CMAKE_CURRENT_SOURCE_DIR}/../include"
)
target_include_directories(mwperffix_diagnostics_etw SYSTEM PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_compile_features(mwperffix_diagnostics_etw PUBLIC cxx_std_23)
target_compile_definitions(mwperffix_diagnostics_etw PUBLIC MWPERFFIX_DIAGNOSTICS=1)
target_link_libraries(mwperffix_diagnostics_etw PUBLIC advapi32)

add_library(mwperffix_diagnostics_runtime STATIC
    src/runtime.cpp
)
add_dependencies(mwperffix_diagnostics_runtime mwperffix_build_info)
target_include_directories(mwperffix_diagnostics_runtime
    PUBLIC include
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/../include"
        "${MWPERFFIX_GENERATED_INCLUDE_DIR}"
)
target_compile_features(mwperffix_diagnostics_runtime PUBLIC cxx_std_23)
target_link_libraries(mwperffix_diagnostics_runtime PUBLIC mwperffix_diagnostics_etw)

if(MINGW)
    target_compile_options(mwperffix_diagnostics_runtime PRIVATE
        -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow
    )
    target_compile_options(mwperffix_diagnostics_etw PRIVATE
        -fms-extensions -Wall -Wextra -Werror -Wconversion -Wshadow
    )
endif()

function(mwperffix_enable_diagnostics target)
    target_compile_definitions(${target} PRIVATE MWPERFFIX_DIAGNOSTICS=1)
    target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_link_libraries(${target} PRIVATE mwperffix_diagnostics_runtime)
endfunction()

if(BUILD_TESTING)
    add_executable(mwperffix_etw_emitter tests/etw_emitter.cpp)
    target_link_libraries(mwperffix_etw_emitter PRIVATE mwperffix_diagnostics_etw)
    target_compile_features(mwperffix_etw_emitter PRIVATE cxx_std_23)

    if(MINGW)
        foreach(target mwperffix_etw_emitter)
            target_compile_options(${target} PRIVATE
                -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow
            )
            target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        endforeach()
    endif()

    find_program(POWERSHELL_EXECUTABLE powershell REQUIRED)
    add_test(
        NAME mwperffix_diagnostics_validator_tests
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/validator-tests.ps1"
            -Validator "${CMAKE_CURRENT_SOURCE_DIR}/tools/validate-etw.ps1"
            -Work "${CMAKE_CURRENT_BINARY_DIR}/validator-tests"
    )
endif()
if(BUILD_TESTING)
    add_test(
        NAME mwperffix_diagnostics_analysis_tests
        COMMAND "${POWERSHELL_EXECUTABLE}" -NoProfile -ExecutionPolicy Bypass
            -File "${CMAKE_CURRENT_SOURCE_DIR}/tests/analysis-tests.ps1"
    )
endif()
