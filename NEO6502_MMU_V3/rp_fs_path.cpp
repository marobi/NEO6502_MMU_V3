#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "rp_fs_path.h"
#include "usb_storage.h"

// ============================================================
// rp_fs_path.cpp
// NEO MMU - RP-owned NEOX path and per-process CWD engine
// ============================================================

struct RPFSProcessCwd {
  bool valid;
  uint8_t device;
  char path[RP_FS_NEOX_CWD_MAX];
};

static RPFSProcessCwd gRPFSProcessCwd[RP_FS_NEOX_MAX_PROCS];

/// <summary>
/// Detects an N:/ prefix in an RP-local NUL-terminated path.
/// </summary>
/// <param name="path">NUL-terminated path.</param>
/// <param name="device">Optional destination for the decoded drive.</param>
/// <returns>true when the path begins with a drive-qualified root.</returns>
static bool rp_fs_path_has_drive_prefix(const char* path, uint8_t* device) {
  if (path == nullptr || path[0] == '\0' || path[1] == '\0' || path[2] == '\0')
    return false;

  if (path[0] < '0' || path[0] > '9' || path[1] != ':' || path[2] != '/')
    return false;

  if (device != nullptr)
    *device = (uint8_t)(path[0] - '0');
  return true;
}

/// <summary>
/// Returns the internal CWD slot for a PID.
/// </summary>
/// <param name="pid">Process ID.</param>
/// <returns>Slot pointer, or nullptr for an invalid PID.</returns>
static RPFSProcessCwd* rp_fs_cwd_slot(uint8_t pid) {
  return pid < RP_FS_NEOX_MAX_PROCS ? &gRPFSProcessCwd[pid] : nullptr;
}

/// <summary>
/// Returns the internal CWD slot for a PID without permitting modification.
/// </summary>
/// <param name="pid">Process ID.</param>
/// <returns>Slot pointer, or nullptr for an invalid PID.</returns>
static const RPFSProcessCwd* rp_fs_cwd_slot_const(uint8_t pid) {
  return pid < RP_FS_NEOX_MAX_PROCS ? &gRPFSProcessCwd[pid] : nullptr;
}

/// <summary>
/// Removes the last canonical component from a path buffer.
/// </summary>
/// <param name="path">Canonical component path without a leading slash.</param>
static void rp_fs_path_pop_component(char* path) {
  if (path == nullptr || path[0] == '\0')
    return;

  char* const slash = strrchr(path, '/');
  if (slash == nullptr)
    path[0] = '\0';
  else
    *slash = '\0';
}

/// <summary>
/// Appends one validated component to a canonical path buffer.
/// </summary>
/// <param name="path">Canonical component path without a leading slash.</param>
/// <param name="path_len">Size of the path buffer.</param>
/// <param name="component">Component bytes.</param>
/// <param name="component_len">Number of component bytes.</param>
/// <returns>true when the component fitted.</returns>
static bool rp_fs_path_append_component(char* path, size_t path_len, const char* component, size_t component_len) {
  if (path == nullptr || path_len == 0 || component == nullptr || component_len == 0)
    return false;

  size_t const current_len = strlen(path);
  size_t const separator_len = current_len == 0 ? 0 : 1;
  if (current_len + separator_len + component_len >= path_len)
    return false;

  size_t offset = current_len;
  if (separator_len != 0)
    path[offset++] = '/';

  memcpy(path + offset, component, component_len);
  path[offset + component_len] = '\0';
  return true;
}

bool rp_fs_path_device_valid(uint8_t device) {
  return device < USB_STORAGE_MAX_DEVICES && device <= 9;
}

bool rp_fs_path_component_valid_83(const char* start, size_t len) {
  if (start == nullptr || len == 0)
    return false;

  uint8_t base_len = 0;
  uint8_t ext_len = 0;
  bool in_ext = false;

  for (size_t i = 0; i < len; i++) {
    char const c = start[i];

    if (c == '/' || c == '\\' || c == ':' || c < 33 || c >= 127)
      return false;

    if (c >= 'a' && c <= 'z')
      return false;

    if (c == '.') {
      if (in_ext || base_len == 0)
        return false;
      in_ext = true;
      continue;
    }

    if (!(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9') && c != '_' && c != '-' && c != '$' && c != '~')
      return false;

    if (in_ext) {
      ext_len++;
      if (ext_len > 3)
        return false;
    }
    else {
      base_len++;
      if (base_len > 8)
        return false;
    }
  }

  return base_len != 0 && (!in_ext || ext_len != 0);
}

bool rp_fs_path_valid_83(const char* path) {
  if (path == nullptr)
    return false;

  const char* logical = path;
  uint8_t qualified_device = 0;
  if (rp_fs_path_has_drive_prefix(path, &qualified_device)) {
    if (!rp_fs_path_device_valid(qualified_device))
      return false;
    logical = path + 3;
  }

  if (logical[0] == '\0')
    return true;

  if (logical[0] == '/') {
    logical++;
    if (logical[0] == '\0')
      return true;
  }

  const char* component = logical;
  size_t component_len = 0;

  for (const char* p = logical; ; p++) {
    char const c = *p;
    if (c == '/' || c == '\0') {
      if (!rp_fs_path_component_valid_83(component, component_len))
        return false;

      if (c == '\0')
        return true;

      component = p + 1;
      component_len = 0;
      if (*component == '\0')
        return false;
      continue;
    }

    component_len++;
  }
}

void rp_fs_cwd_reset_all() {
  for (uint8_t pid = 0; pid < RP_FS_NEOX_MAX_PROCS; pid++) {
    gRPFSProcessCwd[pid].valid = true;
    gRPFSProcessCwd[pid].device = 0;
    gRPFSProcessCwd[pid].path[0] = '\0';
  }
}

bool rp_fs_cwd_init_root(uint8_t pid, uint8_t device) {
  RPFSProcessCwd* const slot = rp_fs_cwd_slot(pid);
  if (slot == nullptr || !rp_fs_path_device_valid(device))
    return false;

  slot->valid = true;
  slot->device = device;
  slot->path[0] = '\0';
  return true;
}

bool rp_fs_cwd_clone(uint8_t child_pid, uint8_t parent_pid) {
  RPFSProcessCwd* const child = rp_fs_cwd_slot(child_pid);
  const RPFSProcessCwd* const parent = rp_fs_cwd_slot_const(parent_pid);
  if (child == nullptr || parent == nullptr || !parent->valid)
    return false;

  *child = *parent;
  return true;
}

bool rp_fs_cwd_get(uint8_t pid, RPFSResolvedPath* cwd) {
  const RPFSProcessCwd* const slot = rp_fs_cwd_slot_const(pid);
  if (slot == nullptr || cwd == nullptr || !slot->valid)
    return false;

  cwd->device = slot->device;
  if (slot->path[0] == '\0') {
    cwd->path[0] = '/';
    cwd->path[1] = '\0';
  }
  else {
    size_t const len = strlen(slot->path);
    memcpy(cwd->path, slot->path, len + 1);
  }
  return true;
}

bool rp_fs_cwd_set(uint8_t pid, const RPFSResolvedPath* cwd) {
  RPFSProcessCwd* const slot = rp_fs_cwd_slot(pid);
  if (slot == nullptr || cwd == nullptr || !rp_fs_path_device_valid(cwd->device))
    return false;

  uint8_t qualified_device = 0;
  if (rp_fs_path_has_drive_prefix(cwd->path, &qualified_device))
    return false;

  if (!rp_fs_path_valid_83(cwd->path))
    return false;

  const char* source = cwd->path;
  if (source[0] == '/')
    source++;

  size_t const len = strlen(source);
  if (len >= RP_FS_NEOX_CWD_MAX)
    return false;

  slot->valid = true;
  slot->device = cwd->device;
  memcpy(slot->path, source, len + 1);
  return true;
}

bool rp_fs_cwd_format(uint8_t pid, char* out, size_t out_len, size_t* result_len) {
  const RPFSProcessCwd* const slot = rp_fs_cwd_slot_const(pid);
  if (slot == nullptr || out == nullptr || !slot->valid || !rp_fs_path_device_valid(slot->device))
    return false;

  size_t const cwd_len = strlen(slot->path);
  size_t const visible_len = 3 + cwd_len;
  if (visible_len + 1 > out_len)
    return false;

  out[0] = (char)('0' + slot->device);
  out[1] = ':';
  out[2] = '/';
  memcpy(out + 3, slot->path, cwd_len + 1);

  if (result_len != nullptr)
    *result_len = visible_len;
  return true;
}

rp_fs_path_result_t rp_fs_resolve_process_path(uint8_t pid, uint8_t fallback_device, const char* input, RPFSResolvedPath* resolved) {
  if (pid >= RP_FS_NEOX_MAX_PROCS || resolved == nullptr)
    return RP_FS_PATH_INVALID_PID;

  if (input == nullptr || input[0] == '\0')
    return RP_FS_PATH_INVALID_PATH;

  RPFSProcessCwd const* const cwd = rp_fs_cwd_slot_const(pid);
  uint8_t device = fallback_device;
  const char* cursor = input;
  char canonical[RP_FS_NEOX_PATH_MAX]{};

  uint8_t explicit_device = 0;
  if (rp_fs_path_has_drive_prefix(input, &explicit_device)) {
    device = explicit_device;
    cursor = input + 3;
  }
  else if (input[0] == '/') {
    if (cwd != nullptr && cwd->valid)
      device = cwd->device;
    cursor = input + 1;
  }
  else if (cwd != nullptr && cwd->valid) {
    device = cwd->device;
    size_t const cwd_len = strlen(cwd->path);
    if (cwd_len >= sizeof(canonical))
      return RP_FS_PATH_TOO_LONG;
    memcpy(canonical, cwd->path, cwd_len + 1);
  }

  if (!rp_fs_path_device_valid(device))
    return RP_FS_PATH_INVALID_DEVICE;

  while (*cursor != '\0') {
    while (*cursor == '/')
      cursor++;

    if (*cursor == '\0')
      break;

    const char* const component = cursor;
    while (*cursor != '\0' && *cursor != '/')
      cursor++;

    size_t const component_len = (size_t)(cursor - component);
    if (component_len == 1 && component[0] == '.')
      continue;

    if (component_len == 2 && component[0] == '.' && component[1] == '.') {
      rp_fs_path_pop_component(canonical);
      continue;
    }

    if (!rp_fs_path_component_valid_83(component, component_len))
      return RP_FS_PATH_INVALID_PATH;

    if (!rp_fs_path_append_component(canonical, sizeof(canonical), component, component_len))
      return RP_FS_PATH_TOO_LONG;
  }

  resolved->device = device;
  if (canonical[0] == '\0') {
    resolved->path[0] = '/';
    resolved->path[1] = '\0';
  }
  else {
    size_t const len = strlen(canonical);
    memcpy(resolved->path, canonical, len + 1);
  }

  return RP_FS_PATH_OK;
}
