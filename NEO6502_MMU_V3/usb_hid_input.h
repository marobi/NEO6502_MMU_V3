#pragma once

// ============================================================
// usb_hid_input.h
// NEO6502_MMU - TinyUSB USB HID keyboard/mouse input
// ============================================================

/// <summary>
/// initUSBHIDInput initializes the USB HID input layer. The USB host controller
/// itself is initialized and serviced by usb_storage.
/// </summary>
void initUSBHIDInput();

/// <summary>
/// taskUSBHIDInput performs staged HID device selection, report handoff, and
/// USB keyboard console input from normal loop context. Mouse reports are
/// currently diagnostic only and are not routed to NEOX.
/// </summary>
void taskUSBHIDInput();
