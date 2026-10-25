cmake_minimum_required(VERSION 3.28)

project(MWPerfFix VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

option(MWPERFFIX_DIAGNOSTICS "Build the removable diagnostic instrumentation variant" OFF)

set(MWPERFFIX_DISPLAY_NAME "MW PerfFix")
if(MWPERFFIX_DIAGNOSTICS)
    set(MWPERFFIX_VARIANT "diagnostic")
else()
    set(MWPERFFIX_VARIANT "normal")
endif()
set(MWPERFFIX_GENERATED_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/version.txt" CONTENT "${PROJECT_VERSION}\n")
add_custom_target(mwperffix_build_info
    COMMAND "${CMAKE_COMMAND}"
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DOUTPUT_DIR=${MWPERFFIX_GENERATED_INCLUDE_DIR}"
        "-DPROJECT_VERSION=${PROJECT_VERSION}"
        "-DPROJECT_NAME=${PROJECT_NAME}"
        "-DMWPERFFIX_DISPLAY_NAME=${MWPERFFIX_DISPLAY_NAME}"
        "-DMWPERFFIX_VARIANT=${MWPERFFIX_VARIANT}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/build_info.cmake"
    BYPRODUCTS "${MWPERFFIX_GENERATED_INCLUDE_DIR}/mwperffix/build_info.hpp"
    VERBATIM
)

add_library(mwperffix_patch STATIC
    src/patch.cpp
    src/render_fixes.cpp
)
target_include_directories(mwperffix_patch PUBLIC include)
target_compile_features(mwperffix_patch PUBLIC cxx_std_23)

add_library(mwperffix_adaptive_controller STATIC
    src/adaptive_reflections_controller.cpp
)
target_include_directories(mwperffix_adaptive_controller PUBLIC include)
target_compile_features(mwperffix_adaptive_controller PUBLIC cxx_std_23)
if(MWPERFFIX_DIAGNOSTICS)
    target_sources(mwperffix_adaptive_controller PRIVATE diagnostics/src/controller_observations.cpp)
    target_compile_definitions(mwperffix_adaptive_controller PUBLIC MWPERFFIX_DIAGNOSTICS=1)
endif()

add_library(${PROJECT_NAME} MODULE
    src/dllmain.cpp
    src/runtime.cpp
    src/adaptive_reflections_runtime.cpp
    src/screen_messages.cpp
    src/MWPerfFix.def
)
target_include_directories(${PROJECT_NAME} PRIVATE "${MWPERFFIX_GENERATED_INCLUDE_DIR}")
add_dependencies(${PROJECT_NAME} mwperffix_build_info)
target_link_libraries(${PROJECT_NAME} PRIVATE mwperffix_patch mwperffix_adaptive_controller)
target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_23)
set_target_properties(${PROJECT_NAME} PROPERTIES
    PREFIX ""
    SUFFIX ".asi"
)

if(MWPERFFIX_DIAGNOSTICS)
    add_subdirectory(diagnostics)
    mwperffix_enable_diagnostics(${PROJECT_NAME})
endif()


configure_file(
    "${PROJECT_NAME}.ini"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.ini"
    COPYONLY
)

if(BUILD_TESTING)
    add_executable(mwperffix_tests tests/tests.cpp)
    target_link_libraries(mwperffix_tests PRIVATE mwperffix_patch)
    target_compile_features(mwperffix_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_tests COMMAND mwperffix_tests)

    add_executable(mwperffix_adaptive_controller_tests tests/adaptive-reflections-tests.cpp)
    target_link_libraries(mwperffix_adaptive_controller_tests PRIVATE mwperffix_adaptive_controller)
    target_compile_features(mwperffix_adaptive_controller_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_adaptive_controller_tests COMMAND mwperffix_adaptive_controller_tests)

    add_executable(mwperffix_adaptive_runtime_tests
        tests/adaptive-reflections-runtime-tests.cpp
        tests/machine-state.cpp
        src/adaptive_reflections_runtime.cpp
        src/screen_messages.cpp
    )
    # The adapter is compiled separately with deterministic native-call substitutes.
    target_include_directories(mwperffix_adaptive_runtime_tests PRIVATE src)
    target_compile_definitions(mwperffix_adaptive_runtime_tests PRIVATE MWPERFFIX_RUNTIME_TEST=1)
    target_link_libraries(mwperffix_adaptive_runtime_tests PRIVATE
        mwperffix_patch mwperffix_adaptive_controller)
    if(MWPERFFIX_DIAGNOSTICS)
        target_link_libraries(mwperffix_adaptive_runtime_tests PRIVATE mwperffix_diagnostics_runtime)
    endif()
    target_compile_features(mwperffix_adaptive_runtime_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_adaptive_runtime_tests COMMAND mwperffix_adaptive_runtime_tests)

    add_executable(mwperffix_reflection_blending_tests tests/reflection-blending-tests.cpp)
    target_include_directories(mwperffix_reflection_blending_tests PRIVATE include)
    target_compile_features(mwperffix_reflection_blending_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_reflection_blending_tests COMMAND mwperffix_reflection_blending_tests)

    add_executable(mwperffix_runtime_hotkey_tests tests/runtime-hotkeys-tests.cpp)
    target_include_directories(mwperffix_runtime_hotkey_tests PRIVATE include)
    target_compile_features(mwperffix_runtime_hotkey_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_runtime_hotkey_tests COMMAND mwperffix_runtime_hotkey_tests)

    add_executable(mwperffix_runtime_notification_tests tests/runtime-notifications-tests.cpp)
    target_include_directories(mwperffix_runtime_notification_tests PRIVATE include)
    target_compile_features(mwperffix_runtime_notification_tests PRIVATE cxx_std_23)
    add_test(NAME mwperffix_runtime_notification_tests COMMAND mwperffix_runtime_notification_tests)

    include(cmake/check_headers.cmake)
endif()

# Windows headers must see one consistent policy before any include, including
# separately compiled tests and diagnostic code.
add_library(mwperffix_windows INTERFACE)
target_compile_definitions(mwperffix_windows INTERFACE WIN32_LEAN_AND_MEAN NOMINMAX)
set(owned_targets mwperffix_patch mwperffix_adaptive_controller ${PROJECT_NAME})
if(BUILD_TESTING)
    list(APPEND owned_targets mwperffix_tests mwperffix_adaptive_controller_tests
        mwperffix_adaptive_runtime_tests mwperffix_reflection_blending_tests
        mwperffix_runtime_hotkey_tests mwperffix_runtime_notification_tests
        mwperffix_header_checks)
endif()
foreach(target IN LISTS owned_targets)
    target_link_libraries(${target} PRIVATE mwperffix_windows)
endforeach()
if(MWPERFFIX_DIAGNOSTICS)
    target_link_libraries(mwperffix_diagnostics_etw PRIVATE mwperffix_windows)
    target_link_libraries(mwperffix_diagnostics_runtime PRIVATE mwperffix_windows)
    if(BUILD_TESTING)
        target_link_libraries(mwperffix_etw_emitter PRIVATE mwperffix_windows)
    endif()
endif()

# Every owned target shares the same warning policy. Static libraries have no link step.
if(MINGW)
    foreach(target IN LISTS owned_targets)
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -Wshadow)
        get_target_property(kind ${target} TYPE)
        if(kind STREQUAL "MODULE_LIBRARY" OR kind STREQUAL "EXECUTABLE")
            target_link_options(${target} PRIVATE -static -static-libgcc -static-libstdc++)
        endif()
    endforeach()
    target_link_options(${PROJECT_NAME} PRIVATE
        "-Wl,--pdb=${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pdb")
endif()
