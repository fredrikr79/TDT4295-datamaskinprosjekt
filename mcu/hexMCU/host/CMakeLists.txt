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

# The emulator: the portable code plus the host port. host_main.c stands in
# for CubeMX's main() and calls the same myMain() as the firmware.
add_executable(hexmcu_host
    ${APP_SOURCES}
    ${APP_ROOT}/src/port/host/host_main.c
    ${APP_ROOT}/src/port/host/io_stdio.c
    ${APP_ROOT}/src/port/host/platform_host.c
    ${APP_ROOT}/src/port/host/transport_host.c
    ${APP_ROOT}/src/port/host/display_host.c
    ${APP_ROOT}/src/port/host/fake_fpga.c
    ${APP_ROOT}/src/port/host/usb_backend_sdl.c
    ${APP_ROOT}/src/port/host/storage_host.c
)

target_link_libraries(hexmcu_host PRIVATE SDL3::SDL3)

target_include_directories(hexmcu_host PRIVATE
    ${APP_INCLUDES}
    ${APP_ROOT}/src/port/host
)

target_compile_definitions(hexmcu_host PRIVATE
    LOG_LEVEL=LOG_LEVEL_DEBUG
    STORAGE_HOST_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/sd"
)

target_compile_options(hexmcu_host PRIVATE
    -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(hexmcu_host PRIVATE
    -fsanitize=address,undefined)