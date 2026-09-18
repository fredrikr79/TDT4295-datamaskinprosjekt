cmake_minimum_required(VERSION 3.22)
project(hexmcu_host C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/sources.cmake)

add_executable(hexmcu_host
    ${APP_SOURCES}
    ${APP_ROOT}/port/host/host_main.c
    ${APP_ROOT}/port/host/io_stdio.c
    ${APP_ROOT}/port/host/platform_host.c
    ${APP_ROOT}/port/host/transport_host.c
    ${APP_ROOT}/port/host/diskio_host.c
)

target_include_directories(hexmcu_host PRIVATE
    ${APP_INCLUDES}
    ${APP_ROOT}/port/host
)

target_compile_definitions(hexmcu_host PRIVATE
    LOG_LEVEL=LOG_LEVEL_DEBUG
)

target_compile_options(hexmcu_host PRIVATE
    -Wall -Wextra -g -fsanitize=address,undefined)
target_link_options(hexmcu_host PRIVATE
    -fsanitize=address,undefined)

target_compile_definitions(hexmcu_host PRIVATE
    DEFAULT_CARD_IMAGE="${CMAKE_CURRENT_SOURCE_DIR}/card.img")