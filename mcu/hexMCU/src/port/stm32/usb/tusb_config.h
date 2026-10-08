#pragma once
#define CFG_TUSB_MCU          OPT_MCU_STM32U5
#define CFG_TUSB_OS           OPT_OS_NONE
#define CFG_TUSB_DEBUG        0

#define CFG_TUH_ENABLED       1
#define CFG_TUH_MAX_SPEED     OPT_MODE_FULL_SPEED   // U595: OPT_MODE_HIGH_SPEED
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#define CFG_TUH_HUB           1                     // 0 if no hub
#define CFG_TUH_DEVICE_MAX    (CFG_TUH_HUB ? 4 : 1)
#define CFG_TUH_HID           4                     // keyboards are often composite
#define CFG_TUH_HID_EPIN_BUFSIZE  64
#define CFG_TUH_HID_EPOUT_BUFSIZE 64