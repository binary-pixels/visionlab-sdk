/**
 * @file PluginHostLauncher.cpp
 * @brief Plugin process launcher implementation
 *
 * Manages:
 * - Shared-memory name generation
 * - Plugin process creation (via Windows CreateProcess)
 * - Waiting for the plugin to connect (plugin_pid != 0)
 * - Window embedding in PLUGIN_UI_EMBEDDED mode
 * - Graceful / forceful shutdown
 *
 * No Qt dependency – pure Win32 + PluginHostInterface SDK.
 */

#include "PluginHostLauncher.h"
#include "PluginHostInterfaceExt.h"

#ifdef _WIN32
#include <windows.h>
#include <cstring>
#include <cstdio>

// ── Internal structure ───────────────────────────────────────────────────────
struct PluginHostLauncher {
    char             exePath[MAX_PATH] = {};
    PluginUIMode     uiMode            = PLUGIN_UI_STANDALONE;
    HWND             parentHwnd        = nullptr;
    int              embedX = 0, embedY = 0, embedW = 0, embedH = 0;

    PROCESS_INFORMATION pi            = {};
    PluginHostHandle*   host          = nullptr;
    char                shmName[128]  = {};
    bool                launched      = false;
    char                lastError[512]= {};
};

// ── Helpers ──────────────────────────────────────────────────────────────────

static void GenShmName(char* buf, size_t sz) {
    snprintf(buf, sz, "HostPlugin_%lu_%lu",
             GetCurrentProcessId(), GetTickCount());
}

struct FindPidCtx { DWORD pid; HWND hwnd; };

static BOOL CALLBACK FindWindowByPidCb(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindPidCtx*>(lp);
    DWORD wPid = 0;
    GetWindowThreadProcessId(hwnd, &wPid);
    if (wPid == ctx->pid && IsWindowVisible(hwnd)) {
        HWND parent = GetParent(hwnd);
        if (!parent || parent == GetDesktopWindow()) {
            ctx->hwnd = hwnd;
            return FALSE;   // stop enumeration
        }
    }
    return TRUE;
}

static HWND FindPluginWindow(DWORD pid) {
    FindPidCtx ctx = { pid, nullptr };
    EnumWindows(FindWindowByPidCb, reinterpret_cast<LPARAM>(&ctx));
    return ctx.hwnd;
}

static void EmbedPluginWindow(HWND pluginHwnd, HWND containerHwnd,
                               int x, int y, int w, int h) {
    SetParent(pluginHwnd, containerHwnd);

    LONG_PTR style = GetWindowLongPtr(pluginHwnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME);
    style |= WS_CHILD;
    SetWindowLongPtr(pluginHwnd, GWL_STYLE, style);

    SetWindowPos(pluginHwnd, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    ShowWindow(pluginHwnd, SW_SHOW);
}

// ── Public API ───────────────────────────────────────────────────────────────

PluginHostLauncher* PluginHostLauncher_Create(const char* pluginExePath) {
    if (!pluginExePath) return nullptr;
    auto* l = new PluginHostLauncher{};
    strncpy(l->exePath, pluginExePath, MAX_PATH - 1);
    return l;
}

void PluginHostLauncher_Destroy(PluginHostLauncher* l) {
    if (!l) return;
    if (PluginHostLauncher_IsRunning(l))
        PluginHostLauncher_Stop(l, 500);  // 500 ms grace, then kill
    PluginHost_Destroy(l->host);
    if (l->pi.hProcess) CloseHandle(l->pi.hProcess);
    if (l->pi.hThread)  CloseHandle(l->pi.hThread);
    delete l;
}

bool PluginHostLauncher_SetUIMode(PluginHostLauncher* l, PluginUIMode mode) {
    if (!l || l->launched) return false;
    l->uiMode = mode;
    return true;
}

bool PluginHostLauncher_EmbedInto(PluginHostLauncher* l,
                                   void* parentWindowHandle,
                                   int x, int y, int width, int height) {
    if (!l || l->launched) return false;
    if (!parentWindowHandle || width <= 0 || height <= 0) return false;
    l->parentHwnd = static_cast<HWND>(parentWindowHandle);
    l->embedX = x;  l->embedY = y;
    l->embedW = width; l->embedH = height;
    return true;
}

bool PluginHostLauncher_Launch(PluginHostLauncher* l, int timeoutMs) {
    if (!l) return false;
    if (l->launched) {
        snprintf(l->lastError, sizeof(l->lastError), "Already launched");
        return false;
    }

    // Validate exe
    if (GetFileAttributesA(l->exePath) == INVALID_FILE_ATTRIBUTES) {
        snprintf(l->lastError, sizeof(l->lastError),
                 "Executable not found: %s", l->exePath);
        return false;
    }

    // Generate unique shared-memory name
    GenShmName(l->shmName, sizeof(l->shmName));

    // Create host-side IPC resources
    l->host = PluginHost_Create(l->shmName);
    if (!l->host) {
        snprintf(l->lastError, sizeof(l->lastError), "PluginHost_Create failed");
        return false;
    }

    // Build command line.
    // Plugin parses: --shm-name <name>   --parent-hwnd <hwnd-hex>
    // (space-separated, per runtime)
    char cmdLine[MAX_PATH + 256];
    if (l->uiMode == PLUGIN_UI_EMBEDDED && l->parentHwnd) {
        snprintf(cmdLine, sizeof(cmdLine),
                 "\"%s\" --shm-name %s --parent-hwnd 0x%p",
                 l->exePath, l->shmName,
                 static_cast<void*>(l->parentHwnd));
    } else {
        snprintf(cmdLine, sizeof(cmdLine),
                 "\"%s\" --shm-name %s",
                 l->exePath, l->shmName);
    }

    // Start plugin process
    STARTUPINFOA si = {};
    si.cb = sizeof(si);
    if (!CreateProcessA(nullptr, cmdLine,
                        nullptr, nullptr, FALSE, 0,
                        nullptr, nullptr, &si, &l->pi)) {
        snprintf(l->lastError, sizeof(l->lastError),
                 "CreateProcess failed: %lu", GetLastError());
        PluginHost_Destroy(l->host);
        l->host = nullptr;
        return false;
    }

    // Wait for the plugin to connect (writes its PID into shared memory)
    if (!PluginHost_WaitForConnection(l->host, timeoutMs)) {
        snprintf(l->lastError, sizeof(l->lastError),
                 "Plugin did not connect within %dms: %s",
                 timeoutMs, PluginHost_GetLastError(l->host));
        TerminateProcess(l->pi.hProcess, 0);
        CloseHandle(l->pi.hProcess); l->pi.hProcess = nullptr;
        CloseHandle(l->pi.hThread);  l->pi.hThread  = nullptr;
        PluginHost_Destroy(l->host);
        l->host = nullptr;
        return false;
    }

    l->launched = true;

    // Embedded mode: wait for the plugin window and re-parent it
    if (l->uiMode == PLUGIN_UI_EMBEDDED && l->parentHwnd) {
        DWORD pid = l->pi.dwProcessId;
        HWND  pluginHwnd = nullptr;
        DWORD winDeadline = GetTickCount() + 3000;

        while (GetTickCount() < winDeadline) {
            // First check children of the container (plugin may have already
            // called SetParent via --parent-hwnd, making it a child window)
            FindPidCtx childCtx = { pid, nullptr };
            EnumChildWindows(l->parentHwnd, [](HWND hwnd, LPARAM lp) -> BOOL {
                auto* c = reinterpret_cast<FindPidCtx*>(lp);
                DWORD wPid = 0;
                GetWindowThreadProcessId(hwnd, &wPid);
                if (wPid == c->pid) { c->hwnd = hwnd; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&childCtx));
            if (childCtx.hwnd) { pluginHwnd = childCtx.hwnd; break; }

            // Fallback: still a top-level window (SetParent not yet called)
            pluginHwnd = FindPluginWindow(pid);
            if (pluginHwnd) break;

            Sleep(50);
        }

        if (pluginHwnd)
            EmbedPluginWindow(pluginHwnd, l->parentHwnd,
                              l->embedX, l->embedY, l->embedW, l->embedH);
        // If window not found yet the plugin itself will call SetParent via
        // the --parent-window argument; we'll send a Resize IPC later.
    }

    return true;
}

bool PluginHostLauncher_IsRunning(PluginHostLauncher* l) {
    if (!l || !l->launched || !l->pi.hProcess) return false;
    DWORD code = 0;
    if (!GetExitCodeProcess(l->pi.hProcess, &code)) return false;
    return code == STILL_ACTIVE;
}

bool PluginHostLauncher_Stop(PluginHostLauncher* l, int timeoutMs) {
    if (!l || !l->pi.hProcess) return true;
    // Send graceful shutdown signal
    if (l->host) PluginHost_SendShutdown(l->host);
    // Wait for graceful exit (default 800ms — enough for a well-behaved plugin)
    DWORD t = static_cast<DWORD>(timeoutMs > 0 ? timeoutMs : 800);
    if (WaitForSingleObject(l->pi.hProcess, t) != WAIT_OBJECT_0) {
        // Didn't exit gracefully — kill immediately
        TerminateProcess(l->pi.hProcess, 0);
    }
    CloseHandle(l->pi.hProcess); l->pi.hProcess = nullptr;
    CloseHandle(l->pi.hThread);  l->pi.hThread  = nullptr;
    l->launched = false;
    return true;
}

bool PluginHostLauncher_Kill(PluginHostLauncher* l) {
    if (!l || !l->pi.hProcess) return false;
    BOOL ok = TerminateProcess(l->pi.hProcess, 0);
    CloseHandle(l->pi.hProcess); l->pi.hProcess = nullptr;
    CloseHandle(l->pi.hThread);  l->pi.hThread  = nullptr;
    l->launched = false;
    return ok != FALSE;
}

PluginHostHandle* PluginHostLauncher_GetHost(PluginHostLauncher* l) {
    return l ? l->host : nullptr;
}

int PluginHostLauncher_GetPID(PluginHostLauncher* l) {
    return (l && l->launched) ? static_cast<int>(l->pi.dwProcessId) : 0;
}

void* PluginHostLauncher_GetWindowHandle(PluginHostLauncher* l) {
    if (!l || !l->launched) return nullptr;
    return static_cast<void*>(FindPluginWindow(l->pi.dwProcessId));
}

const char* PluginHostLauncher_GetLastError(PluginHostLauncher* l) {
    return l ? l->lastError : "Invalid launcher";
}

// ============================================================
// Non-Windows stubs
// ============================================================
#else

PluginHostLauncher* PluginHostLauncher_Create(const char*)          { return nullptr; }
void PluginHostLauncher_Destroy(PluginHostLauncher*)                {}
bool PluginHostLauncher_SetUIMode(PluginHostLauncher*, PluginUIMode){ return false; }
bool PluginHostLauncher_EmbedInto(PluginHostLauncher*, void*,
                                   int, int, int, int)              { return false; }
bool PluginHostLauncher_Launch(PluginHostLauncher*, int)            { return false; }
bool PluginHostLauncher_IsRunning(PluginHostLauncher*)              { return false; }
bool PluginHostLauncher_Stop(PluginHostLauncher*, int)              { return true;  }
bool PluginHostLauncher_Kill(PluginHostLauncher*)                   { return false; }
PluginHostHandle* PluginHostLauncher_GetHost(PluginHostLauncher*)   { return nullptr; }
int  PluginHostLauncher_GetPID(PluginHostLauncher*)                 { return 0; }
void* PluginHostLauncher_GetWindowHandle(PluginHostLauncher*)       { return nullptr; }
const char* PluginHostLauncher_GetLastError(PluginHostLauncher*)    { return "Platform not supported"; }

#endif // _WIN32
