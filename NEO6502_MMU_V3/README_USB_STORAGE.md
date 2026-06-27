# USB storage + HID input baseline

This version keeps the validated RP2350 USB host setup for a powered hub with MSC storage, a USB HID keyboard, and USB HID mouse testing.

- USB storage mounts normally through TinyUSB MSC and FatFs.
- USB HID keyboard/mouse input is staged until storage is ready.
- Keyboard normal input uses `monitorConsoleInput(c, false)`, so USB keyboard input has local echo and the same console input behavior as the Serial1 terminal path, without USB Ctrl-Z returning to ICM.
- Function keys are consumed by the RP for context/PID routing.
- Mouse input is diagnostic-only and rate-limited.
- The RP-local `TEST.TXT` / `BIG.TXT` validation is manual through the temporary monitor command `fstest`.

## USB HID input module

The HID input module is `usb_hid_input.cpp/.h`. It handles keyboard and mouse HID interfaces through the single global TinyUSB HID callback set.

Boot stability rules:

- no Serial output from TinyUSB HID callbacks;
- no `writeCPUQ()` from TinyUSB HID callbacks;
- no `tuh_hid_interface_protocol()` in the boot-sensitive path;
- no report arming from `tuh_hid_mount_cb()`;
- report arming and input processing happen from normal loop context only.

## TinyUSB host configuration

The baseline uses the composite-HID-safe configuration validated with MSC + keyboard + mouse through the powered hub:

- `CFG_TUH_DEVICE_MAX = 6`
- `CFG_TUH_HID = 8`
- `CFG_TUH_ENUMERATION_BUFSIZE = 256`
- HID endpoint buffers remain 64 bytes.

## Keyboard routing

USB keyboard input uses the existing RP console PID state; no separate keyboard router state is kept. Function keys are consumed by the RP and are not sent to the CPU input FIFO:

- F1..F9 select contexts/PIDs 1..9 via `setConsolePID()`
- F10 selects context/PID 0 via `setConsolePID(0)`
- F11/F12 are reserved and consumed
- normal ASCII/control keys use `monitorConsoleInput(c, false)` for local echo plus CPU queue delivery
- keyboard removal resets the route to context/PID 0

Serial1 output is diagnostic only. The functional path is USB HID keyboard -> function-key routing / ASCII decode -> `setConsolePID()` for F-keys or `monitorConsoleInput(c, false)` for normal keys.

## V24 control character support

USB keyboard Ctrl combinations produce real control bytes before normal ASCII translation:

- Ctrl-A..Ctrl-Z -> 0x01..0x1A
- Ctrl-[ -> ESC / 0x1B
- Ctrl-\\ -> 0x1C
- Ctrl-] -> 0x1D
- Ctrl-^ -> 0x1E
- Ctrl-_ -> 0x1F

USB keyboard Ctrl-Z is delivered to the selected console as a control byte. Serial1 Ctrl-Z still returns to ICM.

## V26 key repeat

Software key repeat is added in `usb_hid_input.cpp`:

- normal routed keys repeat after a 500 ms initial delay;
- repeated characters are emitted every 50 ms while the key remains held;
- F1-F12 routing keys do not repeat;
- repeat is cleared on key release, keyboard removal, or keyboard reselection.


## V28c notes

V28c keeps the validated local FatFs multi-volume build and makes the RP FS layer device-aware:

- RP FS handles now record their owning device/FatFs drive.
- `rp_fs_open_readonly_83(device, filename)` opens on `device:/`.
- `rp_fs_close_all_for_device(device)` closes only handles for the removed/unmounted drive.
- `FS_OPEN` uses `ARG2` low byte as flags and high byte as device id.
- `FS_STATUS` returns ready bit, mounted count, and mounted-device bitmask.
- `fstest [device]` tests `TEST.TXT` and `BIG.TXT` on the selected drive.
- `usbdisks` now labels device 0 as `default` rather than implying other mounted drives are inactive.


## Quiet boot output

Boot-time USB informational output is intentionally quiet. Use `usbdisks` and `fstest [device]` for explicit diagnostics. Errors and device-removal notices remain on Serial1.


## V28e boot status policy

V28e keeps concise USB lifecycle confirmation while avoiding verbose boot debug:

```text
*I: USB MSC ready: device=N drive=N:
*I: USB keyboard ready: layout=US/DE
*I: USB mouse ready
```

Verbose block geometry, FatFs mount steps, HID activation details, and fstest reminders remain removed. Use `usbdisks` and `fstest [device]` for explicit diagnostics.
