cmake_minimum_required(VERSION 3.22)
project(hexmcu_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sources.cmake)

# SDL3 vendored, host build only
set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory(${APP_ROOT}/third_party/SDL
                 ${CMAKE_BINARY_DIR}/SDL EXCLUDE_FROM_ALL)

# Everything except main(), compiled once and shared by both programs:
#   hexmcu_host  the emulator (host_main.c -> myMain)
#   test_host    the fpga.c example (test_host.c has its own main loop)
add_library(hexmcu_host_common OBJECT
    ${APP_SOURCES}
    ${APP_ROOT}/src/port/host/io_stdio.c
    ${APP_ROOT}/src/port/host/platform_host.c
    ${APP_ROOT}/src/port/host/transport_host.c
    ${APP_ROOT}/src/port/host/display_host.c
    ${APP_ROOT}/src/port/host/fake_fpga.c
)

# PUBLIC: both executables inherit includes, defines, flags and SDL.
target_link_libraries(hexmcu_host_common PUBLIC SDL3::SDL3)

target_include_directories(hexmcu_host_common PUBLIC
    ${APP_INCLUDES}
    ${APP_ROOT}/src/port/host
)

target_compile_definitions(hexmcu_host_common PUBLIC
    LOG_LEVEL=LOG_LEVEL_DEBUG
)

target_compile_options(hexmcu_host_common PUBLIC
    -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(hexmcu_host_common PUBLIC
    -fsanitize=address,undefined)

add_executable(hexmcu_host ${APP_ROOT}/src/port/host/host_main.c)
target_link_libraries(hexmcu_host PRIVATE hexmcu_host_common)

add_executable(test_host ${APP_ROOT}/src/port/host/test_host.c)
target_link_libraries(test_host PRIVATE hexmcu_host_common)