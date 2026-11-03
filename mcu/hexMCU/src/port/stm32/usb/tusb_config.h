#pragma once
/* TinyUSB settings, shared by every MCU build. The build can override the
 * chip and speed with target_compile_definitions():
 *   U545 (fsdev):        defaults below
 *   N6, U595 (dwc2):     CFG_TUSB_MCU=OPT_MCU_STM32N6 (or _U5),
 *                        CFG_TUH_MAX_SPEED=OPT_MODE_HIGH_SPEED
 * This file is included by tusb_option.h after the OPT_* values exist. */

#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU          OPT_MCU_STM32U5
#endif
#define CFG_TUSB_OS           OPT_OS_NONE

/* 0 = off. 1-3 log through tusb_log_printf() (usb_backend_tinyusb.c) to
 * the UART. Logging over a blocking UART slows TinyUSB down enough to
 * break enumeration, so turn it on only to chase a problem. */
#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG        0
#endif
#if CFG_TUSB_DEBUG
int tusb_log_printf(const char *fmt, ...);
#define CFG_TUSB_DEBUG_PRINTF tusb_log_printf
#endif

#define CFG_TUH_ENABLED       1

/* dwc2 with a built-in HS PHY (N6, U595) must run HIGH_SPEED: with
 * FULL_SPEED TinyUSB takes its FS-PHY path, which those chips don't have.
 * Low/full-speed keyboards and mice still work on a high-speed port. */
#ifndef CFG_TUH_MAX_SPEED
#define CFG_TUH_MAX_SPEED     OPT_MODE_FULL_SPEED
#endif

/* dwc2: the CPU copies to/from the FIFOs (no DMA), so TinyUSB's buffers
 * need no D-cache handling and no special RAM section. Ignored by fsdev. */
#define CFG_TUH_DWC2_SLAVE_ENABLE 1
#define CFG_TUH_DWC2_DMA_ENABLE   0

#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB           1                     // 0 if no hub
#define CFG_TUH_DEVICE_MAX    (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_HID           4                     // keyboards are often composite
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
