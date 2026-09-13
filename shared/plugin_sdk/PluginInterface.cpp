/**
 * @file PluginInterface.cpp
 * @brief Plugin-side SDK implementation (pure C, no Qt dependency)
 *
 * Used by the plugin executable (e.g. CircleFitTester.exe) to connect
 * to the host and participate in the shared-memory IPC protocol.
 *
 * Counterpart to PluginHostInterface.cpp (host side).
 *
 * Protocol summary (from SharedMemLayout.h V3):
 *  - Host CREATES: SharedMem_Cmd_{name}, SharedMem_Img_{name},
 *                  Event_H2P_{name}_0..3 (host→plugin per slot),
 *                  Event_P2H_{name}_0..3 (plugin→host per slot)
 *  - Plugin OPENS all objects (they already exist when plugin starts).
 *  - Plugin writes plugin_pid into MultiCamHeader to signal "connected".
 *  - Plugin keeps plugin_heartbeat updated (~500 ms).
 *  - Plugin calls Plugin_WaitCommandSlot(), reads cmd from CamSlot,
 *    writes result back, signals Event_P2H_{name}_{slot}.
 */

#include "PluginInterface.h"

#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <cstdio>
#include <cstdint>

// Internal protocol layout (shared with host side)
#include "SharedMemLayout.h"

// ── Internal handle ───────────────────────────────────────────────────────────

struct PluginHandle {
    HANDLE   hCmdMap = nullptr;
    HANDLE   hImgMap = nullptr;
    HANDLE   hH2P[MAX_CAM_SLOTS] = {};   // host → plugin events, per slot
    HANDLE   hP2H[MAX_CAM_SLOTS] = {};   // plugin → host events, per slot
    void*    cmdBase = nullptr;
    uint8_t* imgBase = nullptr;

    int  camCount    = 1;
    int  pendingSlot = -1;   // -1 = no pending command
    char lastError[512] = {};
};

// ── Helpers ───────────────────────────────────────────────────────────────────

static void SetErr(PluginHandle* h, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(h->lastError, sizeof(h->lastError) - 1, fmt, ap);
    va_end(ap);
}

static uint64_t FiletimeNowMs() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    return ui.QuadPart / 10000ULL;
}

// ── Public API ────────────────────────────────────────────────────────────────

PluginHandle* Plugin_Create(const char* shmName) {
    if (!shmName || shmName[0] == '\0') return nullptr;

    char buf[256];
    wchar_t wcmd[256], wimg[256];

    snprintf(buf, sizeof(buf), "SharedMem_Cmd_%s", shmName);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wcmd, 256);
    snprintf(buf, sizeof(buf), "SharedMem_Img_%s", shmName);
    MultiByteToWideChar(CP_UTF8, 0, buf, -1, wimg, 256);

    auto* h = new PluginHandle{};

    h->hCmdMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wcmd);
    if (!h->hCmdMap) {
        SetErr(h, "OpenFileMapping(cmd) failed: %lu", GetLastError());
        delete h;
        return nullptr;
    }

    h->hImgMap = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, wimg);
    if (!h->hImgMap) {
        SetErr(h, "OpenFileMapping(img) failed: %lu", GetLastError());
        CloseHandle(h->hCmdMap);
        delete h;
        return nullptr;
    }

    h->cmdBase = MapViewOfFile(h->hCmdMap, FILE_MAP_ALL_ACCESS, 0, 0, CMD_BLOCK_SIZE);
    h->imgBase = static_cast<uint8_t*>(
        MapViewOfFile(h->hImgMap, FILE_MAP_ALL_ACCESS, 0, 0, IMG_BLOCK_SIZE));

    if (!h->cmdBase || !h->imgBase) {
        SetErr(h, "MapViewOfFile failed: %lu", GetLastError());
        if (h->cmdBase) UnmapViewOfFile(h->cmdBase);
        if (h->imgBase) UnmapViewOfFile(h->imgBase);
        CloseHandle(h->hCmdMap);
        CloseHandle(h->hImgMap);
        delete h;
        return nullptr;
    }

    // Validate magic
    MultiCamHeader* hdr = getCmdHeader(h->cmdBase);
    if (hdr->magic != SHM_MAGIC) {
        SetErr(h, "Invalid shared memory magic: 0x%08X (expected 0x%08X)",
               hdr->magic, SHM_MAGIC);
        UnmapViewOfFile(h->cmdBase);
        UnmapViewOfFile(h->imgBase);
        CloseHandle(h->hCmdMap);
        CloseHandle(h->hImgMap);
        delete h;
        return nullptr;
    }

    // Read cam_count from header (set by host before launching plugin)
    h->camCount = static_cast<int>(hdr->cam_count);
    if (h->camCount < 1 || h->camCount > MAX_CAM_SLOTS) h->camCount = 1;

    // Open all MAX_CAM_SLOTS event pairs (host always creates all of them)
    for (int i = 0; i < MAX_CAM_SLOTS; ++i) {
        wchar_t wh2p[256], wp2h[256];
        snprintf(buf, sizeof(buf), "Event_H2P_%s_%d", shmName, i);
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wh2p, 256);
        snprintf(buf, sizeof(buf), "Event_P2H_%s_%d", shmName, i);
        MultiByteToWideChar(CP_UTF8, 0, buf, -1, wp2h, 256);

        h->hH2P[i] = OpenEventW(SYNCHRONIZE, FALSE, wh2p);
        h->hP2H[i] = OpenEventW(EVENT_MODIFY_STATE, FALSE, wp2h);
        if (!h->hH2P[i] || !h->hP2H[i]) {
            SetErr(h, "OpenEvent failed for slot %d: %lu", i, GetLastError());
            for (int j = 0; j <= i; ++j) {
                if (h->hH2P[j]) CloseHandle(h->hH2P[j]);
                if (h->hP2H[j]) CloseHandle(h->hP2H[j]);
            }
            UnmapViewOfFile(h->cmdBase);
            UnmapViewOfFile(h->imgBase);
            CloseHandle(h->hCmdMap);
            CloseHandle(h->hImgMap);
            delete h;
            return nullptr;
        }
    }

    // Announce presence and write first heartbeat
    hdr->plugin_pid       = static_cast<uint32_t>(GetCurrentProcessId());
    hdr->plugin_heartbeat = FiletimeNowMs();

    return h;
}

void Plugin_Destroy(PluginHandle* h) {
    if (!h) return;
    for (int i = 0; i < MAX_CAM_SLOTS; ++i) {
        if (h->hH2P[i]) CloseHandle(h->hH2P[i]);
        if (h->hP2H[i]) CloseHandle(h->hP2H[i]);
    }
    if (h->cmdBase) UnmapViewOfFile(h->cmdBase);
    if (h->imgBase) UnmapViewOfFile(h->imgBase);
    if (h->hCmdMap) CloseHandle(h->hCmdMap);
    if (h->hImgMap) CloseHandle(h->hImgMap);
    delete h;
}

bool Plugin_WaitCommand(PluginHandle* h, int timeoutMs) {
    if (!h || !h->hH2P[0]) return false;

    getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();

    DWORD t = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    DWORD ret = WaitForSingleObject(h->hH2P[0], t);

    getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();

    if (ret != WAIT_OBJECT_0) return false;

    CamSlot* slot = getCamSlot(h->cmdBase, 0);
    if (static_cast<CmdType>(slot->cmd_type) == CmdType::None) return false;

    h->pendingSlot = 0;
    return true;
}

int Plugin_WaitCommandSlot(PluginHandle* h, int timeoutMs) {
    if (!h || h->camCount < 1) return -1;

    getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();

    HANDLE events[MAX_CAM_SLOTS];
    for (int i = 0; i < h->camCount; ++i)
        events[i] = h->hH2P[i];

    DWORD t = (timeoutMs < 0) ? INFINITE : static_cast<DWORD>(timeoutMs);
    DWORD ret = WaitForMultipleObjects(static_cast<DWORD>(h->camCount),
                                       events, FALSE, t);

    getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();

    if (ret >= WAIT_OBJECT_0 &&
        ret < WAIT_OBJECT_0 + static_cast<DWORD>(h->camCount)) {
        int idx = static_cast<int>(ret - WAIT_OBJECT_0);
        CamSlot* slot = getCamSlot(h->cmdBase, idx);
        if (static_cast<CmdType>(slot->cmd_type) == CmdType::None) return -1;
        h->pendingSlot = idx;
        return idx;
    }
    return -1;
}

bool Plugin_GetImageSlot(PluginHandle* h, int slotIdx, ImageData* out) {
    if (!h || !out || slotIdx < 0 || slotIdx >= h->camCount) return false;

    uint8_t* imgSlot = getImgSlot(h->imgBase, slotIdx);
    const ImageHeader* hdr = reinterpret_cast<const ImageHeader*>(imgSlot);
    if (hdr->width == 0 || hdr->height == 0 || hdr->data_size == 0) {
        SetErr(h, "No image data in slot %d", slotIdx);
        return false;
    }

    out->pixels   = imgSlot + sizeof(ImageHeader);
    out->width    = static_cast<int>(hdr->width);
    out->height   = static_cast<int>(hdr->height);
    out->channels = static_cast<int>(hdr->channels);
    return true;
}

bool Plugin_GetImage(PluginHandle* h, ImageData* out) {
    if (!h || !out) return false;
    if (h->pendingSlot < 0) { SetErr(h, "No command pending"); return false; }
    return Plugin_GetImageSlot(h, h->pendingSlot, out);
}

bool Plugin_GetParamsSlot(PluginHandle* h, int slotIdx, char* json, int maxLen) {
    if (!h || !json || maxLen <= 0 || slotIdx < 0 || slotIdx >= h->camCount)
        return false;

    CamSlot* slot = getCamSlot(h->cmdBase, slotIdx);
    uint32_t plen = slot->params_len;
    if (plen == 0) { json[0] = '\0'; return true; }
    if (static_cast<int>(plen) >= maxLen) {
        SetErr(h, "params buffer too small: need %u, have %d", plen + 1, maxLen);
        return false;
    }
    memcpy(json, slot->params_data, plen);
    json[plen] = '\0';
    return true;
}

bool Plugin_GetParams(PluginHandle* h, char* json, int maxLen) {
    if (!h || !json || maxLen <= 0) return false;
    if (h->pendingSlot < 0) { SetErr(h, "No command pending"); return false; }
    return Plugin_GetParamsSlot(h, h->pendingSlot, json, maxLen);
}

bool Plugin_SendResultSlot(PluginHandle* h, int slotIdx, const char* resultJson) {
    if (!h || !resultJson || slotIdx < 0 || slotIdx >= h->camCount) return false;

    CamSlot* slot = getCamSlot(h->cmdBase, slotIdx);
    size_t rlen = strlen(resultJson);
    if (rlen >= sizeof(slot->result_data)) {
        SetErr(h, "Result JSON too large: %zu (max %zu)",
               rlen, sizeof(slot->result_data) - 1);
        return false;
    }

    memcpy(slot->result_data, resultJson, rlen);
    slot->result_data[rlen] = '\0';
    slot->result_len  = static_cast<uint32_t>(rlen);
    slot->cmd_status  = static_cast<uint32_t>(CmdStatus::Done);

    getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();

    SetEvent(h->hP2H[slotIdx]);

    if (h->pendingSlot == slotIdx) h->pendingSlot = -1;
    return true;
}

bool Plugin_SendResult(PluginHandle* h, const char* resultJson) {
    if (!h || !resultJson) return false;
    if (h->pendingSlot < 0) {
        SetErr(h, "No command pending to reply to");
        return false;
    }
    return Plugin_SendResultSlot(h, h->pendingSlot, resultJson);
}

int Plugin_GetCamCount(PluginHandle* h) {
    return h ? h->camCount : 0;
}

void Plugin_UpdateHeartbeat(PluginHandle* h) {
    if (h && h->cmdBase)
        getCmdHeader(h->cmdBase)->plugin_heartbeat = FiletimeNowMs();
}

bool Plugin_IsConnected(PluginHandle* h) {
    if (!h || !h->cmdBase) return false;
    uint64_t hb = getCmdHeader(h->cmdBase)->host_heartbeat;
    if (hb == 0) return true;  // host hasn't sent first heartbeat yet
    uint64_t now = FiletimeNowMs();
    return (now <= hb) || ((now - hb) < 5000ULL);
}

const char* Plugin_GetLastError(PluginHandle* h) {
    return h ? h->lastError : "Invalid handle";
}

// Version info
const char* Plugin_GetVersion(void)   { return PLUGIN_SDK_VERSION; }
int         Plugin_GetAPIVersion(void){ return PLUGIN_API_VERSION; }

// ============================================================
// C++ plugin::Plugin wrapper
// ============================================================
#ifdef __cplusplus

#include <stdexcept>
#include <string>

namespace plugin {

Plugin::Plugin(const std::string& shmName)
    : m_handle(nullptr)
{
    m_handle = Plugin_Create(shmName.c_str());
    if (!m_handle)
        throw std::runtime_error(std::string("Plugin_Create failed: ") +
                                 Plugin_GetLastError(nullptr));
}

Plugin::~Plugin() {
    Plugin_Destroy(m_handle);
    m_handle = nullptr;
}

bool Plugin::waitCommand(int timeoutMs) {
    return Plugin_WaitCommand(m_handle, timeoutMs);
}

ImageData Plugin::getImage() {
    ImageData img{};
    if (!Plugin_GetImage(m_handle, &img))
        throw std::runtime_error(Plugin_GetLastError(m_handle));
    return img;
}

std::string Plugin::getParams() {
    char buf[4096];
    if (!Plugin_GetParams(m_handle, buf, sizeof(buf)))
        throw std::runtime_error(Plugin_GetLastError(m_handle));
    return std::string(buf);
}

void Plugin::sendResult(const std::string& resultJson) {
    if (!Plugin_SendResult(m_handle, resultJson.c_str()))
        throw std::runtime_error(Plugin_GetLastError(m_handle));
}

bool Plugin::isConnected() const {
    return Plugin_IsConnected(m_handle);
}

std::string Plugin::getLastError() const {
    return Plugin_GetLastError(m_handle);
}

}  // namespace plugin

#endif  // __cplusplus

// ============================================================
// Non-Windows stubs
// ============================================================
#else

PluginHandle* Plugin_Create(const char*)                           { return nullptr; }
void          Plugin_Destroy(PluginHandle*)                        {}
bool Plugin_WaitCommand(PluginHandle*, int)                        { return false; }
int  Plugin_WaitCommandSlot(PluginHandle*, int)                    { return -1;    }
bool Plugin_GetImage(PluginHandle*, ImageData*)                    { return false; }
bool Plugin_GetImageSlot(PluginHandle*, int, ImageData*)           { return false; }
bool Plugin_GetParams(PluginHandle*, char*, int)                   { return false; }
bool Plugin_GetParamsSlot(PluginHandle*, int, char*, int)          { return false; }
bool Plugin_SendResult(PluginHandle*, const char*)                 { return false; }
bool Plugin_SendResultSlot(PluginHandle*, int, const char*)        { return false; }
bool Plugin_IsConnected(PluginHandle*)                             { return false; }
int  Plugin_GetCamCount(PluginHandle*)                             { return 0;     }
void Plugin_UpdateHeartbeat(PluginHandle*)                         {}
const char* Plugin_GetLastError(PluginHandle*)                     { return "Platform not supported"; }
const char* Plugin_GetVersion(void)                                { return PLUGIN_SDK_VERSION; }
int         Plugin_GetAPIVersion(void)                             { return PLUGIN_API_VERSION; }

#endif  // _WIN32
