# RP firmware 3.17.680 — debug_neox shared layout correction

Baseline: uploaded `NEO6502_MMU sources(2).zip` (version 3.17.679).

## Changes

- replaced obsolete two-argument launch metadata with the validated opaque
  launch-line layout;
- set `SPAWN_LINE_MAX` to 64 bytes;
- added `proc_launch_line_len[MAX_PROCS]`;
- added `proc_launch_line[MAX_PROCS * SPAWN_LINE_MAX]`;
- removed obsolete `proc_launch_argc`, `proc_launch_arg0_len`,
  `proc_launch_arg1_len`, `proc_launch_arg0`, and `proc_launch_arg1` fields;
- added `OBJ_DIR` / `DIR` to the debug open-object type table;
- added compile-time offset and total-size checks for `scheduler_info_t`;
- incremented RP firmware version from 3.17.679 to 3.17.680.

The corrected key addresses are:

- `proc_launch_id`: `$B15F`
- `proc_launch_line_len`: `$B167`
- `proc_launch_line`: `$B16F`
- `system_ticks_lo`: `$B36F`
- shared structure end: `$B5DD`
