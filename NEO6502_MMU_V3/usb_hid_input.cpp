#include <Arduino.h>
#include <string.h>
#include "usb_hid_input.h"
#include "usb_keyboard_layout.h"
#include "tusb_config.h"
#include "Adafruit_TinyUSB.h"
#include "monitor.h"
#include "mailbox.h"
#include "usb_storage.h"

// ============================================================
// usb_hid_input.cpp
// NEO6502_MMU - RP2350 TinyUSB USB HID keyboard/mouse input
//
// The USB host controller and TinyUSB host task are owned by usb_storage.cpp.
// This module records HID keyboard/mouse interfaces, enables HID reports after
// storage is ready, translates simple boot-keyboard reports to ASCII, and logs
// mouse movement/button reports for hub stability testing.
//
// Boot stability rules for this module:
//   - no Serial output from TinyUSB HID callbacks;
//   - no writeCPUQ() from TinyUSB HID callbacks;
//   - no tuh_hid_interface_protocol() in the boot-sensitive path;
//   - no report arming from tuh_hid_mount_cb();
//   - report arming and input processing happen from normal loop context only.
// ============================================================

static constexpr uint8_t USB_HID_MAX_RECORDS = 12;
static constexpr uint8_t USB_KEYBOARD_MAX_KEYS = 6;
static constexpr uint32_t USB_HID_ARM_DELAY_MS = 1500;
static constexpr uint32_t USB_HID_REARM_DELAY_MS = 20;
static constexpr uint32_t USB_KEY_REPEAT_DELAY_MS = 500;
static constexpr uint32_t USB_KEY_REPEAT_INTERVAL_MS = 50;
static constexpr uint32_t USB_MOUSE_DEBUG_INTERVAL_MS = 100;


// USB HID Keyboard/Keypad usage IDs for function keys.
// These are handled as RP-side routing commands before ASCII translation.
static constexpr uint8_t USB_HID_KEY_F1  = 0x3A;
static constexpr uint8_t USB_HID_KEY_F2  = 0x3B;
static constexpr uint8_t USB_HID_KEY_F3  = 0x3C;
static constexpr uint8_t USB_HID_KEY_F4  = 0x3D;
static constexpr uint8_t USB_HID_KEY_F5  = 0x3E;
static constexpr uint8_t USB_HID_KEY_F6  = 0x3F;
static constexpr uint8_t USB_HID_KEY_F7  = 0x40;
static constexpr uint8_t USB_HID_KEY_F8  = 0x41;
static constexpr uint8_t USB_HID_KEY_F9  = 0x42;
static constexpr uint8_t USB_HID_KEY_F10 = 0x43;
static constexpr uint8_t USB_HID_KEY_F11 = 0x44;
static constexpr uint8_t USB_HID_KEY_F12 = 0x45;

enum USBHIDRole : uint8_t {
  USB_HID_ROLE_UNKNOWN = 0,
  USB_HID_ROLE_KEYBOARD,
  USB_HID_ROLE_MOUSE,
  USB_HID_ROLE_GENERIC
};

struct USBHIDMountRecord {
  uint8_t dev_addr;
  uint8_t instance;
  uint16_t desc_len;
  USBHIDRole role;
  bool mounted;
};

static USBHIDMountRecord g_hidRecords[USB_HID_MAX_RECORDS] = {};

static hid_keyboard_report_t g_lastKeyboardReport = {};
static hid_keyboard_report_t g_pendingKeyboardReport = {};
struct USBMouseBootReport {
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
  int8_t pan;
};

static USBMouseBootReport g_lastMouseReport = {};
static USBMouseBootReport g_pendingMouseReport = {};

static volatile bool g_keyboardReportPending = false;
static volatile bool g_keyboardReportActive = false;
static volatile bool g_keyboardRemovedNoticePending = false;

static volatile bool g_mouseReportPending = false;
static volatile bool g_mouseReportActive = false;
static volatile bool g_mouseRemovedNoticePending = false;

static uint8_t g_keyboardDevAddr = 0;
static uint8_t g_keyboardInstance = 0;
static bool g_keyboardSelected = false;
static bool g_keyboardActiveAnnounced = false;
static uint32_t g_lastKeyboardArmAttemptMs = 0;

static bool g_keyRepeatActive = false;
static uint8_t g_keyRepeatKeycode = 0;
static uint8_t g_keyRepeatModifier = 0;
static uint8_t g_keyRepeatAscii = 0;
static uint32_t g_keyRepeatNextMs = 0;

static uint8_t g_mouseDevAddr = 0;
static uint8_t g_mouseInstance = 0;
static bool g_mouseSelected = false;
static bool g_mouseActiveAnnounced = false;
static uint32_t g_lastMouseArmAttemptMs = 0;
static uint32_t g_lastMouseDebugPrintMs = 0;
static bool g_mouseDebugPending = false;
static int32_t g_mouseAccumX = 0;
static int32_t g_mouseAccumY = 0;
static int32_t g_mouseAccumWheel = 0;
static int32_t g_mouseAccumPan = 0;
static uint8_t g_mouseLastButtonsPrinted = 0;

static bool g_storageReadySeen = false;
static uint32_t g_storageReadyMs = 0;

static void usb_keyboard_repeat_clear();

/// <summary>
/// usb_hid_find_record returns the index for an existing HID mount record.
/// </summary>
static int usb_hid_find_record(uint8_t dev_addr, uint8_t instance) {
  for (uint8_t i = 0; i < USB_HID_MAX_RECORDS; i++) {
    if (g_hidRecords[i].mounted &&
        g_hidRecords[i].dev_addr == dev_addr &&
        g_hidRecords[i].instance == instance) {
      return (int)i;
    }
  }

  return -1;
}

/// <summary>
/// usb_hid_alloc_record returns the index for a free HID mount record.
/// </summary>
static int usb_hid_alloc_record() {
  for (uint8_t i = 0; i < USB_HID_MAX_RECORDS; i++) {
    if (!g_hidRecords[i].mounted)
      return (int)i;
  }

  return -1;
}

/// <summary>
/// usb_hid_descriptor_has_desktop_usage scans a HID report descriptor for a
/// Generic Desktop usage. This avoids calling tuh_hid_interface_protocol() in
/// the boot-sensitive path.
/// </summary>
static bool usb_hid_descriptor_has_desktop_usage(uint8_t const* desc_report,
                                                 uint16_t desc_len,
                                                 uint16_t wanted_usage) {
  if (desc_report == nullptr || desc_len == 0)
    return false;

  uint16_t usage_page = 0;
  uint16_t i = 0;

  while (i < desc_len) {
    uint8_t const prefix = desc_report[i];

    if (prefix == 0xFE) {
      if ((uint16_t)(i + 2) >= desc_len)
        break;
      uint8_t const long_size = desc_report[i + 1];
      i = (uint16_t)(i + 3 + long_size);
      continue;
    }

    uint8_t const size_code = prefix & 0x03;
    uint8_t const data_size = (size_code == 3) ? 4 : size_code;
    uint8_t const item_type = (prefix >> 2) & 0x03;
    uint8_t const item_tag = (prefix >> 4) & 0x0F;

    if ((uint16_t)(i + 1 + data_size) > desc_len)
      break;

    uint32_t value = 0;
    for (uint8_t b = 0; b < data_size; b++)
      value |= ((uint32_t)desc_report[i + 1 + b]) << (8 * b);

    // Global item: Usage Page
    if (item_type == 1 && item_tag == 0)
      usage_page = (uint16_t)value;

    // Local item: Usage
    if (item_type == 2 && item_tag == 0) {
      if (usage_page == 0x01 && (uint16_t)value == wanted_usage)
        return true;
    }

    i = (uint16_t)(i + 1 + data_size);
  }

  return false;
}

/// <summary>
/// usb_hid_classify_descriptor classifies a mounted HID interface from the
/// report descriptor. desc_len == 0 is treated as generic/vendor HID.
/// </summary>
static USBHIDRole usb_hid_classify_descriptor(uint8_t const* desc_report, uint16_t desc_len) {
  if (desc_len == 0)
    return USB_HID_ROLE_GENERIC;

  if (usb_hid_descriptor_has_desktop_usage(desc_report, desc_len, 0x06))
    return USB_HID_ROLE_KEYBOARD;

  if (usb_hid_descriptor_has_desktop_usage(desc_report, desc_len, 0x02))
    return USB_HID_ROLE_MOUSE;

  return USB_HID_ROLE_UNKNOWN;
}

/// <summary>
/// usb_keyboard_keycode_to_ascii converts a boot-keyboard HID key code to the
/// active locale's single-byte console character. Function keys are handled
/// before this point and are not layout-dependent. Unsupported locale-specific
/// extended characters return 0 and are ignored.
/// </summary>
static uint8_t usb_keyboard_keycode_to_ascii(uint8_t keycode, uint8_t modifier) {
  return usb_keyboard_keycode_to_ascii_locale(keycode, modifier);
}

/// <summary>
/// usb_keyboard_key_is_down checks whether a key exists in a boot-keyboard
/// report.
/// </summary>
static bool usb_keyboard_key_is_down(const hid_keyboard_report_t& report, uint8_t keycode) {
  for (uint8_t i = 0; i < USB_KEYBOARD_MAX_KEYS; i++) {
    if (report.keycode[i] == keycode)
      return true;
  }

  return false;
}

/// <summary>
/// usb_keyboard_is_route_key checks whether a raw HID key code is reserved for
/// RP-side keyboard context selection. Locale layout handling must not affect
/// F-key routing.
/// </summary>
static bool usb_keyboard_is_route_key(uint8_t keycode) {
  return keycode >= USB_HID_KEY_F1 && keycode <= USB_HID_KEY_F12;
}

/// <summary>
/// usb_keyboard_handle_route_key_down consumes function-key routing commands.
/// F1..F9 select console contexts 1..9, F10 returns to context 0, and F11/F12
/// are reserved. The existing RP console PID is the source of truth; no separate
/// keyboard routing state is kept here.
/// </summary>
static bool usb_keyboard_handle_route_key_down(uint8_t keycode) {
  if (!usb_keyboard_is_route_key(keycode))
    return false;

  if (keycode >= USB_HID_KEY_F1 && keycode <= USB_HID_KEY_F9) {
    usb_keyboard_repeat_clear();
    uint8_t const pid = (uint8_t)(keycode - USB_HID_KEY_F1 + 1);
    setConsolePID(pid);
    Serial1.printf("*I: USB keyboard route: context %u\n", getConsolePID());
    return true;
  }

  if (keycode == USB_HID_KEY_F10) {
    usb_keyboard_repeat_clear();
    setConsolePID(0);
    Serial1.printf("*I: USB keyboard route: context %u\n", getConsolePID());
    return true;
  }

  usb_keyboard_repeat_clear();
  Serial1.printf("*I: USB keyboard route: F%u reserved\n",
                 (unsigned)(keycode - USB_HID_KEY_F1 + 1));
  return true;
}

/// <summary>
/// usb_hid_select_keyboard chooses the first mounted interface classified as a
/// keyboard.
/// </summary>
static bool usb_hid_select_keyboard() {
  if (g_keyboardSelected)
    return true;

  for (uint8_t i = 0; i < USB_HID_MAX_RECORDS; i++) {
    USBHIDMountRecord& r = g_hidRecords[i];
    if (!r.mounted || r.role != USB_HID_ROLE_KEYBOARD)
      continue;

    g_keyboardDevAddr = r.dev_addr;
    g_keyboardInstance = r.instance;
    g_keyboardSelected = true;
    memset(&g_lastKeyboardReport, 0, sizeof(g_lastKeyboardReport));
    memset(&g_pendingKeyboardReport, 0, sizeof(g_pendingKeyboardReport));
    usb_keyboard_repeat_clear();

    Serial1.printf("*I: USB keyboard ready: dev=%u inst=%u layout=%s\n",
                   g_keyboardDevAddr,
                   g_keyboardInstance,
                   usb_keyboard_get_locale_name());
    return true;
  }

  return false;
}

/// <summary>
/// usb_hid_select_mouse chooses the first mounted interface classified as a
/// mouse.
/// </summary>
static bool usb_hid_select_mouse() {
  if (g_mouseSelected)
    return true;

  for (uint8_t i = 0; i < USB_HID_MAX_RECORDS; i++) {
    USBHIDMountRecord& r = g_hidRecords[i];
    if (!r.mounted || r.role != USB_HID_ROLE_MOUSE)
      continue;

    g_mouseDevAddr = r.dev_addr;
    g_mouseInstance = r.instance;
    g_mouseSelected = true;
    memset(&g_lastMouseReport, 0, sizeof(g_lastMouseReport));
    memset(&g_pendingMouseReport, 0, sizeof(g_pendingMouseReport));
    g_mouseAccumX = 0;
    g_mouseAccumY = 0;
    g_mouseAccumWheel = 0;
    g_mouseAccumPan = 0;
    g_mouseDebugPending = false;
    g_lastMouseDebugPrintMs = millis();

    Serial1.printf("*I: USB mouse ready: dev=%u inst=%u\n",
                   g_mouseDevAddr,
                   g_mouseInstance);
    return true;
  }

  return false;
}

/// <summary>
/// usb_keyboard_repeat_clear stops software key repeat. This is called when the
/// repeated key is released, the keyboard is removed, or a new keyboard is selected.
/// </summary>
static void usb_keyboard_repeat_clear() {
  g_keyRepeatActive = false;
  g_keyRepeatKeycode = 0;
  g_keyRepeatModifier = 0;
  g_keyRepeatAscii = 0;
  g_keyRepeatNextMs = 0;
}

/// <summary>
/// usb_keyboard_repeat_start starts software repeat for a normal routed key.
/// Function-key routing commands are consumed before this point and are never
/// repeated.
/// </summary>
static void usb_keyboard_repeat_start(uint8_t keycode, uint8_t modifier, uint8_t ascii) {
  if (ascii == 0) {
    usb_keyboard_repeat_clear();
    return;
  }

  g_keyRepeatActive = true;
  g_keyRepeatKeycode = keycode;
  g_keyRepeatModifier = modifier;
  g_keyRepeatAscii = ascii;
  g_keyRepeatNextMs = millis() + USB_KEY_REPEAT_DELAY_MS;
}

/// <summary>
/// usb_keyboard_repeat_update_from_report keeps the repeat candidate in sync
/// with the latest modifier state and clears repeat when the candidate key is
/// released.
/// </summary>
static void usb_keyboard_repeat_update_from_report(const hid_keyboard_report_t& report) {
  if (!g_keyRepeatActive)
    return;

  if (!usb_keyboard_key_is_down(report, g_keyRepeatKeycode)) {
    usb_keyboard_repeat_clear();
    return;
  }

  if (report.modifier != g_keyRepeatModifier) {
    uint8_t const c = usb_keyboard_keycode_to_ascii(g_keyRepeatKeycode, report.modifier);
    if (c == 0) {
      usb_keyboard_repeat_clear();
      return;
    }

    g_keyRepeatModifier = report.modifier;
    g_keyRepeatAscii = c;
  }
}

/// <summary>
/// usb_keyboard_repeat_task emits repeated input for the current repeat
/// candidate from normal loop context.
/// </summary>
static void usb_keyboard_repeat_task() {
  if (!g_keyRepeatActive || g_keyRepeatAscii == 0)
    return;

  uint32_t const now = millis();
  if ((int32_t)(now - g_keyRepeatNextMs) < 0)
    return;

  monitorConsoleInput(g_keyRepeatAscii, false);
  g_keyRepeatNextMs = now + USB_KEY_REPEAT_INTERVAL_MS;
}

/// <summary>
/// usb_keyboard_process_report translates key transitions from a completed
/// boot-keyboard report. Function keys are consumed as RP-side context routing
/// commands before ASCII translation. Normal ASCII keys are passed through
/// monitorConsoleInput(c, false), giving USB keyboard input the same local echo
/// and console handling as the Serial1 terminal path without return-to-ICM.
/// </summary>
static void usb_keyboard_process_report(const hid_keyboard_report_t& report) {
  for (uint8_t i = 0; i < USB_KEYBOARD_MAX_KEYS; i++) {
    uint8_t const keycode = report.keycode[i];
    if (keycode == 0)
      continue;

    if (usb_keyboard_key_is_down(g_lastKeyboardReport, keycode))
      continue;

    if (usb_keyboard_handle_route_key_down(keycode))
      continue;

    uint8_t const c = usb_keyboard_keycode_to_ascii(keycode, report.modifier);
    if (c != 0) {
      monitorConsoleInput(c, false);
      usb_keyboard_repeat_start(keycode, report.modifier, c);
    }
  }

  usb_keyboard_repeat_update_from_report(report);
  g_lastKeyboardReport = report;
}

/// <summary>
/// usb_mouse_process_report prints concise mouse diagnostics. Mouse input is
/// not routed to NEOX yet.
/// </summary>
static void usb_mouse_process_report(const USBMouseBootReport& report) {
  bool const movement = (report.x != 0) || (report.y != 0) || (report.wheel != 0) || (report.pan != 0);
  bool const buttons_changed = (report.buttons != g_lastMouseReport.buttons);

  if (movement) {
    g_mouseAccumX += report.x;
    g_mouseAccumY += report.y;
    g_mouseAccumWheel += report.wheel;
    g_mouseAccumPan += report.pan;
  }

  if (movement || buttons_changed)
    g_mouseDebugPending = true;

  uint32_t const now = millis();
  bool const debug_due = buttons_changed ||
                         ((uint32_t)(now - g_lastMouseDebugPrintMs) >= USB_MOUSE_DEBUG_INTERVAL_MS);

  if (g_mouseDebugPending && debug_due) {
    Serial1.printf("*I: USB mouse: buttons=%02X dx=%ld dy=%ld wheel=%ld pan=%ld\n",
                   report.buttons,
                   (long)g_mouseAccumX,
                   (long)g_mouseAccumY,
                   (long)g_mouseAccumWheel,
                   (long)g_mouseAccumPan);

    g_mouseAccumX = 0;
    g_mouseAccumY = 0;
    g_mouseAccumWheel = 0;
    g_mouseAccumPan = 0;
    g_mouseLastButtonsPrinted = report.buttons;
    g_mouseDebugPending = false;
    g_lastMouseDebugPrintMs = now;
  }

  g_lastMouseReport = report;
}

/// <summary>
/// usb_hid_process_pending_reports copies completed keyboard/mouse reports out
/// of the TinyUSB callback handoff areas and processes them from normal loop
/// context.
/// </summary>
static void usb_hid_process_pending_reports() {
  if (g_keyboardReportPending) {
    hid_keyboard_report_t report;
    memcpy(&report, &g_pendingKeyboardReport, sizeof(report));
    g_keyboardReportPending = false;
    usb_keyboard_process_report(report);
  }

  if (g_mouseReportPending) {
    USBMouseBootReport report;
    memcpy(&report, &g_pendingMouseReport, sizeof(report));
    g_mouseReportPending = false;
    usb_mouse_process_report(report);
  }
}

/// <summary>
/// usb_hid_arm_keyboard_report requests the next boot-keyboard report. This is
/// called only from normal loop context and only for the selected keyboard.
/// </summary>
static void usb_hid_arm_keyboard_report() {
  if (!g_keyboardSelected || g_keyboardReportActive || g_keyboardReportPending)
    return;

  uint32_t const now = millis();
  if ((uint32_t)(now - g_lastKeyboardArmAttemptMs) < USB_HID_REARM_DELAY_MS)
    return;
  g_lastKeyboardArmAttemptMs = now;

  if (tuh_hid_receive_report(g_keyboardDevAddr, g_keyboardInstance)) {
    g_keyboardReportActive = true;
    if (!g_keyboardActiveAnnounced) {
      g_keyboardActiveAnnounced = true;
      Serial1.println("*I: USB keyboard input active");
    }
  }
}

/// <summary>
/// usb_hid_arm_mouse_report requests the next mouse report. This is called only
/// from normal loop context and only for the selected mouse.
/// </summary>
static void usb_hid_arm_mouse_report() {
  if (!g_mouseSelected || g_mouseReportActive || g_mouseReportPending)
    return;

  uint32_t const now = millis();
  if ((uint32_t)(now - g_lastMouseArmAttemptMs) < USB_HID_REARM_DELAY_MS)
    return;
  g_lastMouseArmAttemptMs = now;

  if (tuh_hid_receive_report(g_mouseDevAddr, g_mouseInstance)) {
    g_mouseReportActive = true;
    if (!g_mouseActiveAnnounced) {
      g_mouseActiveAnnounced = true;
      Serial1.println("*I: USB mouse input active");
    }
  }
}

/// <summary>
/// initUSBHIDInput announces that the staged HID input layer is present. TinyUSB
/// host initialization remains owned by usb_storage.
/// </summary>
void initUSBHIDInput() {
  setConsolePID(0);
  Serial1.printf("*I: USB HID input: staged keyboard/mouse enabled; key repeat active; current context %u; F1-F9 route contexts 1-9, F10 routes context 0\n", getConsolePID());
}

/// <summary>
/// taskUSBHIDInput performs staged HID selection, report handoff, and report
/// arming from normal loop context.
/// </summary>
void taskUSBHIDInput() {
  if (g_keyboardRemovedNoticePending) {
    g_keyboardRemovedNoticePending = false;
    setConsolePID(0);
    Serial1.printf("*I: USB keyboard removed; route reset to context %u\n", getConsolePID());
  }

  if (g_mouseRemovedNoticePending) {
    g_mouseRemovedNoticePending = false;
    Serial1.println("*I: USB mouse removed");
  }

  if (usb_storage_ready() && !g_storageReadySeen) {
    g_storageReadySeen = true;
    g_storageReadyMs = millis();
  }

  if (!g_storageReadySeen)
    return;

  if ((uint32_t)(millis() - g_storageReadyMs) < USB_HID_ARM_DELAY_MS)
    return;

  usb_hid_select_keyboard();
  usb_hid_select_mouse();
  usb_hid_process_pending_reports();
  usb_keyboard_repeat_task();
  usb_hid_arm_keyboard_report();
  usb_hid_arm_mouse_report();
}

/// <summary>
/// tuh_hid_mount_cb records a HID interface mount. It deliberately does not
/// print, query HID protocol, or arm HID reports.
/// </summary>
extern "C" void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
  int index = usb_hid_find_record(dev_addr, instance);
  if (index < 0)
    index = usb_hid_alloc_record();

  if (index >= 0) {
    USBHIDMountRecord& r = g_hidRecords[index];
    r.dev_addr = dev_addr;
    r.instance = instance;
    r.desc_len = desc_len;
    r.role = usb_hid_classify_descriptor(desc_report, desc_len);
    r.mounted = true;
  }
}

/// <summary>
/// tuh_hid_umount_cb records HID interface removal and clears selected keyboard
/// or mouse state if the selected interface disappeared.
/// </summary>
extern "C" void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
  int const index = usb_hid_find_record(dev_addr, instance);
  if (index >= 0)
    memset(&g_hidRecords[index], 0, sizeof(g_hidRecords[index]));

  if (g_keyboardSelected && dev_addr == g_keyboardDevAddr && instance == g_keyboardInstance) {
    g_keyboardSelected = false;
    g_keyboardReportActive = false;
    g_keyboardReportPending = false;
    g_keyboardActiveAnnounced = false;
    g_keyboardRemovedNoticePending = true;
    g_keyboardDevAddr = 0;
    g_keyboardInstance = 0;
    memset(&g_lastKeyboardReport, 0, sizeof(g_lastKeyboardReport));
    memset(&g_pendingKeyboardReport, 0, sizeof(g_pendingKeyboardReport));
    usb_keyboard_repeat_clear();
  }

  if (g_mouseSelected && dev_addr == g_mouseDevAddr && instance == g_mouseInstance) {
    g_mouseSelected = false;
    g_mouseReportActive = false;
    g_mouseReportPending = false;
    g_mouseActiveAnnounced = false;
    g_mouseRemovedNoticePending = true;
    g_mouseDevAddr = 0;
    g_mouseInstance = 0;
    g_mouseAccumX = 0;
    g_mouseAccumY = 0;
    g_mouseAccumWheel = 0;
    g_mouseAccumPan = 0;
    g_mouseDebugPending = false;
    memset(&g_lastMouseReport, 0, sizeof(g_lastMouseReport));
    memset(&g_pendingMouseReport, 0, sizeof(g_pendingMouseReport));
  }
}

/// <summary>
/// tuh_hid_report_received_cb copies one completed keyboard or mouse report
/// into a small handoff buffer. It deliberately does not print, queue input,
/// query HID protocol, or rearm the next report.
/// </summary>
extern "C" void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
  if (g_keyboardSelected && dev_addr == g_keyboardDevAddr && instance == g_keyboardInstance) {
    g_keyboardReportActive = false;

    if (len >= sizeof(hid_keyboard_report_t)) {
      memcpy(&g_pendingKeyboardReport, report, sizeof(g_pendingKeyboardReport));
      g_keyboardReportPending = true;
    }
    return;
  }

  if (g_mouseSelected && dev_addr == g_mouseDevAddr && instance == g_mouseInstance) {
    g_mouseReportActive = false;

    memset(&g_pendingMouseReport, 0, sizeof(g_pendingMouseReport));
    uint16_t const copy_len = (len < sizeof(USBMouseBootReport)) ? len : sizeof(USBMouseBootReport);
    if (copy_len > 0) {
      memcpy(&g_pendingMouseReport, report, copy_len);
      g_mouseReportPending = true;
    }
  }
}
