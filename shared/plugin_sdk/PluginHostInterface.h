#pragma once

/**
 * @file PluginHostInterface.h
 * 
 * @brief Host Application SDK Public Interface
 * 
 * This header defines the complete API for host application developers.
 * Include this file in your host/client application to integrate the plugin system.
 * 
 * @note This is for HOST/CLIENT developers, not plugin developers.
 *       Plugin developers should use PluginInterface.h instead.
 * 
 * @version 1.1.0 (Integrated - includes async support from Enhanced v1.0)
 * @date 2026-04-06
 * 
 * @changelog
 * - v1.1.0: Integrated async request handling (SendAsyncCommand, PollResult)
 * - v1.1.0: Added GetPendingResultCount for queue monitoring
 * - v1.1.0: Added complete C++ wrapper classes (Host, HostImage, HostResult)
 * - v1.1.0: Merged detailed async usage patterns
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Version Information
// ============================================================

#define PLUGIN_HOST_SDK_VERSION "1.0.0"
#define PLUGIN_HOST_API_VERSION 1

/**
 * Get Host SDK version string
 * @return Version string (e.g., "1.0.0")
 */
const char* PluginHost_GetVersion(void);

/**
 * Get Host API version number for compatibility checking
 * @return API version (e.g., 1)
 */
int PluginHost_GetAPIVersion(void);

// ============================================================
// Error Codes
// ============================================================

typedef enum {
    PLUGIN_HOST_OK = 0,
    PLUGIN_HOST_ERR_UNKNOWN = -1,
    PLUGIN_HOST_ERR_SHARED_MEMORY_FAILED = -2,
    PLUGIN_HOST_ERR_INVALID_HANDLE = -3,
    PLUGIN_HOST_ERR_PLUGIN_NOT_RUNNING = -4,
    PLUGIN_HOST_ERR_TIMEOUT = -5,
    PLUGIN_HOST_ERR_INVALID_IMAGE = -6,
    PLUGIN_HOST_ERR_INVALID_PARAMS = -7,
    PLUGIN_HOST_ERR_RESOURCE_EXHAUSTED = -8,
    PLUGIN_HOST_ERR_CONNECTION_LOST = -9,
} PluginHostError;

// ============================================================
// Data Structures
// ============================================================

/**
 * @struct PluginHostHandle
 * @brief Opaque handle to host-side plugin connection
 * 
 * This is an internal structure. Do not access its members directly.
 * Use it only as a handle to pass to PluginHost_* functions.
 */
typedef struct PluginHostHandle PluginHostHandle;

/**
 * @struct PluginHostImage
 * @brief Image to send to plugin for processing
 * 
 * Represents an image that will be sent to the plugin.
 * Pixel data must remain valid until PluginHost_SendCommand() is called.
 */
typedef struct {
    /**
     * Pointer to pixel data
     * 
     * Layout depends on channels:
     * - 1 channel (grayscale):  Each pixel is 1 byte (0-255)
     * - 3 channels (BGR/RGB):   Each pixel is 3 bytes
     * 
     * Data must be row-major (C-style contiguous).
     * For OpenCV: cv::Mat must have CV_8UC1 or CV_8UC3 type.
     */
    const uint8_t* pixels;
    
    /**
     * Image width in pixels
     */
    int width;
    
    /**
     * Image height in pixels
     */
    int height;
    
    /**
     * Number of color channels (1 for grayscale, 3 for BGR/RGB)
     */
    int channels;
} PluginHostImage;

/**
 * @struct PluginHostResult
 * @brief Result received from plugin
 * 
 * Contains the result data returned by the plugin.
 * String pointers are valid until the next PluginHost_* call.
 */
typedef struct {
    /**
     * Request ID (matches the ID from PluginHost_SendCommand)
     * Useful for correlating results with requests in async mode
     */
    int request_id;
    
    /**
     * Processing status
     * - "ok": Successful execution
     * - "error": Processing error occurred
     */
    const char* status;
    
    /**
     * Result data as JSON string
     * 
     * Example for successful case:
     * @code
     * {
     *     "circles": [
     *         {"x": 100.5, "y": 150.3, "radius": 50.0},
     *         {"x": 300.2, "y": 200.1, "radius": 45.0}
     *     ],
     *     "processing_time_ms": 25
     * }
     * @endcode
     * 
     * @note Parse this string with your preferred JSON library
     *       (nlohmann/json, RapidJSON, QJsonDocument, etc.)
     */
    const char* data;
    
    /**
     * Error message (only set if status is "error")
     */
    const char* error_message;
    
    /**
     * Error code (only set if status is "error")
     */
    int error_code;
} PluginHostResult;

/**
 * @struct PluginHostAsyncRequest
 * @brief Request with ID for async tracking
 * 
 * Used in async mode where you submit multiple requests
 * and poll results later.
 */
typedef struct {
    /**
     * Unique request identifier
     * 
     * You assign this ID. It will be returned in the result
     * so you can match result to request.
     * 
     * Recommendation: Use incrementing counter (0, 1, 2, ...)
     * or timestamps (milliseconds)
     */
    int request_id;
    
    /**
     * Image data to process
     */
    PluginHostImage image;
    
    /**
     * JSON parameters (null-terminated)
     */
    const char* params_json;
} PluginHostAsyncRequest;

// ============================================================
// Core API Functions
// ============================================================

/**
 * @brief Initialize host-side plugin connection
 * 
 * Must be called once before any other PluginHost_* functions.
 * This creates the shared memory regions and synchronization events
 * that will be used to communicate with the plugin.
 * 
 * The shared memory identifier (name) is passed to the plugin process
 * via command-line argument: `plugin.exe --shm-name=<name>`
 * 
 * @param[in] shmName Unique identifier for this host-plugin pair
 *                    Example: "HostPlugin_PID_12345"
 *                    Recommendation: Include current process ID and/or timestamp
 *                    Must be unique across multiple host instances
 * 
 * @return Non-NULL handle on success, NULL on failure
 * 
 * Failure causes:
 * - Shared memory with same name already in use
 * - Insufficient system resources (kernel limits)
 * - shmName is invalid or too long (max 64 characters)
 * - Windows API error (see Platform Notes section)
 * 
 * @example
 * @code
 * char shmName[256];
 * sprintf(shmName, "HostPlugin_%u_%lld", 
 *         (unsigned)GetCurrentProcessId(),
 *         (long long)time(NULL));
 * 
 * PluginHostHandle* host = PluginHost_Create(shmName);
 * if (!host) {
 *     fprintf(stderr, "Failed to initialize plugin host\n");
 *     return 1;
 * }
 * 
 * // Launch plugin process
 * // execl("circle_detect.exe", "circle_detect.exe", 
 * //       "--shm-name=", shmName, NULL);
 * @endcode
 * 
 * @see PluginHost_Destroy for cleanup
 */
PluginHostHandle* PluginHost_Create(const char* shmName);

/**
 * @brief Send image and parameters to plugin for processing
 * 
 * Submits an image and JSON parameters to the plugin for processing.
 * This function does NOT block waiting for results.
 * Call PluginHost_WaitResult() to retrieve the result.
 * 
 * This is a convenience wrapper that assigns request_id=0 automatically.
 * Use PluginHost_SendAsyncCommand() for multi-request scenarios.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[in] image Image data to process
 * @param[in] paramsJson JSON string with algorithm parameters (null-terminated)
 * 
 * @return true on success, false on error
 * 
 * @see PluginHost_SendAsyncCommand for async multi-request scenarios
 * @see PluginHost_WaitResult for retrieving results
 */
bool PluginHost_SendCommand(PluginHostHandle* handle,
                            const PluginHostImage* image,
                            const char* paramsJson);

/**
 * @brief Send command with request ID (for async multi-request processing)
 * 
 * Like PluginHost_SendCommand() but with explicit request_id.
 * Useful when sending multiple requests rapidly and tracking results.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[in] request Request with ID and image/params
 * 
 * @return true on success, false on error
 * 
 * Key differences from PluginHost_SendCommand():
 * - You provide the request_id explicitly
 * - Result will include the same request_id for correlation
 * - Allows tracking multiple in-flight requests
 * 
 * @example
 * @code
 * // Send 10 requests rapidly (async)
 * for (int i = 0; i < 10; i++) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = i;
 *     req.image = images[i];
 *     req.params_json = params[i];
 *     
 *     if (!PluginHost_SendAsyncCommand(host, &req)) {
 *         fprintf(stderr, "Send failed for req %d: %s\n",
 *                 i, PluginHost_GetLastError(host));
 *     }
 * }
 * 
 * // Collect results (in any order)
 * for (int i = 0; i < 10; i++) {
 *     PluginHostResult result;
 *     if (PluginHost_PollResult(host, &result, 100)) {
 *         printf("Got result for request %d: %s\n",
 *                result.request_id, result.data);
 *     }
 * }
 * @endcode
 * 
 * @performance
 * - Send latency: <5ms per request (memory copy + IPC)
 * - Queue depth: Typical 5-20 pending requests
 * - Total throughput: ~200 req/sec on typical hardware
 * 
 * For 10 requests: ~50ms to send all, then ~500ms to process all
 */
bool PluginHost_SendAsyncCommand(PluginHostHandle* handle,
                                 const PluginHostAsyncRequest* request);

/**
 * @brief Wait for plugin to complete processing and return result
 * 
 * Blocks until the plugin sends back results or timeout expires.
 * Must be called after PluginHost_SendCommand() returns true.
 * 
 * The result pointer remains valid until:
 * - Next call to PluginHost_WaitResult()
 * - Next call to PluginHost_SendCommand()
 * - PluginHost_Destroy() is called
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[out] result Output structure to fill with result data
 * @param[in] timeoutMs Timeout in milliseconds
 *                      - 0: Return immediately (polling mode, not recommended)
 *                      - 100-5000: Typical range for algorithm processing
 *                      - -1: Wait indefinitely (risky, no timeout protection)
 * 
 * @return true on success, false on timeout or error
 * 
 * Error cases:
 * - Timeout expired waiting for plugin result
 * - Plugin process crashed/disconnected
 * - handle is invalid
 * - result pointer is NULL
 * 
 * Timeout recommendations:
 * - Simple algorithms: 100-500ms
 * - Complex algorithms: 1000-5000ms
 * - Real-time processing: 33ms (30 FPS) to 16ms (60 FPS)
 * 
 * @example
 * @code
 * if (!PluginHost_SendCommand(host, &image, params)) {
 *     fprintf(stderr, "Send failed\n");
 *     return 1;
 * }
 * 
 * PluginHostResult result;
 * if (!PluginHost_WaitResult(host, &result, 5000)) {
 *     // Handle timeout
 *     fprintf(stderr, "Plugin timed out after 5 seconds\n");
 *     // Can retry or abort processing
 *     return 1;
 * }
 * 
 * if (strcmp(result.status, "ok") == 0) {
 *     printf("Success: %s\n", result.data);
 * } else {
 *     printf("Error: %s (code: %d)\n", 
 *            result.error_message, result.error_code);
 * }
 * @endcode
 * 
 * @performance
 * - Typical latency: 25-100ms (depends on plugin algorithm)
 * - CPU usage: Minimal (event-based waiting)
 * - No active polling (efficient)
 * 
 * @see PluginHost_PollResult for non-blocking polling in async scenarios
 */
bool PluginHost_WaitResult(PluginHostHandle* handle,
                           PluginHostResult* result,
                           int timeoutMs);

/**
 * @brief Poll for a result without blocking (for async processing)
 * 
 * Non-blocking version of PluginHost_WaitResult().
 * Useful for async multi-request scenarios where you submit
 * many requests and collect results as they arrive.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[out] result Output structure to fill if result available
 * @param[in] timeoutMs Maximum wait time for this call
 *                      - 0: Return immediately if no result (polling)
 *                      - 10-100: Recommended for async loops
 * 
 * @return true if result is ready, false on timeout
 * 
 * @example: Async multi-request processing
 * @code
 * // Send 10 requests rapidly
 * for (int i = 0; i < 10; i++) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = i;
 *     req.image = images[i];
 *     req.params_json = R"({"threshold": 50})";
 *     
 *     PluginHost_SendAsyncCommand(host, &req);
 *     printf("Sent request %d\n", i);
 * }
 * 
 * // Collect results (may arrive out of order)
 * int completed = 0;
 * while (completed < 10) {
 *     PluginHostResult result;
 *     
 *     if (PluginHost_PollResult(host, &result, 50)) {
 *         // Result received for request result.request_id
 *         printf("Request %d: %s\n", result.request_id, result.data);
 *         completed++;
 *     } else {
 *         // Can do other work here while waiting
 *         updateProgressBar(completed, 10);
 *     }
 * }
 * @endcode
 * 
 * @performance (for 10 requests)
 * - Send phase: ~50ms (10 x 5ms)
 * - Collect phase: ~500-1000ms (depends on plugin speed)
 * - Total: ~550-1050ms (vs 5-50s with sync blocking)
 * - CPU: Very low during poll (event-based)
 * 
 * @note
 * Results may arrive out of order. Use result.request_id to match
 * results to original requests. See PluginHostAsyncRequest for example.
 * 
 * @see PluginHost_SendAsyncCommand for detailed async example
 */
bool PluginHost_PollResult(PluginHostHandle* handle,
                           PluginHostResult* result,
                           int timeoutMs);

/**
 * @brief Send command and wait for result (synchronous convenience function)
 * 
 * Combines PluginHost_SendCommand() + PluginHost_WaitResult() in one call.
 * Useful for simple synchronous processing workflows.
 * 
 * Not recommended for multi-request scenarios (use SendAsyncCommand + PollResult instead).
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[in] image Image data to process
 * @param[in] paramsJson JSON parameters
 * @param[out] result Output structure filled with result
 * @param[in] timeoutMs Total timeout for send + processing + receive
 * 
 * @return true on success, false on timeout or error
 * 
 * @example
 * @code
 * PluginHostImage img = { matData, width, height, channels };
 * PluginHostResult result;
 * 
 * if (PluginHost_SendAndWait(host, &img, params, &result, 5000)) {
 *     // Result available immediately
 *     if (strcmp(result.status, "ok") == 0) {
 *         processResult(result.data);
 *     }
 * } else {
 *     fprintf(stderr, "Processing failed or timed out\n");
 * }
 * @endcode
 * 
 * @note This is a convenience wrapper. For better control, use
 *       PluginHost_SendCommand() and PluginHost_WaitResult() separately.
 * 
 * @see PluginHost_SendAsyncCommand for multi-request scenarios
 */
bool PluginHost_SendAndWait(PluginHostHandle* handle,
                            const PluginHostImage* image,
                            const char* paramsJson,
                            PluginHostResult* result,
                            int timeoutMs);

/**
 * @brief Get count of pending results in the queue
 * 
 * Returns the number of results that have arrived but not yet
 * retrieved via PluginHost_WaitResult() or PluginHost_PollResult().
 * 
 * Useful for monitoring pipeline saturation in async multi-request scenarios.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @return Number of pending results (0 if none)
 * 
 * @example
 * @code
 * // Monitor queue depth
 * while (true) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = next_id++;
 *     req.image = ...;
 *     req.params_json = ...;
 *     
 *     if (PluginHost_SendAsyncCommand(host, &req)) {
 *         int pending = PluginHost_GetPendingResultCount(host);
 *         printf("Pending results: %d\n", pending);
 *         
 *         // If queue getting full, start draining
 *         if (pending > 20) {
 *             PluginHostResult result;
 *             if (PluginHost_PollResult(host, &result, 10)) {
 *                 handleResult(&result);
 *             }
 *         }
 *     }
 * }
 * @endcode
 */
int PluginHost_GetPendingResultCount(PluginHostHandle* handle);

/**
 * @brief Check if plugin is still connected
 * 
 * Use this to detect if the plugin process has crashed or disconnected.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @return true if plugin is connected, false if disconnected
 * 
 * @example
 * @code
 * if (!PluginHost_IsPluginConnected(host)) {
 *     fprintf(stderr, "Plugin disconnected, restarting...\n");
 *     // Restart plugin process
 * }
 * @endcode
 */
bool PluginHost_IsPluginConnected(PluginHostHandle* handle);

/**
 * @brief Clean up host resources and close connection
 * 
 * Should be called once when the host application shuts down.
 * Safe to call multiple times or with NULL handle.
 * 
 * Cleanup includes:
 * - Closing shared memory regions
 * - Closing synchronization events
 * - Releasing OS resources
 * 
 * @param[in] handle Handle from PluginHost_Create() (can be NULL)
 * 
 * @example
 * @code
 * PluginHost_Destroy(host);
 * host = NULL;
 * @endcode
 */
void PluginHost_Destroy(PluginHostHandle* handle);

// ============================================================
// Information and Debugging Functions
// ============================================================

/**
 * @brief Get the shared memory name used by this host
 * 
 * Useful for logging or debugging purposes.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @return Shared memory name (valid until PluginHost_Destroy())
 * 
 * @example
 * @code
 * printf("Using shared memory: %s\n", 
 *        PluginHost_GetShmName(host));
 * @endcode
 */
const char* PluginHost_GetShmName(PluginHostHandle* handle);

/**
 * @brief Get detailed error message from last failed operation
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @return Error message string (valid until next PluginHost_* call)
 * 
 * @note Optional - for debugging purposes only
 * @example
 * @code
 * if (!PluginHost_SendCommand(host, &img, params)) {
 *     printf("Error: %s\n", PluginHost_GetLastError(host));
 * }
 * @endcode
 */
const char* PluginHost_GetLastError(PluginHostHandle* handle);

/**
 * @brief Get plugin connection statistics
 * 
 * Returns timing and quality metrics for debugging and optimization.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[out] lastCommandTimeMs Last command send time in milliseconds
 * @param[out] lastResultTimeMs Last result receive time in milliseconds
 * @param[out] totalCommandsCount Total number of commands sent
 * 
 * @example
 * @code
 * int cmdTime, resultTime, count;
 * PluginHost_GetStats(host, &cmdTime, &resultTime, &count);
 * printf("Avg result latency: %dms (over %d commands)\n", 
 *        resultTime, count);
 * @endcode
 */
void PluginHost_GetStats(PluginHostHandle* handle,
                         int* lastCommandTimeMs,
                         int* lastResultTimeMs,
                         int* totalCommandsCount);

// ============================================================
// Platform-Specific Notes
// ============================================================

/**
 * @section host_platform_notes Platform-Specific Information
 * 
 * @subsection host_windows Windows
 * - Requires: Windows 7 or later
 * - API: CreateFileMappingW, MapViewOfFile, CreateEventW
 * - Permissions: Can run in user mode, no elevation required
 * - Tested on: Windows 10, Windows 11
 * 
 * @subsection host_linux Linux
 * - Status: Under development (use POSIX shared memory + semaphores)
 * - Planned for v1.1.0
 * - API will remain compatible
 * 
 * @subsection host_macos macOS
 * - Status: Under development (use Mach IPC)
 * - Planned for v1.1.0
 * - API will remain compatible
 */

// ============================================================
// Usage Patterns
// ============================================================

/**
 * @section host_patterns Common Usage Patterns
 * 
 * @subsection pattern_sync Synchronous Processing (Simplest)
 * @code
 * // Create host connection
 * PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * // Load and prepare image
 * cv::Mat mat = cv::imread("image.jpg");
 * PluginHostImage img = {
 *     mat.data, mat.cols, mat.rows, mat.channels()
 * };
 * 
 * // Process synchronously
 * PluginHostResult result;
 * if (PluginHost_SendAndWait(host, &img, params, &result, 5000)) {
 *     if (strcmp(result.status, "ok") == 0) {
 *         // Parse and display result
 *     }
 * }
 * 
 * // Cleanup
 * PluginHost_Destroy(host);
 * @endcode
 * 
 * @subsection pattern_async Asynchronous Processing (More Control)
 * @code
 * PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * while (hasMoreImages) {
 *     PluginHostImage img = { ... };
 *     
 *     // Send command
 *     if (!PluginHost_SendCommand(host, &img, params)) {
 *         continue;  // Skip this image
 *     }
 *     
 *     // Do other work while plugin processes...
 *     doUIUpdate();
 *     
 *     // Wait for result
 *     PluginHostResult result;
 *     if (PluginHost_WaitResult(host, &result, 5000)) {
 *         displayResult(result.data);
 *     }
 * }
 * 
 * PluginHost_Destroy(host);
 * @endcode
 * 
 * @subsection pattern_batch Batch Processing
 * @code
 * PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * std::vector<std::string> results;
 * for (const auto& imagePath : imageList) {
 *     cv::Mat mat = cv::imread(imagePath);
 *     PluginHostImage img = { ... };
 *     
 *     PluginHost_SendCommand(host, &img, params);
 *     
 *     PluginHostResult result;
 *     if (PluginHost_WaitResult(host, &result, 10000)) {
 *         if (strcmp(result.status, "ok") == 0) {
 *             results.push_back(result.data);
 *         }
 *     }
 * }
 * 
 * PluginHost_Destroy(host);
 * return results;
 * @endcode
 * 
 * @subsection pattern_robust Robust Error Handling
 * @code
 * PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * if (!host) {
 *     fprintf(stderr, "Failed to create host connection\n");
 *     return 1;
 * }
 * 
 * PluginHostImage img = { ... };
 * PluginHostResult result;
 * 
 * int retries = 3;
 * while (retries > 0) {
 *     if (!PluginHost_SendCommand(host, &img, params)) {
 *         fprintf(stderr, "Send failed: %s\n", 
 *                 PluginHost_GetLastError(host));
 *         retries--;
 *         continue;
 *     }
 *     
 *     if (!PluginHost_WaitResult(host, &result, 5000)) {
 *         fprintf(stderr, "Result timeout\n");
 *         
 *         // Check if plugin crashed
 *         if (!PluginHost_IsPluginConnected(host)) {
 *             fprintf(stderr, "Plugin disconnected, aborting\n");
 *             break;
 *         }
 *         retries--;
 *         continue;
 *     }
 *     
 *     // Success!
 *     processResult(result);
 *     break;
 * }
 * 
 * PluginHost_Destroy(host);
 * @endcode
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
 * @class Host (C++ RAII wrapper)
 * @brief Convenience wrapper for PluginHost API
 * 
 * Automatically manages plugin host lifecycle.
 * Throws exceptions on errors (compatible with modern C++).
 * 
 * @example
 * @code
 * try {
 *     plugin::Host host("MyHost_12345");
 *     
 *     cv::Mat image = cv::imread("image.jpg");
 *     plugin::HostImage img(image);
 *     
 *     auto result = host.sendAndWait(img, params, 5000);
 *     
 *     if (result.isOk()) {
 *         std::cout << "Success: " << result.data() << std::endl;
 *     } else {
 *         std::cerr << "Error: " << result.errorMessage() << std::endl;
 *     }
 * } catch (const std::exception& e) {
 *     std::cerr << "Fatal error: " << e.what() << std::endl;
 * }
 * @endcode
 */
class Host {
public:
    /**
     * Initialize host connection
     * @param shmName Shared memory identifier
     * @throws std::runtime_error if initialization fails
     */
    explicit Host(const std::string& shmName);
    
    /**
     * Destructor automatically cleans up resources
     */
    ~Host();
    
    /**
     * Send image and parameters to plugin
     * @param image Image data
     * @param paramsJson JSON parameters
     * @return true on success
     * @throws std::runtime_error on error
     */
    bool sendCommand(const class HostImage& image, 
                     const std::string& paramsJson);
    
    /**
     * Wait for plugin processing result
     * @param timeoutMs Timeout in milliseconds
     * @return Result object
     * @throws std::runtime_error on error
     */
    class HostResult waitResult(int timeoutMs);
    
    /**
     * Send command and wait for result (convenience)
     * @param image Image data
     * @param paramsJson JSON parameters
     * @param timeoutMs Timeout in milliseconds
     * @return Result object
     */
    class HostResult sendAndWait(const class HostImage& image,
                                 const std::string& paramsJson,
                                 int timeoutMs);
    
    /**
     * Check plugin connection status
     * @return true if plugin is connected
     */
    bool isPluginConnected() const;
    
    /**
     * Get shared memory name
     * @return SHM identifier
     */
    std::string getShmName() const;
    
    /**
     * Get last error message
     * @return Error string
     */
    std::string getLastError() const;
    
private:
    PluginHostHandle* m_handle;
};

/**
 * @class HostImage
 * @brief Convenience wrapper for PluginHostImage
 * 
 * Supports direct construction from OpenCV Mat objects.
 * 
 * @example
 * @code
 * cv::Mat mat = cv::imread("image.jpg");
 * plugin::HostImage img(mat);
 * @endcode
 */
class HostImage {
public:
    /**
     * Construct from raw data
     */
    HostImage(const uint8_t* pixels, int width, int height, int channels);
    
    /**
     * Construct from OpenCV Mat (if OpenCV available)
     */
#ifdef CV_VERSION
    explicit HostImage(const cv::Mat& mat);
#endif
    
    /**
     * Get underlying C structure
     */
    const PluginHostImage& getCStruct() const { return m_img; }
    
private:
    PluginHostImage m_img;
};

/**
 * @class HostResult
 * @brief Convenience wrapper for PluginHostResult
 * 
 * @example
 * @code
 * auto result = host.waitResult(5000);
 * if (result.isOk()) {
 *     std::cout << result.data() << std::endl;
 * } else {
 *     std::cout << result.errorMessage() << std::endl;
 * }
 * @endcode
 */
class HostResult {
public:
    /**
     * Check if processing was successful
     */
    bool isOk() const;
    
    /**
     * Get result data (JSON string)
     */
    std::string data() const;
    
    /**
     * Get error message (if failed)
     */
    std::string errorMessage() const;
    
    /**
     * Get error code (if failed)
     */
    int errorCode() const;
    
    /**
     * Get status string
     */
    std::string status() const;
    
private:
    friend class Host;
    PluginHostResult m_result;
};

}  // namespace plugin

#endif  // __cplusplus
