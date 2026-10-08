set(APP_ROOT ${CMAKE_CURRENT_LIST_DIR}/..)

# Portable code: built unchanged for host and firmware.
set(APP_SOURCES
    ${APP_ROOT}/src/app/myMain.c
    ${APP_ROOT}/src/app/log.c
    ${APP_ROOT}/src/app/cli.c
    ${APP_ROOT}/src/app/transport_core.c
    ${APP_ROOT}/src/app/fpga.c
    ${APP_ROOT}/src/app/frame_sync.c
)
set(APP_INCLUDES
    ${APP_ROOT}/src/include
)

# TinyUSB host stack core. Portable, but only the firmware uses it.
# Whoever uses this also links an HCD driver and provides tusb_config.h.
set(TINYUSB_ROOT ${APP_ROOT}/third_party/tinyusb)
set(TINYUSB_SOURCES
    ${TINYUSB_ROOT}/src/tusb.c
    ${TINYUSB_ROOT}/src/common/tusb_fifo.c
    ${TINYUSB_ROOT}/src/host/usbh.c
    ${TINYUSB_ROOT}/src/host/hub.c
    ${TINYUSB_ROOT}/src/class/hid/hid_host.c
)
set(TINYUSB_INCLUDES
    ${TINYUSB_ROOT}/src
)