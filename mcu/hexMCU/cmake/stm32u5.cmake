# =====================================================================
# stm32u5.cmake -- everything an STM32U5 build of hexMCU needs on top of
# its CubeMX project: our common code, the STM32 port, TinyUSB.
#
# Used by both STM32 projects:
#   CMakeLists.txt               the Nucleo-U545RE-Q (USB_IP fsdev)
#   cube/hexU59/CMakeLists.txt   the final board, STM32U595ZJT6 (USB_IP dwc2)
#
# How to use it, in a CubeMX CMakeLists.txt, after add_executable():
#
#   set(USB_IP dwc2)                                  # or fsdev
#   include(<path to here>/stm32u5.cmake)
#   hexmcu_stm32u5_setup(${CMAKE_PROJECT_NAME})
#
# Settings (set them before the include, or with -D on the command line):
#   USB_IP               fsdev = U535/U545 (USB_DRD_FS), dwc2 = U595/U5A5 (OTG_HS)
#   HEXMCU_USB_USE_UCPD  dwc2 only: 1 = CC1/CC2 go to UCPD1 (PA15/PB15), VBUS
#                        only turns on once a device is plugged in; 0 = VBUS on
#                        from start. Default 1 (our PCB, J8).
#   HEXMCU_LOG_LEVEL     default LOG_LEVEL_INFO
#
# Paths here are all from this file's own folder, so it works from any
# project that includes it.
# =====================================================================

get_filename_component(HEXMCU_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(NOT DEFINED USB_IP)
    message(FATAL_ERROR "stm32u5.cmake: set USB_IP (fsdev or dwc2) before including it")
endif()
if(NOT DEFINED HEXMCU_USB_USE_UCPD)
    set(HEXMCU_USB_USE_UCPD 1)
endif()
if(NOT DEFINED HEXMCU_LOG_LEVEL)
    set(HEXMCU_LOG_LEVEL LOG_LEVEL_INFO)
endif()

# the common code (APP_SOURCES/INCLUDES, TINYUSB_*), and the TinyUSB dwc2 fixes
include(${HEXMCU_ROOT}/cmake/sources.cmake)
include(${HEXMCU_ROOT}/cmake/tinyusb_patches.cmake)

function(hexmcu_stm32u5_setup target)
    message(STATUS "hexMCU: STM32U5, USB ${USB_IP}, log level ${HEXMCU_LOG_LEVEL}")

    # ---- our code: common + STM32 port --------------------------------
    target_sources(${target} PRIVATE
        ${APP_SOURCES}
        ${HEXMCU_ROOT}/src/port/stm32/io_uart.c
        ${HEXMCU_ROOT}/src/port/stm32/platform_stm32.c
        ${HEXMCU_ROOT}/src/port/stm32/transport_ospi.c
        ${HEXMCU_ROOT}/src/port/stm32/storage_filex.c
    )
    target_include_directories(${target} PRIVATE
        ${APP_INCLUDES}
        ${HEXMCU_ROOT}/src/port/stm32
    )
    target_compile_definitions(${target} PRIVATE
        LOG_LEVEL=${HEXMCU_LOG_LEVEL}
    )

    # ---- USB host: TinyUSB + our backend -------------------------------
    target_sources(${target} PRIVATE
        ${TINYUSB_SOURCES}
        ${HEXMCU_ROOT}/src/port/stm32/usb/usb_backend_tinyusb.c
        ${HEXMCU_ROOT}/src/port/stm32/usb/hid_mouse.c          # mouse report layout (wheel)
        ${HEXMCU_ROOT}/src/port/stm32/usb/usb_port_stm32u5.c
    )
    target_include_directories(${target} PRIVATE
        ${TINYUSB_INCLUDES}
        ${HEXMCU_ROOT}/src/port/stm32/usb                      # tusb_config.h, usb_backend.h, hid_mouse.h
    )
    # TinyUSB fixes in usb_backend_tinyusb.c, instead of patching third_party/:
    #   hcd_setup_send  hub descriptor request (hub.c)
    #   hcd_edpt_xfer   lost periodic transfer race (hcd_dwc2.c)
    target_link_options(${target} PRIVATE
        -Wl,--wrap=hcd_setup_send
        -Wl,--wrap=hcd_edpt_xfer
    )

    if(USB_IP STREQUAL "fsdev")
        target_sources(${target} PRIVATE
            ${TINYUSB_ROOT}/src/portable/st/stm32_fsdev/hcd_stm32_fsdev.c
            ${TINYUSB_ROOT}/src/portable/st/stm32_fsdev/fsdev_common.c
        )
    elseif(USB_IP STREQUAL "dwc2")
        # patched copy of hcd_dwc2.c in the build folder (tinyusb_patches.cmake)
        hexmcu_tinyusb_hcd_dwc2(HCD_DWC2)
        target_sources(${target} PRIVATE
            ${HCD_DWC2}
            ${TINYUSB_ROOT}/src/portable/synopsys/dwc2/dwc2_common.c
            ${HEXMCU_ROOT}/src/port/stm32/usb/usb_dwc2_debug.c  # event counters + register dump
        )
        target_include_directories(${target} PRIVATE
            ${HEXMCU_HCD_DWC2_INCLUDE}                          # the copy includes dwc2_common.h etc. from here
        )
        target_compile_definitions(${target} PRIVATE
            # OTG_HS with the built-in high-speed PHY: without this TinyUSB sets
            # the core up for an external full-speed PHY and the port stays dead
            CFG_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
            # J8's CC1/CC2 go straight to UCPD1 (PA15/PB15)
            USB_USE_UCPD=${HEXMCU_USB_USE_UCPD}
        )
    else()
        message(FATAL_ERROR "USB_IP must be fsdev or dwc2, got '${USB_IP}'")
    endif()
endfunction()
