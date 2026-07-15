#pragma once

#include <stddef.h>
#include <stdint.h>

// ============================================================
// rp_fs_path.h
// NEO MMU - RP-owned NEOX path and per-process CWD engine
//
// Paths use NEOX 8.3 components. The CWD table is RP-local and indexed by
// kernel-supplied PID. Root is stored internally as an empty component path.
// Resolved operation paths use "/" for root and otherwise have no leading
// slash; the selected device is carried separately.
// ============================================================

static constexpr uint8_t RP_FS_NEOX_MAX_PROCS = 8;
static constexpr size_t RP_FS_NEOX_PATH_MAX = 64;
static constexpr size_t RP_FS_NEOX_CWD_MAX = 32;

enum rp_fs_path_result_t : uint8_t {
  RP_FS_PATH_OK = 0,
  RP_FS_PATH_INVALID_PID,
  RP_FS_PATH_INVALID_DEVICE,
  RP_FS_PATH_INVALID_PATH,
  RP_FS_PATH_TOO_LONG
};

struct RPFSResolvedPath {
  uint8_t device;
  char path[RP_FS_NEOX_PATH_MAX];
};

/// <summary>
/// Checks whether a device number can identify an RP USB/FatFs drive.
/// </summary>
/// <param name="device">Device number to validate.</param>
/// <returns>true when the device number is supported.</returns>
bool rp_fs_path_device_valid(uint8_t device);

/// <summary>
/// Validates one NEOX 8.3 path component.
/// </summary>
/// <param name="start">First component character.</param>
/// <param name="len">Number of component characters.</param>
/// <returns>true when the component satisfies NEOX 8.3 rules.</returns>
bool rp_fs_path_component_valid_83(const char* start, size_t len);

/// <summary>
/// Validates an already-normalized NEOX 8.3 path. The path may be unqualified,
/// root-relative, or FatFs drive-qualified. Empty and root paths are accepted
/// for directory operations.
/// </summary>
/// <param name="path">NUL-terminated path.</param>
/// <returns>true when the path is valid.</returns>
bool rp_fs_path_valid_83(const char* path);

/// <summary>
/// Initializes every RP process CWD slot to device 0 root.
/// </summary>
void rp_fs_cwd_reset_all();

/// <summary>
/// Initializes one process CWD to the selected drive root.
/// </summary>
/// <param name="pid">Kernel-supplied process ID.</param>
/// <param name="device">Initial filesystem device.</param>
/// <returns>true when PID and device are valid.</returns>
bool rp_fs_cwd_init_root(uint8_t pid, uint8_t device = 0);

/// <summary>
/// Clones a parent process CWD into a child process slot.
/// </summary>
/// <param name="child_pid">Child PID to initialize.</param>
/// <param name="parent_pid">Parent PID whose CWD is copied.</param>
/// <returns>true when both slots are valid and the parent has a CWD.</returns>
bool rp_fs_cwd_clone(uint8_t child_pid, uint8_t parent_pid);

/// <summary>
/// Returns one process CWD as a resolved device/path pair.
/// </summary>
/// <param name="pid">Kernel-supplied process ID.</param>
/// <param name="cwd">Destination for the current directory.</param>
/// <returns>true when the process CWD exists.</returns>
bool rp_fs_cwd_get(uint8_t pid, RPFSResolvedPath* cwd);

/// <summary>
/// Stores a previously resolved and filesystem-validated directory as a
/// process CWD. The caller must verify directory existence before committing.
/// </summary>
/// <param name="pid">Kernel-supplied process ID.</param>
/// <param name="cwd">Resolved directory to store.</param>
/// <returns>true when PID, device, and path are valid.</returns>
bool rp_fs_cwd_set(uint8_t pid, const RPFSResolvedPath* cwd);

/// <summary>
/// Formats one process CWD in user-visible N:/PATH form.
/// </summary>
/// <param name="pid">Kernel-supplied process ID.</param>
/// <param name="out">Destination buffer.</param>
/// <param name="out_len">Destination size.</param>
/// <param name="result_len">Optional resulting length excluding NUL.</param>
/// <returns>true when the CWD fitted in the destination.</returns>
bool rp_fs_cwd_format(uint8_t pid, char* out, size_t out_len, size_t* result_len = nullptr);

/// <summary>
/// Resolves a caller path against the RP-owned CWD for a process. Explicit
/// N:/ paths select a drive root; /PATH uses the CWD drive root; relative paths
/// start at the process CWD. Repeated separators, '.', and '..' are normalized.
/// </summary>
/// <param name="pid">Kernel-supplied process ID.</param>
/// <param name="fallback_device">Device used only if the CWD slot is uninitialized.</param>
/// <param name="input">NUL-terminated caller path copied into RP-local memory.</param>
/// <param name="resolved">Destination for selected device and canonical path.</param>
/// <returns>Detailed path result.</returns>
rp_fs_path_result_t rp_fs_resolve_process_path(uint8_t pid, uint8_t fallback_device, const char* input, RPFSResolvedPath* resolved);
