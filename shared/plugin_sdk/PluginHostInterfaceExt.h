#pragma once
/**
 * @file PluginHostInterfaceExt.h
 * @brief Host SDK Extension Functions (internal use)
 *
 * Functions not yet in the public PluginHostInterface.h API,
 * needed by the Qt host wrapper (IpcClient) and the Launcher.
 */

#include "PluginHostInterface.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Update the host-side heartbeat timestamp (call every ~500 ms). */
void PluginHost_UpdateHostHeartbeat(PluginHostHandle* handle);

/** Block until the plugin writes its PID into shared memory. */
bool PluginHost_WaitForConnection(PluginHostHandle* handle, int timeoutMs);

/** Send a Resize IPC command to the plugin. */
bool PluginHost_SendResize(PluginHostHandle* handle, int x, int y, int w, int h);

/** Send a Shutdown IPC command to the plugin. */
bool PluginHost_SendShutdown(PluginHostHandle* handle);

/** Return the plugin PID from shared memory (0 = not yet connected). */
uint32_t PluginHost_GetPluginPid(PluginHostHandle* handle);

/**
 * Return the last plugin heartbeat timestamp in milliseconds
 * (same epoch as PluginHost_NowMs).  Returns 0 before first heartbeat.
 */
uint64_t PluginHost_GetPluginHeartbeat(PluginHostHandle* handle);

/**
 * Return current time in milliseconds (Windows FILETIME epoch: 100ns
 * units since 1601 → divided by 10 000).  Consistent with both the
 * host and plugin heartbeat timestamps.
 */
uint64_t PluginHost_NowMs(void);

// ── Multi-camera slot API ─────────────────────────────────────────────────────

/**
 * Per-slot command request (used by PluginHost_SendCommandSlot).
 * slot_index selects which camera slot (0..MAX_CAM_SLOTS-1).
 * request_id is stored in CamSlot::cmd_id for correlation in results.
 */
typedef struct {
    int             slot_index;
    uint32_t        request_id;
    PluginHostImage image;
    const char*     params_json;
} PluginHostSlotRequest;

/** Set the number of active camera slots (1..MAX_CAM_SLOTS). Must be called
 *  before the plugin process is launched so the plugin reads the correct
 *  cam_count from the shared memory header. */
void PluginHost_SetCamCount(PluginHostHandle* handle, int count);

/** Return the number of active camera slots configured on this handle. */
int  PluginHost_GetCamCount(PluginHostHandle* handle);

/** Send a command to a specific camera slot. */
bool PluginHost_SendCommandSlot(PluginHostHandle* handle,
                                const PluginHostSlotRequest* req);

/** Wait for a result from a specific camera slot. */
bool PluginHost_WaitResultSlot(PluginHostHandle* handle, int slotIndex,
                               PluginHostResult* result, int timeoutMs);

/** Poll (non-blocking or timed) for a result from a specific slot. */
bool PluginHost_PollResultSlot(PluginHostHandle* handle, int slotIndex,
                               PluginHostResult* result, int timeoutMs);

#ifdef __cplusplus
}
#endif
