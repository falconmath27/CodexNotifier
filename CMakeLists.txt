cmake_minimum_required(VERSION 3.24)

project(
    DimePenny
    VERSION 0.1.0
    DESCRIPTION "A Windows desktop companion for Codex sessions"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

option(DIMEPENNY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(Qt6 REQUIRED COMPONENTS Core Network Sql Widgets)

function(dimepenny_enable_warnings target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
        if(DIMEPENNY_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
        if(DIMEPENNY_WARNINGS_AS_ERRORS)
            target_compile_options(${target_name} PRIVATE -Werror)
        endif()
    endif()
endfunction()

configure_file(
    "${PROJECT_SOURCE_DIR}/cmake/build_config.h.in"
    "${PROJECT_BINARY_DIR}/generated/dimepenny/core/build_config.h"
    @ONLY
)

add_subdirectory(src)
add_subdirectory(app)
add_subdirectory(bridge)

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

