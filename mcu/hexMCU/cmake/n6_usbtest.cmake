# USB host test for the Nucleo-N657X0-Q: adds TinyUSB (dwc2) and our USB
# code to the CubeMX N6 project's FSBL target.
#
# In the N6 project's FSBL/CMakeLists.txt, after add_executable():
#
#   set(HEXMCU_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../.. CACHE PATH "hexMCU repo")
#   include(${HEXMCU_ROOT}/cmake/n6_usbtest.cmake)
#   hexmcu_add_n6_usbtest(${CMAKE_PROJECT_NAME})
#   target_sources(${CMAKE_PROJECT_NAME} PRIVATE
#       Core/Src/usbtest_main.c
#       Core/Src/platform_n6.c
#   )
#
# Only the portable input code goes in, not the game: the FPGA link
# (transport_ospi.c) is OCTOSPI, which the N6 doesn't have.

include(${CMAKE_CURRENT_LIST_DIR}/sources.cmake)   # APP_ROOT, TINYUSB_*
include(${CMAKE_CURRENT_LIST_DIR}/tinyusb_patches.cmake)

function(hexmcu_add_n6_usbtest target)
    hexmcu_tinyusb_hcd_dwc2(HCD_DWC2)            # patched copy, see tinyusb_patches.cmake
    target_sources(${target} PRIVATE
        ${APP_ROOT}/src/app/log.c
        ${APP_ROOT}/src/app/usb_input.c
        ${APP_ROOT}/src/port/stm32/io_uart.c
        ${APP_ROOT}/src/port/stm32/fault.c      # HardFault report + stack meter
        ${TINYUSB_SOURCES}
        ${HCD_DWC2}
        ${TINYUSB_ROOT}/src/portable/synopsys/dwc2/dwc2_common.c
        ${APP_ROOT}/src/port/stm32/usb/usb_backend_tinyusb.c
        ${APP_ROOT}/src/port/stm32/usb/hid_mouse.c        # mouse report layout (wheel)
        ${APP_ROOT}/src/port/stm32/usb/usb_port_stm32n6.c
        ${APP_ROOT}/src/port/stm32/usb/usb_dwc2_debug.c   # event counters + register dump
    )
    target_include_directories(${target} PRIVATE
        ${APP_INCLUDES}
        ${TINYUSB_INCLUDES}
        ${HEXMCU_HCD_DWC2_INCLUDE}
        ${APP_ROOT}/src/port/stm32/usb      # tusb_config.h, usb_port.h, usb_backend.h
        ${APP_ROOT}/src/port/stm32/
    )
    # TinyUSB fixes in usb_backend_tinyusb.c, instead of patching third_party/:
    #   hcd_setup_send  hub descriptor request (hub.c)
    #   hcd_edpt_xfer   lost periodic transfer race (hcd_dwc2.c)
    target_link_options(${target} PRIVATE -Wl,--wrap=hcd_setup_send -Wl,--wrap=hcd_edpt_xfer)
    target_compile_definitions(${target} PRIVATE
        CFG_TUSB_MCU=OPT_MCU_STM32N6
        CFG_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
        LOG_LEVEL=LOG_LEVEL_INFO
        # CFG_TUSB_DEBUG=2                  # TinyUSB's own log, see tusb_config.h
    )
endfunction()