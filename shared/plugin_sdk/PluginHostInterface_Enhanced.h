#pragma once

/**
 * @file PluginHostInterface_Enhanced.h
 * 
 * @brief DEPRECATED - Use PluginHostInterface.h instead
 * 
 * @note This file is kept for reference only.
 * All content from PluginHostInterface_Enhanced.h v1.0 has been integrated into
 * PluginHostInterface.h v1.1, which now includes:
 * 
 * ✅ PluginHostAsyncRequest structure
 * ✅ PluginHost_SendAsyncCommand() function
 * ✅ PluginHost_PollResult() function
 * ✅ PluginHost_GetPendingResultCount() function
 * ✅ Complete async usage patterns and examples
 * ✅ Full C++ wrapper classes
 * 
 * Instructions:
 * 1. Remove this file from your includes
 * 2. Use PluginHostInterface.h instead (v1.1.0+)
 * 3. All async APIs are available in the main header
 * 
 * @version 1.0.0 (DEPRECATED - DO NOT USE)
 * 
 * @note
 * For single-request or simple use cases, use PluginHostInterface.h
 * For advanced async scenarios with 10+ req/sec, use this file
 */

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Core Async Structures (Additions to PluginHostInterface.h)
// ============================================================

/**
 * @struct PluginHostAsyncRequest
 * @brief Request with ID for async tracking
 * 
 * Used in async mode where you submit multiple requests
 * and poll results later.
 * 
 * @example Typical usage:
 * @code
 * PluginHostAsyncRequest req;
 * req.request_id = 1001;  // Your unique ID
 * req.image.pixels = img_data;
 * req.image.width = 640;
 * req.image.height = 480;
 * req.image.channels = 1;  // grayscale
 * req.params_json = R"({"threshold": 50})";
 * 
 * PluginHost_SendAsyncCommand(host, &req);
 * @endcode
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
     * 
     * Value range: 0 to 2^31-1
     */
    int request_id;
    
    /**
     * Image data to process
     */
    struct PluginHostImage image;
    
    /**
     * JSON parameters (null-terminated string)
     */
    const char* params_json;
} PluginHostAsyncRequest;

// ============================================================
// Enhanced API Functions
// ============================================================

/**
 * @brief Send command with request ID (for async multi-request processing)
 * 
 * Like PluginHost_SendCommand() but with explicit request_id.
 * Useful when sending multiple requests rapidly and tracking results.
 * 
 * Non-blocking - returns immediately after queueing the request.
 * 
 * @param[in] handle Handle from PluginHost_Create()
 * @param[in] request Request with ID and image/params
 * 
 * @return true on success, false on error
 * 
 * Typical failure causes:
 * - Plugin process disconnected
 * - Plugin buffer full (queue depth exceeded max ~50)
 * - Invalid image dimensions
 * - params_json too large (>3840 bytes)
 * 
 * Key differences from PluginHost_SendCommand():
 * - You provide the request_id explicitly
 * - Result will include the same request_id for correlation
 * - Allows tracking multiple in-flight requests
 * - Better for high-throughput scenarios
 * 
 * @performance
 * - Latency: <5ms per request (memory copy + IPC)
 * - Queue depth: Typical 5-20 pending requests supported
 * - Total throughput: ~200-500 req/sec on typical hardware
 * 
 * For 10 requests: ~50ms to send all
 * 
 * @example: High-speed batch sending
 * @code
 * // Send 10 requests rapidly (async)
 * int request_ids[10];
 * 
 * for (int i = 0; i < 10; i++) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = request_ids[i] = 1000 + i;
 *     req.image = images[i];
 *     req.params_json = params[i];
 *     
 *     if (!PluginHost_SendAsyncCommand(host, &req)) {
 *         fprintf(stderr, "Send failed for req %d: %s\n",
 *                 req.request_id, PluginHost_GetLastError(host));
 *     }
 * }
 * @endcode
 * 
 * @see PluginHost_PollResult for retrieving results
 * @see PluginHost_GetPendingResultCount for queue monitoring
 */
bool PluginHost_SendAsyncCommand(struct PluginHostHandle* handle,
                                 const PluginHostAsyncRequest* request);

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
 *                      - 1000+: Not recommended (defeats purpose)
 * 
 * @return true if result is ready, false on timeout
 * 
 * Result pointer remains valid until next PluginHost_PollResult() call.
 * 
 * @example: Collecting results (may arrive out of order)
 * @code
 * // Collect results as they arrive
 * int completed = 0;
 * while (completed < 10) {
 *     struct PluginHostResult result;
 *     
 *     if (PluginHost_PollResult(host, &result, 50)) {
 *         // Result received for request result.request_id
 *         printf("Request %d complete: %s\n",
 *                result.request_id, result.status);
 *         completed++;
 *         
 *         // Process result immediately
 *         if (strcmp(result.status, "ok") == 0) {
 *             handleResult(&result);
 *         } else {
 *             handleError(&result);
 *         }
 *     } else {
 *         // Timeout - can do other work here
 *         updateProgressUI(completed, 10);
 *     }
 * }
 * @endcode
 * 
 * @performance (for 10 requests)
 * - Send phase: ~50ms (10 x 5ms)
 * - Poll phase: ~500-1000ms (depends on plugin speed)
 * - Total E2E: ~550-1050ms (vs 5-50s with sync blocking)
 * - CPU during poll: Very low (event-based, not spinning)
 * 
 * @note
 * - Results may arrive out of order
 * - Use result.request_id to match results to original requests
 * - Safe to call from UI thread (non-blocking, CPU efficient)
 * 
 * @see PluginHost_SendAsyncCommand for detailed example
 * @see PluginHost_GetPendingResultCount for queue monitoring
 */
bool PluginHost_PollResult(struct PluginHostHandle* handle,
                           struct PluginHostResult* result,
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
 * @note
 * - This is the number of COMPLETED results waiting to be collected
 * - Does NOT include in-flight requests still being processed
 * - Value can jump around as results complete
 * 
 * @example: Adaptive pipelining
 * @code
 * // Monitor queue and adapt submission rate
 * while (true) {
 *     int pending = PluginHost_GetPendingResultCount(host);
 *     
 *     if (pending < 5) {
 *         // Queue light, submit more requests
 *         for (int i = 0; i < 5; i++) {
 *             PluginHostAsyncRequest req;
 *             req.request_id = next_id++;
 *             req.image = getNextImage();
 *             req.params_json = getParams();
 *             
 *             PluginHost_SendAsyncCommand(host, &req);
 *         }
 *     } else if (pending > 20) {
 *         // Queue full, drain aggressively before submitting more
 *         for (int i = 0; i < 10; i++) {
 *             PluginHostResult result;
 *             if (PluginHost_PollResult(host, &result, 10)) {
 *                 processResult(&result);
 *             }
 *         }
 *     } else {
 *         // Queue balanced, maintain steady state
 *         PluginHostResult result;
 *         if (PluginHost_PollResult(host, &result, 50)) {
 *             processResult(&result);
 *         }
 *     }
 * }
 * @endcode
 * 
 * @see PluginHost_SendAsyncCommand for queueing requests
 * @see PluginHost_PollResult for collecting results
 */
int PluginHost_GetPendingResultCount(struct PluginHostHandle* handle);

// ============================================================
// Usage Patterns for Multi-Request Scenarios
// ============================================================

/**
 * @section async_patterns Advanced Usage Patterns (Multi-Request)
 * 
 * @subsection pattern_highspeed High-Speed Batch Submission (10 req/sec)
 * 
 * Optimal for processing 10+ images per second:
 * @code
 * struct PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * // Phase 1: Submit all requests rapidly (non-blocking)
 * std::vector<cv::Mat> images = loadImages("*.jpg");
 * int next_id = 0;
 * 
 * for (const auto& img : images) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = next_id++;
 *     req.image.pixels = img.data;
 *     req.image.width = img.cols;
 *     req.image.height = img.rows;
 *     req.image.channels = img.channels();
 *     req.params_json = R"({"threshold": 50})";
 *     
 *     if (!PluginHost_SendAsyncCommand(host, &req)) {
 *         fprintf(stderr, "Send failed: %s\n",
 *                 PluginHost_GetLastError(host));
 *     }
 * }
 * printf("Submitted %zu requests in %.1fms\n", 
 *        images.size(), (images.size() * 5) / 1000.0);
 * 
 * // Phase 2: Collect results as they arrive
 * std::map<int, std::string> results;
 * int collected = 0;
 * 
 * while (collected < images.size()) {
 *     PluginHostResult result;
 *     
 *     // Poll with short timeout to stay responsive
 *     if (PluginHost_PollResult(host, &result, 50)) {
 *         if (strcmp(result.status, "ok") == 0) {
 *             results[result.request_id] = result.data;
 *         }
 *         collected++;
 *         printf("Completed: %d/%zu\n", collected, images.size());
 *     }
 * }
 * 
 * PluginHost_Destroy(host);
 * @endcode
 * 
 * @subsection pattern_mixed_algorithms Mixed Algorithm Requests
 * 
 * For sending requests with different algorithms/parameters:
 * @code
 * struct PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * int rid = 0;
 * 
 * // Submit 3 circle detection requests
 * for (int i = 0; i < 3; i++) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = rid++;
 *     req.image = images[i];
 *     req.params_json = R"({"algorithm": "circle", "threshold": 50})";
 *     PluginHost_SendAsyncCommand(host, &req);
 * }
 * 
 * // Then 3 edge detection requests
 * for (int i = 3; i < 6; i++) {
 *     PluginHostAsyncRequest req;
 *     req.request_id = rid++;
 *     req.image = images[i];
 *     req.params_json = R"({"algorithm": "edge", "threshold": 25})";
 *     PluginHost_SendAsyncCommand(host, &req);
 * }
 * 
 * // Results arrive mixed - use request_id to organize
 * std::map<int, PluginHostResult> resultMap;
 * for (int i = 0; i < 6; i++) {
 *     PluginHostResult result;
 *     if (PluginHost_PollResult(host, &result, 1000)) {
 *         resultMap[result.request_id] = result;
 *     }
 * }
 * 
 * // Process by type
 * for (int i = 0; i < 3; i++) {
 *     printf("Circle result[%d]: %s\n", i, resultMap[i].data);
 * }
 * for (int i = 3; i < 6; i++) {
 *     printf("Edge result[%d]: %s\n", i, resultMap[i].data);
 * }
 * 
 * PluginHost_Destroy(host);
 * @endcode
 * 
 * @subsection pattern_queue_aware Queue-Aware Submission
 * 
 * For adaptive pipelining based on queue depth:
 * @code
 * struct PluginHostHandle* host = PluginHost_Create("MyHost_12345");
 * 
 * int next_id = 0;
 * int submitted = 0, processed = 0;
 * 
 * while (submitted < images.size() || processed < images.size()) {
 *     // Check queue saturation
 *     int pending = PluginHost_GetPendingResultCount(host);
 *     
 *     if (pending < 5 && submitted < images.size()) {
 *         // Queue is light, submit more
 *         PluginHostAsyncRequest req;
 *         req.request_id = next_id;
 *         req.image = images[submitted];
 *         req.params_json = params;
 *         
 *         if (PluginHost_SendAsyncCommand(host, &req)) {
 *             submitted++;
 *             next_id++;
 *         }
 *     } else if (pending > 0) {
 *         // Drain at least one result
 *         PluginHostResult result;
 *         if (PluginHost_PollResult(host, &result, 50)) {
 *             printf("Processed request %d\n", result.request_id);
 *             processed++;
 *         }
 *     } else {
 *         // Queue empty but more work - wait briefly
 *         usleep(10000);  // 10ms
 *     }
 * }
 * 
 * PluginHost_Destroy(host);
 * @endcode
 */

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // PLUGIN_HOST_INTERFACE_ENHANCED_H
