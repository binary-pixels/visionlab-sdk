#pragma once

/**
 * @file PluginInterface.h
 * 
 * @brief Plugin SDK Public Interface
 * 
 * This header defines the complete API for plugin developers.
 * Include this file in your plugin code to use the SDK.
 * 
 * @version 1.0.0
 * @date 2026-04-06
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Version Information
// ============================================================

#define PLUGIN_SDK_VERSION "1.0.0"
#define PLUGIN_API_VERSION 1

/**
 * Get SDK version string
 * @return Version string (e.g., "1.0.0")
 */
const char* Plugin_GetVersion(void);

/**
 * Get API version number for compatibility checking
 * @return API version (e.g., 1)
 * 
 * Use this to check if newer features are available:
 * @code
 * if (Plugin_GetAPIVersion() >= 2) {
 *     // Use v2+ features
 * } else {
 *     // Use v1 features only
 * }
 * @endcode
 */
int Plugin_GetAPIVersion(void);

// ============================================================
// Error Codes
// ============================================================

typedef enum {
    PLUGIN_OK = 0,
    PLUGIN_ERR_INVALID_HANDLE = -1,
    PLUGIN_ERR_SHARED_MEMORY = -2,
    PLUGIN_ERR_NO_COMMAND = -3,
    PLUGIN_ERR_BUFFER_TOO_SMALL = -4,
    PLUGIN_ERR_CONNECTION_LOST = -5,
    PLUGIN_ERR_INVALID_IMAGE = -6,
    PLUGIN_ERR_TIMEOUT = -7,
} PluginError;

// ============================================================
// Data Structures
// ============================================================

/**
 * @struct PluginHandle
 * @brief Opaque handle to plugin connection
 * 
 * This is an internal structure. Do not access its members directly.
 * Use it only as a handle to pass to Plugin_* functions.
 */
typedef struct PluginHandle PluginHandle;

/**
 * @struct ImageData
 * @brief Image information and pixel data pointer
 * 
 * Represents a single image received from the host.
 * The pixel data pointer is valid until the next call to Plugin_WaitCommand().
 */
typedef struct {
    /**
     * Pointer to pixel data
     * 
     * Layout depends on channels:
     * - 1 channel (grayscale):  Each pixel is 1 byte (0-255)
     * - 3 channels (BGR/RGB):   Each pixel is 3 bytes (B,G,R or R,G,B)
     * 
     * @warning Do NOT store this pointer beyond the current cycle.
     *          It becomes invalid after Plugin_WaitCommand() or Plugin_SendResult().
     */
    uint8_t* pixels;
    
    /**
     * Image width in pixels
     */
    int width;
    
    /**
     * Image height in pixels
     */
    int height;
    
    /**
     * Number of color channels
     * - 1: Grayscale (CV_8UC1)
     * - 3: Color BGR or RGB (CV_8UC3)
     */
    int channels;
} ImageData;

// ============================================================
// Core API Functions
// ============================================================

/**
 * @brief Initialize plugin connection to host
 * 
 * Must be called once at plugin startup before any other functions.
 * The shmName is passed by the host via `--shm-name=` command-line argument.
 * 
 * @param[in] shmName Shared memory identifier (e.g., "HostPlugin_12345")
 *                    Typically obtained from argv[]
 * 
 * @return Non-NULL handle on success, NULL on failure
 * 
 * Failure causes:
 * - Host process not running
 * - Invalid shmName
 * - Insufficient system resources
 * - Shared memory already in use
 * 
 * @example
 * @code
 * const char* shmName = nullptr;
 * for (int i = 1; i < argc; i++) {
 *     if (strncmp(argv[i], "--shm-name=", 11) == 0) {
 *         shmName = argv[i] + 11;
 *         break;
 *     }
 * }
 * if (!shmName) {
 *     fprintf(stderr, "Error: --shm-name=<name> required\n");
 *     return 1;
 * }
 * PluginHandle* plugin = Plugin_Create(shmName);
 * if (!plugin) {
 *     fprintf(stderr, "Failed to initialize plugin\n");
 *     return 1;
 * }
 * @endcode
 */
PluginHandle* Plugin_Create(const char* shmName);

/**
 * @brief Wait for a command from the host
 * 
 * Blocks until:
 * - Host sends a command (returns true)
 * - Timeout expires (returns false)
 * 
 * Must be called repeatedly in your main loop.
 * Only call Plugin_GetImage() and Plugin_GetParams() after this returns true.
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @param[in] timeoutMs Timeout in milliseconds
 *                      - 0: Return immediately (polling mode)
 *                      - 100: Wait up to 100ms (recommended)
 *                      - -1: Wait indefinitely (risky, no timeout)
 * 
 * @return true if command received, false on timeout
 * 
 * @example
 * @code
 * while (true) {
 *     if (Plugin_WaitCommand(plugin, 100)) {
 *         // Process command
 *         ImageData img;
 *         Plugin_GetImage(plugin, &img);
 *         // ... more processing ...
 *     } else {
 *         // Timeout - can do background work or sleep
 *     }
 * }
 * @endcode
 * 
 * @performance
 * - Typical latency: 1-2ms when command available
 * - CPU usage during timeout: Minimal (event-based waiting)
 * - Recommended timeout: 100ms (good balance of responsiveness and CPU usage)
 */
bool Plugin_WaitCommand(PluginHandle* handle, int timeoutMs);

/**
 * @brief Retrieve the image from the pending command
 * 
 * Must be called after Plugin_WaitCommand() returns true.
 * Retrieving non-existent image will fail.
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @param[out] out ImageData structure to fill with image information
 * 
 * @return true on success, false on error
 * 
 * Error cases:
 * - No command is pending
 * - handle is invalid
 * - out pointer is NULL
 * 
 * @warning The pixels pointer in @c out is valid only until:
 *          - Next call to Plugin_WaitCommand()
 *          - Next call to Plugin_SendResult()
 *          Do NOT store the pointer for later use.
 * 
 * @example
 * @code
 * if (Plugin_WaitCommand(plugin, 100)) {
 *     ImageData img;
 *     if (!Plugin_GetImage(plugin, &img)) {
 *         fprintf(stderr, "Failed to get image\n");
 *         continue;
 *     }
 *     printf("Image: %d x %d x %d\n", img.width, img.height, img.channels);
 *     
 *     // Convert to OpenCV (if needed)
 *     cv::Mat mat(img.height, img.width,
 *                 img.channels == 1 ? CV_8UC1 : CV_8UC3,
 *                 img.pixels);
 * }
 * @endcode
 * 
 * @note Image data uses row-major layout (C-style contiguous storage).
 *       For OpenCV: rows are stored sequentially in memory.
 */
bool Plugin_GetImage(PluginHandle* handle, ImageData* out);

/**
 * @brief Retrieve the JSON parameters from the pending command
 * 
 * Must be called after Plugin_WaitCommand() returns true.
 * Parameters are typically algorithm settings (threshold, radius, etc).
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @param[out] json Output buffer (must be pre-allocated by caller)
 * @param[in] maxLen Size of output buffer in bytes
 * 
 * @return true on success, false on error
 * 
 * Error cases:
 * - No command is pending
 * - json buffer is NULL
 * - maxLen is too small for the parameters
 * - handle is invalid
 * 
 * Buffer size recommendations:
 * - Minimum: 256 bytes (safe for common use)
 * - Recommended: 1024 bytes
 * - Maximum supported: 3840 bytes
 * 
 * @example
 * @code
 * char params[1024];
 * if (!Plugin_GetParams(plugin, params, sizeof(params))) {
 *     fprintf(stderr, "Failed to get parameters\n");
 *     continue;
 * }
 * printf("Params: %s\n", params);
 * 
 * // Parse JSON (use your preferred JSON library)
 * // Example with nlohmann/json:
 * // auto j = nlohmann::json::parse(params);
 * // int threshold = j.value("threshold", 50);
 * @endcode
 * 
 * Typical parameter format:
 * @code
 * {
 *     "algorithm": "circle_detect",
 *     "threshold": 50,
 *     "min_radius": 30,
 *     "max_radius": 100
 * }
 * @endcode
 */
bool Plugin_GetParams(PluginHandle* handle, char* json, int maxLen);

/**
 * @brief Send results back to the host
 * 
 * Must be called after processing the image and parameters.
 * Signals the host that results are ready.
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @param[in] resultJson JSON string with results (null-terminated)
 * 
 * @return true on success, false on error
 * 
 * Error cases:
 * - Connection lost to host
 * - resultJson is NULL or malformed
 * - resultJson is too large (>1MB)
 * - handle is invalid
 * 
 * Result JSON format (success case):
 * @code
 * {
 *     "status": "ok",
 *     "data": {
 *         "circles": [
 *             {"x": 100, "y": 150, "radius": 50},
 *             {"x": 300, "y": 200, "radius": 45}
 *         ],
 *         "processing_time_ms": 25,
 *         "circle_count": 2
 *     }
 * }
 * @endcode
 * 
 * Result JSON format (error case):
 * @code
 * {
 *     "status": "error",
 *     "error_code": 1001,
 *     "error_message": "Image too small"
 * }
 * @endcode
 * 
 * Size limits:
 * - Recommended: < 10 KB (typical results fit easily)
 * - Maximum: 1 MB (for future expansion)
 * 
 * @example
 * @code
 * char resultJson[4096];
 * sprintf(resultJson, 
 *     R"({"status":"ok","circles":[{"x":%.1f,"y":%.1f,"radius":%.1f}]})",
 *     x, y, radius);
 * 
 * if (!Plugin_SendResult(plugin, resultJson)) {
 *     fprintf(stderr, "Failed to send result\n");
 * }
 * @endcode
 * 
 * @performance
 * - Latency: <1ms (direct write to shared memory)
 * - Host receives: Within 100ms (depends on host's polling rate)
 */
bool Plugin_SendResult(PluginHandle* handle, const char* resultJson);

/**
 * @brief Clean up plugin resources and close connection
 * 
 * Should be called once at plugin shutdown.
 * Safe to call multiple times or with NULL handle.
 * 
 * @param[in] handle Handle from Plugin_Create() (can be NULL)
 * 
 * Cleanup includes:
 * - Unmapping shared memory
 * - Closing synchronization events
 * - Releasing OS resources
 * 
 * @example
 * @code
 * Plugin_Destroy(plugin);
 * plugin = nullptr;  // Good practice
 * return 0;
 * @endcode
 */
void Plugin_Destroy(PluginHandle* handle);

// ============================================================
// Helper Functions (Optional)
// ============================================================

/**
 * @brief Get detailed error message from last failed operation
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @return Error message string (valid until next API call)
 * 
 * @note Optional - for debugging purposes only
 * @code
 * if (!Plugin_WaitCommand(plugin, 100)) {
 *     printf("Error: %s\n", Plugin_GetLastError(plugin));
 * }
 * @endcode
 */
const char* Plugin_GetLastError(PluginHandle* handle);

/**
 * @brief Check if connection to host is still active
 * 
 * @param[in] handle Handle from Plugin_Create()
 * @return true if connected, false if disconnected
 * 
 * @note Optional - useful for detecting host crashes
 * @code
 * if (!Plugin_IsConnected(plugin)) {
 *     fprintf(stderr, "Host disconnected, exiting\n");
 *     break;
 * }
 * @endcode
 */
bool Plugin_IsConnected(PluginHandle* handle);

// ============================================================
// Multi-Camera Slot API (V3)
// ============================================================

/**
 * @brief Return the number of active camera slots configured by the host.
 *
 * @param[in] handle Handle from Plugin_Create()
 * @return Number of active slots (1..MAX_CAM_SLOTS), or 0 on invalid handle.
 */
int Plugin_GetCamCount(PluginHandle* handle);

/**
 * @brief Update the plugin heartbeat timestamp (call every ~500 ms).
 *
 * Plugin_WaitCommand() and Plugin_WaitCommandSlot() call this automatically.
 * Only needed if you have a long processing loop without those calls.
 *
 * @param[in] handle Handle from Plugin_Create()
 */
void Plugin_UpdateHeartbeat(PluginHandle* handle);

/**
 * @brief Wait for a command on any active camera slot (multi-camera).
 *
 * Uses WaitForMultipleObjects internally to listen on all active slots
 * simultaneously.  Returns the index of the slot that received a command,
 * or -1 on timeout/error.
 *
 * After this returns >= 0 use Plugin_GetImageSlot / Plugin_GetParamsSlot /
 * Plugin_SendResultSlot with the returned slot index.
 *
 * @param[in] handle    Handle from Plugin_Create()
 * @param[in] timeoutMs Milliseconds to wait (-1 = infinite, 0 = poll)
 * @return Slot index (0..camCount-1) on success, -1 on timeout/error.
 */
int Plugin_WaitCommandSlot(PluginHandle* handle, int timeoutMs);

/**
 * @brief Retrieve the image from a specific camera slot.
 *
 * @param[in]  handle    Handle from Plugin_Create()
 * @param[in]  slotIndex Camera slot (0..camCount-1)
 * @param[out] out       ImageData to fill
 * @return true on success, false if no image or invalid slot.
 */
bool Plugin_GetImageSlot(PluginHandle* handle, int slotIndex, ImageData* out);

/**
 * @brief Retrieve JSON parameters from a specific camera slot.
 *
 * @param[in]  handle    Handle from Plugin_Create()
 * @param[in]  slotIndex Camera slot (0..camCount-1)
 * @param[out] json      Caller-allocated output buffer
 * @param[in]  maxLen    Size of output buffer in bytes
 * @return true on success.
 */
bool Plugin_GetParamsSlot(PluginHandle* handle, int slotIndex,
                          char* json, int maxLen);

/**
 * @brief Send results back to the host for a specific camera slot.
 *
 * @param[in] handle     Handle from Plugin_Create()
 * @param[in] slotIndex  Camera slot (0..camCount-1)
 * @param[in] resultJson Null-terminated JSON result string
 * @return true on success.
 */
bool Plugin_SendResultSlot(PluginHandle* handle, int slotIndex,
                           const char* resultJson);

// ============================================================
// Platform-Specific Notes
// ============================================================

/**
 * @section platform_notes Platform-Specific Information
 * 
 * @subsection windows Windows
 * - Requires: Windows SDK for shared memory and events
 * - Automatically linked: kernel32.lib, user32.lib
 * - Tested on: Windows 10, Windows 11
 * - Performance: Excellent (native Windows IPC)
 * 
 * @subsection linux Linux
 * - Status: Under development
 * - Alternative IPC mechanism will be used
 * - API remains compatible
 * 
 * @subsection macos macOS
 * - Status: Under development
 * - Mach kernel IPC will be used
 * - API remains compatible
 */

// ============================================================
// Compiler Support
// ============================================================

/**
 * @section compiler_support Compiler Requirements
 * 
 * @subsection cxx C++
 * - Standard: C++11 or later
 * - Tested with: MSVC 2022, GCC 11, Clang 14
 * 
 * @subsection c C
 * - Standard: C99 or later
 * - Also fully C-compatible (extern "C" guard)
 */

#ifdef __cplusplus
}  // extern "C"
#endif

// ============================================================
// C++ Convenience Wrappers (Optional)
// ============================================================

#ifdef __cplusplus

#include <string>
#include <memory>

namespace plugin {

/**
 * @class Plugin (C++ wrapper)
 * @brief RAII wrapper for PluginHandle
 * 
 * Optional C++ convenience class that automatically manages cleanup.
 * 
 * @example
 * @code
 * try {
 *     plugin::Plugin plugin("HostPlugin_12345");
 *     
 *     while (plugin.waitCommand(100)) {
 *         auto img = plugin.getImage();
 *         auto params = plugin.getParams();
 *         
 *         // Process...
 *         plugin.sendResult(resultJson);
 *     }
 * } catch (const std::exception& e) {
 *     std::cerr << "Error: " << e.what() << std::endl;
 * }
 * @endcode
 */
class Plugin {
public:
    /**
     * Initialize plugin connection
     * @throws std::runtime_error if initialization fails
     */
    explicit Plugin(const std::string& shmName);
    
    /**
     * Destructor automatically cleans up resources
     */
    ~Plugin();
    
    /**
     * Wait for command
     * @param timeoutMs Timeout in milliseconds
     * @return true if command received
     * @throws std::runtime_error on error
     */
    bool waitCommand(int timeoutMs = 100);
    
    /**
     * Get image data
     * @return ImageData structure
     * @throws std::runtime_error if no command pending
     */
    ImageData getImage();
    
    /**
     * Get parameters JSON
     * @return Parameters as string
     * @throws std::runtime_error on error
     */
    std::string getParams();
    
    /**
     * Send results
     * @param resultJson Result JSON string
     * @throws std::runtime_error on error
     */
    void sendResult(const std::string& resultJson);
    
    /**
     * Check if connected
     * @return true if connection active
     */
    bool isConnected() const;
    
    /**
     * Get last error message
     * @return Error message
     */
    std::string getLastError() const;
    
private:
    PluginHandle* m_handle;
};

}  // namespace plugin

#endif  // __cplusplus
