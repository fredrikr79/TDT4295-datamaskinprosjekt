cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME hexMCU)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# ---------------------------------------------------------------------
# Filesystem backend: leave exactly ONE of these lines uncommented.
#   fatfs  third_party/ff16 + port/stm32/diskio_sdmmc.c
#   filex  port/stm32/fs_filex.c (stub for now)
# Save, then build as usual; CMake reconfigures by itself.
# ---------------------------------------------------------------------
set(FS_BACKEND fatfs)
# set(FS_BACKEND filex)

message("Filesystem backend: " ${FS_BACKEND})

# Include sources.cmake
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/sources.cmake)

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ${APP_SOURCES}
    port/stm32/io_uart.c
    port/stm32/platform_stm32.c
    port/stm32/transport_ospi.c
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined include paths
    ${APP_INCLUDES}
)

if(FS_BACKEND STREQUAL "fatfs")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        ${FATFS_SOURCES}
        port/stm32/diskio_sdmmc.c
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        ${FATFS_INCLUDES}
    )
elseif(FS_BACKEND STREQUAL "filex")
    target_sources(${CMAKE_PROJECT_NAME} PRIVATE
        port/stm32/fs_filex.c
        # TODO(filex): FileX sources + SD driver
    )
    target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
        port/stm32                      # fs_filex.h
        # TODO(filex): FileX include dirs
    )
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
        FS_USE_FILEX
        # TODO(filex): FX_STANDALONE_ENABLE if running without ThreadX
    )
else()
    message(FATAL_ERROR "FS_BACKEND must be fatfs or filex, got '${FS_BACKEND}'")
endif()

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
    LOG_LEVEL=LOG_LEVEL_INFO
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)