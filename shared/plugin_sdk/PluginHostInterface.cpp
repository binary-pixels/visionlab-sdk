/**
 * @file PluginHostInterface.cpp
 * @brief Host-side Plugin SDK Implementation
 *
 * Implements PluginHostInterface.h + PluginHostInterfaceExt.h using
 * Windows named shared memory and auto-reset events from SharedMemLayout.h.
 *
 * Thread-safety: all public functions that touch shared memory are protected
 * by a CRITICAL_SECTION embedded in PluginHostHandle.
 * Exception: PluginHost_UpdateHostHeartbeat() does a single aligned 64-bit
 * store which is atomic on x86-64 – no lock needed.
 */

#include "PluginHostInterface.h"
#include "PluginHostInterfaceExt.h"

// SharedMemLayout.h lives in the SDK directory (self-contained).
#include "SharedMemLayout.h"

// ============================================================
// Windows implementation
// ============================================================
#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdarg>

// ── Internal handle structures ───────────────────────────────────────────────

struct SlotState {
    bool resultPending = false;
    char statusBuf[32]    = {};
    char dataBuf[2048]    = {};   // matches CamSlot::result_data capacity
    char errorMsgBuf[512] = {};
    PluginHostResult lastResult = {};
};

struct PluginHostHandle {
    HANDLE   hCmdFile = nullptr;
    HANDLE   hImgFile = nullptr;
    HANDLE   hH2P[MAX_CAM_SLOTS] = {};   // Host → Plugin, per slot
    HANDLE   hP2H[MAX_CAM_SLOTS] = {};   // Plugin → Host, per slot
    void*    cmdBase  = nullptr;
    uint8_t* imgBase  = nullptr;

    char     shmName[128] = {};
    int      camCount = 1;
    uint32_t cmdSeq[MAX_CAM_SLOTS] = {};

    CRITICAL_SECTION cs;
    SlotState slots[MAX_CAM_SLOTS];

    // Diagnostics
    int   totalCommands = 0;
    DWORD lastCmdMs     = 0;
    DWORD lastResultMs  = 0;
    char  lastError[512] = {};
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static uint64_t FiletimeNowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart / 10000ULL;   // 100-ns → ms
}

static void SetErr(PluginHostHandle* h, const char* fmt, ...) {
    if (!h) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(h->lastError, sizeof(h->lastError), fmt, ap);
    va_end(ap);
}

static void ToWide(const char* src, wchar_t* dst, int dstLen) {
    MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, dstLen);
}

// ── Version ──────────────────────────────────────────────────────────────────

const char* PluginHost_GetVersion(void)    { return PLUGIN_HOST_SDK_VERSION; }
int         PluginHost_GetAPIVersion(void) { return PLUGIN_HOST_API_VERSION; }

// ── Create / Destroy ─────────────────────────────────────────────────────────

PluginHostHandle* PluginHost_Create(const char* shmName) {
    if (!shmName || shmName[0] == '\0' || strlen(shmName) > 64)
        return nullptr;

    auto* h = new PluginHostHandle{};
    InitializeCriticalSection(&h->cs);
    strncpy(h->shmName, shmName, sizeof(h->shmName) - 1);

    char nbuf[256];
    wchar_t wcmd[256], wimg[256];

    snprintf(nbuf, sizeof(nbuf), "SharedMem_Cmd_%s", shmName);
    ToWide(nbuf, wcmd, 256);
    snprintf(nbuf, sizeof(nbuf), "SharedMem_Img_%s", shmName);
    ToWide(nbuf, wimg, 256);

    h->hCmdFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE, 0,
                                     static_cast<DWORD>(CMD_BLOCK_SIZE), wcmd);
    if (!h->hCmdFile) {
        SetErr(h, "CreateFileMapping(cmd) failed: %lu", GetLastError());
        goto fail;
    }

    h->hImgFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                     PAGE_READWRITE,
                                     static_cast<DWORD>(IMG_BLOCK_SIZE >> 32),
                                     static_cast<DWORD>(IMG_BLOCK_SIZE & 0xFFFFFFFFu),
                                     wimg);
    if (!h->hImgFile) {
        SetErr(h, "CreateFileMapping(img) failed: %lu", GetLastError());
        goto fail;
    }

    h->cmdBase = MapViewOfFile(h->hCmdFile, FILE_MAP_ALL_ACCESS, 0, 0, CMD_BLOCK_SIZE);
    h->imgBase = static_cast<uint8_t*>(
        MapViewOfFile(h->hImgFile, FILE_MAP_ALL_ACCESS, 0, 0, IMG_BLOCK_SIZE));

    if (!h->cmdBase || !h->imgBase) {
        SetErr(h, "MapViewOfFile failed: %lu", GetLastError());
        goto fail;
    }

    {
        // Initialise the MultiCamHeader
        memset(h->cmdBase, 0, CMD_BLOCK_SIZE);
        MultiCamHeader* hdr = getCmdHeader(h->cmdBase);
        hdr->magic     = SHM_MAGIC;
        hdr->version   = SHM_VERSION;
        hdr->host_pid  = static_cast<uint32_t>(GetCurrentProcessId());
        hdr->cam_count = static_cast<uint32_t>(h->camCount);
    }

    // Create per-slot auto-reset events (initially non-signalled)
    for (int i = 0; i < MAX_CAM_SLOTS; ++i) {
        wchar_t wh2p[256], wp2h[256];
        snprintf(nbuf, sizeof(nbuf), "Event_H2P_%s_%d", shmName, i);
        ToWide(nbuf, wh2p, 256);
        snprintf(nbuf, sizeof(nbuf), "Event_P2H_%s_%d", shmName, i);
        ToWide(nbuf, wp2h, 256);
        h->hH2P[i] = CreateEventW(nullptr, FALSE, FALSE, wh2p);
        h->hP2H[i] = CreateEventW(nullptr, FALSE, FALSE, wp2h);
        if (!h->hH2P[i] || !h->hP2H[i]) {
            SetErr(h, "CreateEvent failed for slot %d: %lu", i, GetLastError());
            goto fail;
        }
    }

    return h;

fail:
    for (int i = 0; i < MAX_CAM_SLOTS; ++i) {
        if (h->hH2P[i]) CloseHandle(h->hH2P[i]);
        if (h->hP2H[i]) CloseHandle(h->hP2H[i]);
    }
    if (h->cmdBase) UnmapViewOfFile(h->cmdBase);
    if (h->imgBase) UnmapViewOfFile(h->imgBase);
    if (h->hCmdFile) CloseHandle(h->hCmdFile);
    if (h->hImgFile) CloseHandle(h->hImgFile);
    DeleteCriticalSection(&h->cs);
    delete h;
    return nullptr;
}

void PluginHost_Destroy(PluginHostHandle* h) {
    if (!h) return;
    for (int i = 0; i < MAX_CAM_SLOTS; ++i) {
        if (h->hH2P[i]) CloseHandle(h->hH2P[i]);
        if (h->hP2H[i]) CloseHandle(h->hP2H[i]);
    }
    if (h->cmdBase) UnmapViewOfFile(h->cmdBase);
    if (h->imgBase) UnmapViewOfFile(h->imgBase);
    if (h->hCmdFile) CloseHandle(h->hCmdFile);
    if (h->hImgFile) CloseHandle(h->hImgFile);
    DeleteCriticalSection(&h->cs);
    delete h;
}

// ── Send command helpers ──────────────────────────────────────────────────────

// Internal: write image (optional) + params into a specific slot, then signal.
// Caller must hold h->cs.
static bool WriteAndSignalSlot(PluginHostHandle* h, int slotIdx,
                                const PluginHostImage* image,
                                const char* paramsJson,
                                CmdType cmdType,
                                uint32_t requestId = 0) {
    if (!h->cmdBase || !h->imgBase) {
        SetErr(h, "Shared memory not initialised");
        return false;
    }
    if (slotIdx < 0 || slotIdx >= h->camCount) {
        SetErr(h, "Invalid slot index: %d (camCount=%d)", slotIdx, h->camCount);
        return false;
    }

    CamSlot* slot    = getCamSlot(h->cmdBase, slotIdx);
    uint8_t* imgSlot = getImgSlot(h->imgBase, slotIdx);

    // Write image (optional — nullptr means params-only command)
    if (image && image->pixels) {
        if (image->width <= 0 || image->height <= 0 || image->channels <= 0) {
            SetErr(h, "Invalid image dimensions");
            return false;
        }
        uint32_t pixelBytes = static_cast<uint32_t>(
            image->width * image->height * image->channels);
        if (pixelBytes > IMG_SLOT_SIZE - sizeof(ImageHeader)) {
            SetErr(h, "Image too large: %u bytes (max %zu)",
                   pixelBytes, IMG_SLOT_SIZE - sizeof(ImageHeader));
            return false;
        }
        ImageHeader* hdr = reinterpret_cast<ImageHeader*>(imgSlot);
        hdr->width     = static_cast<uint32_t>(image->width);
        hdr->height    = static_cast<uint32_t>(image->height);
        hdr->channels  = static_cast<uint32_t>(image->channels);
        hdr->stride    = static_cast<uint32_t>(image->width * image->channels);
        hdr->data_size = pixelBytes;
        memcpy(imgSlot + sizeof(ImageHeader), image->pixels, pixelBytes);
    } else {
        memset(imgSlot, 0, sizeof(ImageHeader));
    }

    // Validate and write params
    if (!paramsJson) paramsJson = "{}";
    size_t paramsLen = strlen(paramsJson);
    if (paramsLen >= sizeof(slot->params_data)) {
        SetErr(h, "params_json too large: %zu bytes", paramsLen);
        return false;
    }
    memcpy(slot->params_data, paramsJson, paramsLen + 1);
    slot->params_len = static_cast<uint32_t>(paramsLen);

    // Write command header
    slot->cmd_id     = requestId ? requestId : ++h->cmdSeq[slotIdx];
    slot->cmd_type   = static_cast<uint32_t>(cmdType);
    slot->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    slot->timeout_ms = 0;

    SetEvent(h->hH2P[slotIdx]);
    h->lastCmdMs = GetTickCount();
    ++h->totalCommands;
    h->slots[slotIdx].resultPending = true;
    return true;
}

bool PluginHost_SendCommand(PluginHostHandle* h,
                             const PluginHostImage* image,
                             const char* paramsJson) {
    if (!h) return false;
    EnterCriticalSection(&h->cs);
    CmdType type = (image && image->pixels) ? CmdType::PushAndSearch : CmdType::GetParams;
    bool ok = WriteAndSignalSlot(h, 0, image, paramsJson, type);
    LeaveCriticalSection(&h->cs);
    return ok;
}

bool PluginHost_SendAsyncCommand(PluginHostHandle* h,
                                  const PluginHostAsyncRequest* req) {
    if (!h || !req) return false;
    EnterCriticalSection(&h->cs);
    CmdType type = req->image.pixels ? CmdType::PushAndSearch : CmdType::GetParams;
    bool ok = WriteAndSignalSlot(h, 0, &req->image, req->params_json, type,
                                 static_cast<uint32_t>(req->request_id));
    LeaveCriticalSection(&h->cs);
    return ok;
}

// ── Wait / Poll result ───────────────────────────────────────────────────────

// Parse result JSON from a CamSlot into a SlotState.
static void ParseResultIntoSlot(SlotState* s, const CamSlot* slot) {
    uint32_t rlen = slot->result_len;
    if (rlen > 0 && rlen < sizeof(s->dataBuf)) {
        memcpy(s->dataBuf, slot->result_data, rlen);
        s->dataBuf[rlen] = '\0';
    } else {
        s->dataBuf[0] = '\0';
    }

    if (strstr(s->dataBuf, "\"ok\"")) {
        strncpy(s->statusBuf, "ok", sizeof(s->statusBuf));
        s->errorMsgBuf[0] = '\0';
        s->lastResult.error_code = 0;
    } else {
        strncpy(s->statusBuf, "error", sizeof(s->statusBuf));
        s->errorMsgBuf[0] = '\0';
        const char* em = strstr(s->dataBuf, "\"error_message\"");
        if (em) {
            const char* q1 = strchr(em + 15, '"');
            if (q1) { q1++;
                const char* q2 = strchr(q1, '"');
                if (q2) {
                    size_t n = static_cast<size_t>(q2 - q1);
                    if (n < sizeof(s->errorMsgBuf)) {
                        memcpy(s->errorMsgBuf, q1, n);
                        s->errorMsgBuf[n] = '\0';
                    }
                }
            }
        }
        if (s->errorMsgBuf[0] == '\0')
            strncpy(s->errorMsgBuf, "Unknown error", sizeof(s->errorMsgBuf));
        s->lastResult.error_code = -1;
    }

    s->lastResult.request_id    = static_cast<int>(slot->cmd_id);
    s->lastResult.status        = s->statusBuf;
    s->lastResult.data          = s->dataBuf;
    s->lastResult.error_message = s->errorMsgBuf;
}

bool PluginHost_WaitResultSlot(PluginHostHandle* h, int slotIdx,
                                PluginHostResult* result, int timeoutMs) {
    if (!h || !result || slotIdx < 0 || slotIdx >= h->camCount) return false;

    DWORD timeout = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    DWORD ret = WaitForSingleObject(h->hP2H[slotIdx], timeout);
    if (ret != WAIT_OBJECT_0) {
        if (ret == WAIT_TIMEOUT)
            SetErr(h, "WaitResult slot %d timed out after %dms", slotIdx, timeoutMs);
        else
            SetErr(h, "WaitForSingleObject error (slot %d): %lu", slotIdx, GetLastError());
        return false;
    }

    EnterCriticalSection(&h->cs);
    CamSlot* slot = getCamSlot(h->cmdBase, slotIdx);
    ParseResultIntoSlot(&h->slots[slotIdx], slot);
    *result = h->slots[slotIdx].lastResult;
    h->slots[slotIdx].resultPending = false;
    h->lastResultMs = GetTickCount();
    LeaveCriticalSection(&h->cs);
    return true;
}

bool PluginHost_WaitResult(PluginHostHandle* h,
                            PluginHostResult* result,
                            int timeoutMs) {
    return PluginHost_WaitResultSlot(h, 0, result, timeoutMs);
}

bool PluginHost_PollResultSlot(PluginHostHandle* h, int slotIdx,
                                PluginHostResult* result, int timeoutMs) {
    return PluginHost_WaitResultSlot(h, slotIdx, result, timeoutMs);
}

bool PluginHost_PollResult(PluginHostHandle* h,
                            PluginHostResult* result,
                            int timeoutMs) {
    return PluginHost_WaitResultSlot(h, 0, result, timeoutMs);
}

bool PluginHost_SendAndWait(PluginHostHandle* h,
                             const PluginHostImage* image,
                             const char* paramsJson,
                             PluginHostResult* result,
                             int timeoutMs) {
    if (!PluginHost_SendCommand(h, image, paramsJson)) return false;
    return PluginHost_WaitResultSlot(h, 0, result, timeoutMs);
}

// ── Slot API ──────────────────────────────────────────────────────────────────

void PluginHost_SetCamCount(PluginHostHandle* h, int count) {
    if (!h || count < 1 || count > MAX_CAM_SLOTS) return;
    h->camCount = count;
    if (h->cmdBase)
        getCmdHeader(h->cmdBase)->cam_count = static_cast<uint32_t>(count);
}

int PluginHost_GetCamCount(PluginHostHandle* h) {
    return h ? h->camCount : 0;
}

bool PluginHost_SendCommandSlot(PluginHostHandle* h,
                                 const PluginHostSlotRequest* req) {
    if (!h || !req) return false;
    if (req->slot_index < 0 || req->slot_index >= h->camCount) {
        SetErr(h, "Invalid slot_index: %d", req->slot_index);
        return false;
    }
    EnterCriticalSection(&h->cs);
    CmdType type = req->image.pixels ? CmdType::PushAndSearch : CmdType::GetParams;
    bool ok = WriteAndSignalSlot(h, req->slot_index, &req->image,
                                 req->params_json, type,
                                 static_cast<uint32_t>(req->request_id));
    LeaveCriticalSection(&h->cs);
    return ok;
}

// ── Status / Info ────────────────────────────────────────────────────────────

int PluginHost_GetPendingResultCount(PluginHostHandle* h) {
    if (!h) return 0;
    int count = 0;
    for (int i = 0; i < h->camCount; ++i)
        if (h->slots[i].resultPending) ++count;
    return count;
}

bool PluginHost_IsPluginConnected(PluginHostHandle* h) {
    if (!h || !h->cmdBase) return false;
    MultiCamHeader* hdr = getCmdHeader(h->cmdBase);
    if (hdr->plugin_heartbeat == 0)
        return hdr->plugin_pid != 0;
    uint64_t now = FiletimeNowMs();
    uint64_t hb  = hdr->plugin_heartbeat;
    return (now <= hb || (now - hb) < 5000);
}

const char* PluginHost_GetShmName(PluginHostHandle* h) {
    return h ? h->shmName : "";
}

const char* PluginHost_GetLastError(PluginHostHandle* h) {
    return h ? h->lastError : "Invalid handle";
}

void PluginHost_GetStats(PluginHostHandle* h,
                          int* lastCmdMs, int* lastResultMs, int* totalCmds) {
    if (!h) return;
    if (lastCmdMs)    *lastCmdMs    = static_cast<int>(h->lastCmdMs);
    if (lastResultMs) *lastResultMs = static_cast<int>(h->lastResultMs);
    if (totalCmds)    *totalCmds    = h->totalCommands;
}

// ── Extension functions (PluginHostInterfaceExt.h) ───────────────────────────

void PluginHost_UpdateHostHeartbeat(PluginHostHandle* h) {
    if (h && h->cmdBase)
        getCmdHeader(h->cmdBase)->host_heartbeat = FiletimeNowMs();
}

bool PluginHost_WaitForConnection(PluginHostHandle* h, int timeoutMs) {
    if (!h || !h->cmdBase) return false;
    DWORD start    = GetTickCount();
    DWORD deadline = start + static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 10000);
    while (GetTickCount() < deadline) {
        if (getCmdHeader(h->cmdBase)->plugin_pid != 0) return true;
        Sleep(50);
    }
    SetErr(h, "Plugin did not connect within %dms", timeoutMs);
    return false;
}

bool PluginHost_SendResize(PluginHostHandle* h, int x, int y, int w, int hv) {
    if (!h || w <= 0 || hv <= 0) return false;
    EnterCriticalSection(&h->cs);
    char params[128];
    snprintf(params, sizeof(params),
             "{\"x\":%d,\"y\":%d,\"w\":%d,\"h\":%d}", x, y, w, hv);
    // Resize/UI commands always go to slot 0
    CamSlot* slot = getCamSlot(h->cmdBase, 0);
    uint32_t plen = static_cast<uint32_t>(strlen(params));
    memcpy(slot->params_data, params, plen + 1);
    slot->params_len = plen;
    slot->cmd_id     = ++h->cmdSeq[0];
    slot->cmd_type   = static_cast<uint32_t>(CmdType::Resize);
    slot->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    SetEvent(h->hH2P[0]);
    LeaveCriticalSection(&h->cs);
    return true;
}

bool PluginHost_SendShutdown(PluginHostHandle* h) {
    if (!h) return false;
    EnterCriticalSection(&h->cs);
    CamSlot* slot = getCamSlot(h->cmdBase, 0);
    slot->params_len = 0;
    slot->cmd_id     = ++h->cmdSeq[0];
    slot->cmd_type   = static_cast<uint32_t>(CmdType::Shutdown);
    slot->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    SetEvent(h->hH2P[0]);
    LeaveCriticalSection(&h->cs);
    return true;
}

uint32_t PluginHost_GetPluginPid(PluginHostHandle* h) {
    return (h && h->cmdBase) ? getCmdHeader(h->cmdBase)->plugin_pid : 0;
}

uint64_t PluginHost_GetPluginHeartbeat(PluginHostHandle* h) {
    return (h && h->cmdBase) ? getCmdHeader(h->cmdBase)->plugin_heartbeat : 0;
}

uint64_t PluginHost_NowMs() {
    return FiletimeNowMs();
}

// ============================================================
// Non-Windows stubs
// ============================================================
#else

PluginHostHandle* PluginHost_Create(const char*)                                   { return nullptr; }
void              PluginHost_Destroy(PluginHostHandle*)                            {}
bool PluginHost_SendCommand(PluginHostHandle*, const PluginHostImage*, const char*){ return false; }
bool PluginHost_SendAsyncCommand(PluginHostHandle*, const PluginHostAsyncRequest*) { return false; }
bool PluginHost_WaitResult(PluginHostHandle*, PluginHostResult*, int)              { return false; }
bool PluginHost_PollResult(PluginHostHandle*, PluginHostResult*, int)              { return false; }
bool PluginHost_SendAndWait(PluginHostHandle*, const PluginHostImage*,
                             const char*, PluginHostResult*, int)                  { return false; }
int  PluginHost_GetPendingResultCount(PluginHostHandle*)                           { return 0;     }
bool PluginHost_IsPluginConnected(PluginHostHandle*)                               { return false; }
const char* PluginHost_GetShmName(PluginHostHandle*)                               { return "";    }
const char* PluginHost_GetLastError(PluginHostHandle*)                             { return "Platform not supported"; }
void PluginHost_GetStats(PluginHostHandle*, int*, int*, int*)                      {}
void PluginHost_UpdateHostHeartbeat(PluginHostHandle*)                             {}
bool PluginHost_WaitForConnection(PluginHostHandle*, int)                          { return false; }
bool PluginHost_SendResize(PluginHostHandle*, int, int, int, int)                  { return false; }
bool PluginHost_SendShutdown(PluginHostHandle*)                                    { return false; }
uint32_t PluginHost_GetPluginPid(PluginHostHandle*)                                { return 0;     }
uint64_t PluginHost_GetPluginHeartbeat(PluginHostHandle*)                          { return 0;     }
uint64_t PluginHost_NowMs()                                                        { return 0;     }
void PluginHost_SetCamCount(PluginHostHandle*, int)                                {}
int  PluginHost_GetCamCount(PluginHostHandle*)                                     { return 0;     }
bool PluginHost_SendCommandSlot(PluginHostHandle*, const PluginHostSlotRequest*)   { return false; }
bool PluginHost_WaitResultSlot(PluginHostHandle*, int, PluginHostResult*, int)     { return false; }
bool PluginHost_PollResultSlot(PluginHostHandle*, int, PluginHostResult*, int)     { return false; }

#endif // _WIN32
