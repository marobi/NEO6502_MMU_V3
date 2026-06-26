#pragma once

// ============================================================
// tusb_config.h
// NEO6502_MMU - TinyUSB host configuration overlay
//
// Validated for RP2350 native USB host -> powered hub -> USB MSC + USB
// keyboard + mouse. These values keep enough TinyUSB host resources available
// for MSC and multiple composite HID interfaces at boot.
// ============================================================

#include_next "tusb_config.h"

#undef CFG_TUH_ENUMERATION_BUFSIZE
#define CFG_TUH_ENUMERATION_BUFSIZE 256

#undef CFG_TUH_HUB
#define CFG_TUH_HUB 1

#undef CFG_TUH_MSC
#define CFG_TUH_MSC 1

#undef CFG_TUH_DEVICE_MAX
#define CFG_TUH_DEVICE_MAX 6

#undef CFG_TUH_HID
#define CFG_TUH_HID 8

#undef CFG_TUH_HID_EP_BUFSIZE
#define CFG_TUH_HID_EP_BUFSIZE 64

#undef CFG_TUH_HID_EPIN_BUFSIZE
#define CFG_TUH_HID_EPIN_BUFSIZE 64

#undef CFG_TUH_HID_EPOUT_BUFSIZE
#define CFG_TUH_HID_EPOUT_BUFSIZE 64
