# V29q - RP debug shared-state layout fix

This version fixes only the RP monitor/debug view of the NEOX shared-state block.

NEOX V29o/V29p added `open_file_handle[OPEN_MAX]` after `open_dev[OPEN_MAX]` in `kernel/shared_state.asm`.
The RP-side `debug_neox.cpp` mirror struct did not include that field, so every following field was read eight bytes too early.

Visible symptom:

```text
Timer PID Ticks
0       0 21250
1       0 21249
...
6      82 255
```

The kernel timer table was not necessarily corrupt; the RP monitor was reading the wrong offsets.

Changed:

```cpp
uint8_t open_file_handle[OPEN_MAX];
```

added immediately after:

```cpp
uint8_t open_dev[OPEN_MAX];
```

No mailbox ABI, USB, FatFs, NEOX, or BIOS behavior is changed.
