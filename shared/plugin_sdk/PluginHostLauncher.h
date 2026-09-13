#pragma once

/**
 * @file PluginHostLauncher.h
 * 
 * @brief Simple Plugin Launcher (Hides complexity)
 * 
 * High-level API for customers to launch plugins without dealing with:
 * - Process management (QProcess/CreateProcess)
 * - Command-line arguments
 * - Shared memory name generation
 * - Window embedding details
 * - UI mode switching
 * 
 * Just: Create → Launch → Embed → Use
 * 
 * @version 1.0.0
 * @date 2026-04-06
 */

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Opaque Handle
// ============================================================

/**
 * @struct PluginHostLauncher
 * @brief Opaque launcher instance
 * 
 * Manages plugin process lifecycle and UI embedding.
 * Hides all complexity from users.
 */
typedef struct PluginHostLauncher PluginHostLauncher;

/**
 * @struct PluginHostHandle  (from PluginHostInterface.h)
 * @brief Communication handle to plugin
 */
typedef struct PluginHostHandle PluginHostHandle;

// ============================================================
// UI Mode Enumeration
// ============================================================

/**
 * @enum PluginUIMode
 * @brief How the plugin window is displayed
 */
typedef enum {
    /**
     * Plugin window is child of host UI container
     * - Window is embedded in host's widget/dialog
     * - Plugin window position/size managed by host
     * - Window is hidden from taskbar
     * - Best for: Integrated UIs, tool panels
     * 
     * Example:
     * @code
     * launcher.setUIMode(PLUGIN_UI_EMBEDDED);
     * launcher.embedInto(parentWidget, 10, 10, 640, 480);
     * @endcode
     */
    PLUGIN_UI_EMBEDDED = 1,
    
    /**
     * Plugin window is standalone/independent
     * - Plugin creates its own top-level window
     * - Host can move/resize it freely
     * - Window appears in taskbar
     * - Best for: Separate tool windows, full apps
     * 
     * Example:
     * @code
     * launcher.setUIMode(PLUGIN_UI_STANDALONE);
     * launcher.launch();
     * // Plugin manages its own window
     * @endcode
     */
    PLUGIN_UI_STANDALONE = 2,
    
    /**
     * Headless mode (no UI)
     * - Plugin runs without any window
     * - Host receives results only
     * - Best for: Background processing, servers
     * 
     * Example:
     * @code
     * launcher.setUIMode(PLUGIN_UI_HEADLESS);
     * launcher.launch();
     * // Only IPC communication, no window
     * @endcode
     */
    PLUGIN_UI_HEADLESS = 3,
} PluginUIMode;

// ============================================================
// Launch Status Enumeration
// ============================================================

typedef enum {
    PLUGIN_LAUNCH_OK = 0,
    PLUGIN_LAUNCH_ERR_FILE_NOT_FOUND = -1,
    PLUGIN_LAUNCH_ERR_PERMISSION_DENIED = -2,
    PLUGIN_LAUNCH_ERR_PROCESS_FAILED = -3,
    PLUGIN_LAUNCH_ERR_TIMEOUT = -4,
    PLUGIN_LAUNCH_ERR_INVALID_MODE = -5,
    PLUGIN_LAUNCH_ERR_ALREADY_RUNNING = -6,
} PluginLaunchStatus;

// ============================================================
// Launcher Creation and Destruction
// ============================================================

/**
 * @brief Create a plugin launcher
 * 
 * Creates launcher instance to manage plugin process and communication.
 * Does NOT start the plugin immediately.
 * 
 * @param[in] pluginExePath Path to plugin executable
 *                          - Absolute path: "C:\\plugins\\circle_detect.exe"
 *                          - Relative path: "./plugins/circle_detect.exe"  
 *                          - Filename only: "circle_detect.exe" (searches PATH)
 * 
 * @return Handle to launcher, NULL on error
 * 
 * @note
 * If exePath doesn't exist, creation succeeds but launch will fail.
 * This allows lazy validation (useful for testing).
 * 
 * @example
 * @code
 * PluginHostLauncher* launcher = 
 *     PluginHostLauncher_Create("C:\\plugins\\circle_detect.exe");
 * 
 * if (!launcher) {
 *     fprintf(stderr, "Failed to create launcher\n");
 *     return 1;
 * }
 * @endcode
 * 
 * @see PluginHostLauncher_Destroy for cleanup
 * @see PluginHostLauncher_Launch to start plugin
 */
PluginHostLauncher* PluginHostLauncher_Create(const char* pluginExePath);

/**
 * @brief Destroy launcher and stop plugin if running
 * 
 * Stops plugin process if still running and cleans up resources.
 * Safe to call multiple times or with NULL.
 * 
 * @param[in] launcher Handle from PluginHostLauncher_Create()
 * 
 * @note
 * After calling this, do NOT use the launcher handle again.
 * Any PluginHostHandle from this launcher becomes invalid.
 * 
 * @example
 * @code
 * PluginHostLauncher_Destroy(launcher);
 * launcher = NULL;
 * @endcode
 */
void PluginHostLauncher_Destroy(PluginHostLauncher* launcher);

// ============================================================
// Configuration (before launch)
// ============================================================

/**
 * @brief Set UI mode (before launch)
 * 
 * Configures how the plugin window will be displayed.
 * Must be called BEFORE PluginHostLauncher_Launch().
 * 
 * @param[in] launcher Launcher handle
 * @param[in] mode UI mode (EMBEDDED, STANDALONE, or HEADLESS)
 * 
 * @return true on success, false on error
 * 
 * @note
 * Default mode is STANDALONE if not set.
 * Changing mode after launch has no effect.
 * 
 * @example
 * @code
 * PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
 * @endcode
 * 
 * @see PluginUIMode for mode descriptions
 */
bool PluginHostLauncher_SetUIMode(PluginHostLauncher* launcher,
                                   PluginUIMode mode);

/**
 * @brief Embed plugin window in host UI container (for EMBEDDED mode)
 * 
 * Specifies where the plugin window should appear in the host UI.
 * Only meaningful if mode is PLUGIN_UI_EMBEDDED.
 * Must be called BEFORE PluginHostLauncher_Launch().
 * 
 * @param[in] launcher Launcher handle
 * @param[in] parentWindowHandle Parent window/widget handle
 *                                - Windows: HWND (cast from void*)
 *                                - Qt: (HWND)widget->winId()
 * @param[in] x X position in parent window (pixels)
 * @param[in] y Y position in parent window (pixels)
 * @param[in] width Width (pixels)
 * @param[in] height Height (pixels)
 * 
 * @return true on success, false on error
 * 
 * Failure causes:
 * - parentWindowHandle is NULL or invalid
 * - Width/height is invalid (0 or negative)
 * - Called after launch
 * - UI mode is not EMBEDDED
 * 
 * @example: In Qt
 * @code
 * // In your QWidget subclass
 * PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
 * 
 * PluginHostLauncher_EmbedInto(
 *     launcher,
 *     (void*)containerWidget->winId(),  // Parent window handle
 *     10,   // x offset
 *     10,   // y offset
 *     640,  // width
 *     480   // height
 * );
 * 
 * PluginHostLauncher_Launch(launcher, 5000);
 * @endcode
 * 
 * @example: In Windows (Win32)
 * @code
 * HWND parentHwnd = GetDlgItem(hDialogMainWindow, IDC_CONTAINER);
 * 
 * PluginHostLauncher_EmbedInto(
 *     launcher,
 *     (void*)parentHwnd,
 *     10, 10, 640, 480
 * );
 * @endcode
 */
bool PluginHostLauncher_EmbedInto(PluginHostLauncher* launcher,
                                   void* parentWindowHandle,
                                   int x, int y,
                                   int width, int height);

// ============================================================
// Process Lifecycle
// ============================================================

/**
 * @brief Launch the plugin process
 * 
 * Starts the plugin executable and waits for it to initialize.
 * This is blocking - waits for plugin to be ready or timeout.
 * 
 * What this function does automatically:
 * - Generates unique shared memory name
 * - Creates shared memory regions
 * - Constructs command-line arguments
 * - Starts plugin process
 * - Waits for plugin to connect
 * - Applies UI mode settings
 * - Embeds window if in EMBEDDED mode
 * 
 * You don't need to do any of ^^ that yourself!
 * 
 * @param[in] launcher Launcher handle
 * @param[in] timeoutMs Timeout in milliseconds
 *                      - 1000-5000: Typical
 *                      - 10000: For slow systems or debug
 *                      - -1: Infinite wait (not recommended)
 * 
 * @return true on success, false on timeout/error
 * 
 * Failure causes:
 * - Plugin executable not found
 * - Plugin process crashes during init
 * - Plugin doesn't connect within timeout
 * - Shared memory creation failed
 * - Window embedding failed (in EMBEDDED mode)
 * 
 * @example: Simple launch
 * @code
 * if (!PluginHostLauncher_Launch(launcher, 5000)) {
 *     fprintf(stderr, "Launch failed: %s\n",
 *             PluginHostLauncher_GetLastError(launcher));
 *     return 1;
 * }
 * printf("Plugin launched successfully\n");
 * @endcode
 * 
 * @example: Launch with embedding
 * @code
 * PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
 * PluginHostLauncher_EmbedInto(launcher, (void*)hwnd, 10, 10, 640, 480);
 * 
 * if (!PluginHostLauncher_Launch(launcher, 5000)) {
 *     fprintf(stderr, "Failed: %s\n",
 *             PluginHostLauncher_GetLastError(launcher));
 * }
 * // Window is automatically sized and positioned at this point
 * @endcode
 * 
 * @see PluginHostLauncher_IsRunning to check status
 * @see PluginHostLauncher_Stop to shut down gracefully
 * @see PluginHostLauncher_Kill to terminate forcefully
 */
bool PluginHostLauncher_Launch(PluginHostLauncher* launcher,
                                int timeoutMs);

/**
 * @brief Check if plugin is currently running
 * 
 * Returns true if plugin process is alive and connected.
 * 
 * @param[in] launcher Launcher handle
 * @return true if running, false otherwise
 * 
 * @example
 * @code
 * if (!PluginHostLauncher_IsRunning(launcher)) {
 *     fprintf(stderr, "Plugin not running\n");
 *     return 1;
 * }
 * @endcode
 */
bool PluginHostLauncher_IsRunning(PluginHostLauncher* launcher);

/**
 * @brief Stop plugin gracefully
 * 
 * Signals plugin to shut down cleanly.
 * Waits for plugin to exit.
 * 
 * @param[in] launcher Launcher handle
 * @param[in] timeoutMs Max time to wait for shutdown
 *                      - 1000-3000: Typical
 * 
 * @return true if stopped cleanly, false on timeout
 * 
 * If returns false (timeout), call PluginHostLauncher_Kill()
 * to force termination.
 * 
 * @example
 * @code
 * // Graceful shutdown
 * if (!PluginHostLauncher_Stop(launcher, 3000)) {
 *     printf("Plugin slow to stop, killing...\n");
 *     PluginHostLauncher_Kill(launcher);
 * }
 * @endcode
 */
bool PluginHostLauncher_Stop(PluginHostLauncher* launcher,
                              int timeoutMs);

/**
 * @brief Force terminate plugin process
 * 
 * Kills plugin immediately (no graceful shutdown).
 * Use if Stop() times out or plugin is stuck.
 * 
 * @param[in] launcher Launcher handle
 * @return true on success, false if already dead
 * 
 * @example
 * @code
 * if (pluginHung) {
 *     PluginHostLauncher_Kill(launcher);
 * }
 * @endcode
 */
bool PluginHostLauncher_Kill(PluginHostLauncher* launcher);

// ============================================================
// Getting the Communication Handle
// ============================================================

/**
 * @brief Get the communication handle to plugin
 * 
 * After successful launch, get the PluginHostHandle to send commands.
 * This is the handle you pass to PluginHost_SendCommand(), etc.
 * 
 * @param[in] launcher Launcher handle
 * @return Communication handle, NULL if plugin not running
 * 
 * @note
 * The returned handle is owned by launcher.
 * Do NOT call PluginHost_Destroy() on it.
 * It becomes invalid when launcher is destroyed.
 * 
 * @example
 * @code
 * if (!PluginHostLauncher_Launch(launcher, 5000)) {
 *     return 1;
 * }
 * 
 * PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);
 * if (!host) {
 *     fprintf(stderr, "Failed to get host handle\n");
 *     return 1;
 * }
 * 
 * // Now use host to send commands:
 * PluginHostImage img = { ... };
 * PluginHost_SendCommand(host, &img, params);
 * @endcode
 * 
 * @see PluginHostInterface.h for communication functions
 */
PluginHostHandle* PluginHostLauncher_GetHost(PluginHostLauncher* launcher);

// ============================================================
// Information and Diagnostics
// ============================================================

/**
 * @brief Get the plugin's process ID
 * 
 * Useful for debugging or external monitoring.
 * 
 * @param[in] launcher Launcher handle
 * @return Process ID, 0 if not running
 * 
 * @example
 * @code
 * int pid = PluginHostLauncher_GetPID(launcher);
 * printf("Plugin running with PID: %d\n", pid);
 * @endcode
 */
int PluginHostLauncher_GetPID(PluginHostLauncher* launcher);

/**
 * @brief Get the plugin's window handle (in EMBEDDED/STANDALONE modes)
 * 
 * Returns the plugin's top-level window handle.
 * Useful if you need to manipulate the window directly.
 * 
 * @param[in] launcher Launcher handle
 * @return Window handle (HWND cast to void*), NULL if no window
 * 
 * @note
 * Don't modify window properties directly if possible.
 * Use launcher API instead for consistency.
 * 
 * @example
 * @code
 * void* hwnd = PluginHostLauncher_GetWindowHandle(launcher);
 * if (hwnd) {
 *     printf("Plugin window: 0x%p\n", hwnd);
 * }
 * @endcode
 */
void* PluginHostLauncher_GetWindowHandle(PluginHostLauncher* launcher);

/**
 * @brief Get last error message
 * 
 * Returns human-readable error from last failed operation.
 * 
 * @param[in] launcher Launcher handle
 * @return Error message (valid until next API call)
 * 
 * @example
 * @code
 * if (!PluginHostLauncher_Launch(launcher, 5000)) {
 *     printf("Launch error: %s\n",
 *            PluginHostLauncher_GetLastError(launcher));
 * }
 * @endcode
 */
const char* PluginHostLauncher_GetLastError(PluginHostLauncher* launcher);

// ============================================================
// Simple Usage Pattern
// ============================================================

/**
 * @section launcher_simple_pattern Typical Usage (Simple)
 * 
 * This is what customers should be able to do:
 * 
 * @code
 * // 1. Create launcher
 * PluginHostLauncher* launcher = 
 *     PluginHostLauncher_Create("circle_detect.exe");
 * 
 * // 2. Configure (optional - defaults work too)
 * PluginHostLauncher_SetUIMode(launcher, PLUGIN_UI_EMBEDDED);
 * PluginHostLauncher_EmbedInto(launcher, (void*)hwnd, 10, 10, 640, 480);
 * 
 * // 3. Launch
 * if (!PluginHostLauncher_Launch(launcher, 5000)) {
 *     printf("Error: %s\n", PluginHostLauncher_GetLastError(launcher));
 *     PluginHostLauncher_Destroy(launcher);
 *     return 1;
 * }
 * 
 * // 4. Get handle for communication
 * PluginHostHandle* host = PluginHostLauncher_GetHost(launcher);
 * 
 * // 5. Send commands (see PluginHostInterface.h)
 * PluginHostImage img = { imageData, 640, 480, 1 };
 * PluginHost_SendCommand(host, &img, R"({"threshold": 50})");
 * 
 * // 6. Get results (see PluginHostInterface.h)
 * PluginHostResult result;
 * if (PluginHost_WaitResult(host, &result, 5000)) {
 *     printf("Result: %s\n", result.data);
 * }
 * 
 * // 7. Cleanup (also stops plugin)
 * PluginHostLauncher_Destroy(launcher);
 * @endcode
 * 
 * That's it! No shared memory names, no command-line args, no SetParent calls.
 * All hidden in the launcher.
 */

#ifdef __cplusplus
}  // extern "C"
#endif
