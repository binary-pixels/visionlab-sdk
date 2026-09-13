#pragma once
#include <cstdint>
#include <cstddef>

// ============================================================
// Shared Memory Layout V3 — multi-camera, per-slot command queues
//
// Control block  SharedMem_Cmd_{shmName}  20 KB
//   [0]       MultiCamHeader  4 KB  — registration + heartbeat
//   [4096]    CamSlot[0]      4 KB  — camera 0 command/result
//   [8192]    CamSlot[1]      4 KB
//   [12288]   CamSlot[2]      4 KB
//   [16384]   CamSlot[3]      4 KB
//
// Image block    SharedMem_Img_{shmName}  128 MB
//   ImageSlot[i] starts at i * IMG_SLOT_SIZE (32 MB each)
//   Each slot:  ImageHeader (64 B) + raw pixel data
//
// Named events (8 total, auto-reset):
//   Event_H2P_{shmName}_0 .. _3   host  → plugin  (per camera)
//   Event_P2H_{shmName}_0 .. _3   plugin → host   (per camera)
// ============================================================

// ── Command types ────────────────────────────────────────────
enum class CmdType : uint32_t {
    None          = 0,
    PushImage     = 1,  // host wrote image slot + params_data
    LoadImageFile = 2,  // params_data contains {"path":"..."}
    Search        = 3,  // run algorithm on current image + params
    PushAndSearch = 4,  // PushImage + Search combined (most common)
    GrabAndSearch = 5,  // mode B: plugin grabs image + searches
    OpenCamera    = 6,  // mode B: open camera config UI
    GetParams     = 7,  // return current params JSON to result_data
    Resize        = 8,  // host notifies window resize {"x","y","w","h"}
    Shutdown      = 9,  // notify plugin to exit
    RunRecipe     = 10, // V4 recipe: images in slots 0..N-1 + recipe JSON in
                        // params_data; result = full semantic recipe JSON
    StartRecipe   = 11, // streaming session: params = full recipe JSON; plugin
                        // caches it and resets the staged-shot buffer
    PushShot      = 12, // streaming session: params = {"shot_index":k}, image
                        // in the camera slot → plugin runs shot k immediately
    FinishRecipe  = 13, // streaming session: aggregate staged shots, return the
                        // full semantic recipe JSON, clear the session
};

enum class CmdStatus : uint32_t {
    Idle    = 0,  // slot ready for new command
    Pending = 1,  // command received, processing
    Done    = 2,  // result written, P2H fired
    Error   = 3,  // fatal error (watchdog timeout, etc.)
};

// ── Global header (4096 bytes) ───────────────────────────────
struct MultiCamHeader {
    // Identity — 24 bytes
    uint32_t magic;             // 0xCAFE1234
    uint32_t version;           // 3
    uint32_t host_pid;
    uint32_t plugin_pid;
    uint32_t cam_count;         // number of active camera slots (1-4)
    uint8_t  reserved0[44];     // pad to 64

    // Heartbeat — 16 bytes
    uint64_t host_heartbeat;    // ms timestamp, written by host
    uint64_t plugin_heartbeat;  // ms timestamp, written by plugin

    // Padding to exactly 4096 bytes
    uint8_t  pad[4016];
};
static_assert(sizeof(MultiCamHeader) == 4096, "MultiCamHeader must be 4096 bytes");

// ── Per-camera command slot (4096 bytes) ─────────────────────
struct CamSlot {
    // Command header — 64 bytes
    uint32_t cmd_id;        // monotonically increasing per-slot sequence number
    uint32_t cmd_type;      // CmdType enum value
    uint32_t cmd_status;    // CmdStatus enum value (write atomically)
    uint32_t timeout_ms;    // watchdog timeout (0 = no watchdog)
    // Seqlock writer sequence (P2-2): the HOST bumps it to ODD before writing
    // the command fields and to EVEN after committing, then fires H2P. The
    // plugin processes a command only when it observes an even seq that
    // differs from the last one it handled — this rejects duplicate wakes and
    // torn reads. 0 = legacy host that does not use the mechanism.
    volatile uint32_t writer_seq;
    uint8_t  reserved[44];

    // Params JSON — 2016 bytes
    uint32_t params_len;
    char     params_data[2012];

    // Result JSON — 2016 bytes
    uint32_t result_len;
    char     result_data[2012];
};
static_assert(sizeof(CamSlot) == 4096, "CamSlot must be 4096 bytes");

// ── Image header (64 bytes, precedes raw pixel data) ─────────
struct ImageHeader {
    uint32_t width;         // image width in pixels
    uint32_t height;        // image height in pixels
    uint32_t channels;      // 1=grayscale  3=BGR
    uint32_t stride;        // bytes per row
    uint32_t data_size;     // total pixel bytes following this header
    uint32_t roi_x;         // crop origin x in full frame (0 if full image)
    uint32_t roi_y;         // crop origin y in full frame
    uint32_t orig_width;    // full-frame width  (0 if full image sent)
    uint32_t orig_height;   // full-frame height (0 if full image sent)
    uint8_t  reserved[28];  // pad to 64 bytes
};
static_assert(sizeof(ImageHeader) == 64, "ImageHeader must be 64 bytes");

// ── Size constants ───────────────────────────────────────────
// NOTE: this header is DUPLICATED in runtime — keep
// the two copies byte-identical. MAX_CAM_SLOTS is the single source of the
// camera count: InspectTab::kMaxCameras and CameraPanel::kMaxCameras both
// reference it, so changing it here propagates to the UI + host.
static constexpr int    MAX_CAM_SLOTS   = 4;
static constexpr size_t CAM_SLOT_SIZE   = 4096;
static constexpr size_t CMD_BLOCK_SIZE  = sizeof(MultiCamHeader) + MAX_CAM_SLOTS * CAM_SLOT_SIZE; // 20 KB
static constexpr size_t IMG_SLOT_SIZE   = 32ULL * 1024 * 1024;  // 32 MB per slot
static constexpr size_t IMG_BLOCK_SIZE  = MAX_CAM_SLOTS * IMG_SLOT_SIZE;                          // 128 MB
static constexpr uint32_t SHM_MAGIC    = 0xCAFE1234u;
static constexpr uint32_t SHM_VERSION  = 3u;

// ── Inline accessors ─────────────────────────────────────────
inline MultiCamHeader* getCmdHeader(void* base) {
    return reinterpret_cast<MultiCamHeader*>(base);
}
inline const MultiCamHeader* getCmdHeader(const void* base) {
    return reinterpret_cast<const MultiCamHeader*>(base);
}
inline CamSlot* getCamSlot(void* base, int i) {
    return reinterpret_cast<CamSlot*>(
        static_cast<uint8_t*>(base) + sizeof(MultiCamHeader) + i * CAM_SLOT_SIZE);
}
inline const CamSlot* getCamSlot(const void* base, int i) {
    return reinterpret_cast<const CamSlot*>(
        static_cast<const uint8_t*>(base) + sizeof(MultiCamHeader) + i * CAM_SLOT_SIZE);
}
inline uint8_t* getImgSlot(void* imgBase, int i) {
    return static_cast<uint8_t*>(imgBase) + i * IMG_SLOT_SIZE;
}
inline const uint8_t* getImgSlot(const void* imgBase, int i) {
    return static_cast<const uint8_t*>(imgBase) + i * IMG_SLOT_SIZE;
}
