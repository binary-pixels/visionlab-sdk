#include "IpcClient.h"
#include <QMetaType>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#endif

// P2-4: the IPC protocol layout is part of the on-wire contract — an
// accidental edit of the shared header must not silently change the format.
static_assert(sizeof(CamSlot) == 4096, "CamSlot must stay 4096 bytes");
static_assert(sizeof(ImageHeader) == 64, "ImageHeader must stay 64 bytes");
static_assert(CMD_BLOCK_SIZE == 20 * 1024, "CMD_BLOCK_SIZE must stay 20 KB");
static_assert(IMG_BLOCK_SIZE == 128ULL * 1024 * 1024, "IMG_BLOCK_SIZE must stay 128 MB");
static_assert(MAX_CAM_SLOTS == 4, "MAX_CAM_SLOTS must stay 4 (protocol)");

IpcClient::IpcClient(const QString& shmName, QObject* parent)
    : QThread(parent), m_shmName(shmName)
{}

IpcClient::~IpcClient() {
    requestStop();
    wait(3000);
    closeSharedMem();
}

void IpcClient::requestStop() {
    m_stopRequested = true;
#ifdef _WIN32
    if (m_hP2H) SetEvent(static_cast<HANDLE>(m_hP2H));
#endif
}

// ----------------------------------------------------------------
// openSharedMem — called once before starting the thread
// ----------------------------------------------------------------
bool IpcClient::openSharedMem() {
#ifdef _WIN32
    const std::wstring wcmd = (QString("SharedMem_Cmd_") + m_shmName).toStdWString();
    const std::wstring wimg = (QString("SharedMem_Img_") + m_shmName).toStdWString();
    // V3: per-slot event names use _0 suffix for slot 0
    const std::wstring wh2p = (QString("Event_H2P_") + m_shmName + "_0").toStdWString();
    const std::wstring wp2h = (QString("Event_P2H_") + m_shmName + "_0").toStdWString();

    HANDLE hCmdFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE, 0,
                                         static_cast<DWORD>(CMD_BLOCK_SIZE),
                                         wcmd.c_str());
    if (!hCmdFile) return false;

    HANDLE hImgFile = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr,
                                         PAGE_READWRITE,
                                         static_cast<DWORD>(IMG_BLOCK_SIZE >> 32),
                                         static_cast<DWORD>(IMG_BLOCK_SIZE & 0xFFFFFFFF),
                                         wimg.c_str());
    if (!hImgFile) { CloseHandle(hCmdFile); return false; }

    m_cmdBase = MapViewOfFile(hCmdFile, FILE_MAP_ALL_ACCESS, 0, 0, CMD_BLOCK_SIZE);
    m_img     = static_cast<uint8_t*>(
        MapViewOfFile(hImgFile, FILE_MAP_ALL_ACCESS, 0, 0, IMG_BLOCK_SIZE));

    if (!m_cmdBase || !m_img) {
        closeSharedMem();
        CloseHandle(hCmdFile);
        CloseHandle(hImgFile);
        return false;
    }

    m_hCmdFile = hCmdFile;
    m_hImgFile = hImgFile;

    // Derive typed pointers from base
    m_hdr   = getCmdHeader(m_cmdBase);
    m_slot0 = getCamSlot(m_cmdBase, 0);

    // Init header
    memset(m_cmdBase, 0, CMD_BLOCK_SIZE);
    m_hdr->magic     = SHM_MAGIC;
    m_hdr->version   = SHM_VERSION;
    m_hdr->host_pid  = static_cast<uint32_t>(GetCurrentProcessId());
    m_hdr->cam_count = 1;

    // Events (auto-reset)
    m_hH2P = CreateEventW(nullptr, FALSE, FALSE, wh2p.c_str());
    m_hP2H = CreateEventW(nullptr, FALSE, FALSE, wp2h.c_str());

    return m_hH2P && m_hP2H;
#else
    return false;
#endif
}

void IpcClient::closeSharedMem() {
#ifdef _WIN32
    if (m_cmdBase) { UnmapViewOfFile(m_cmdBase); m_cmdBase = nullptr; m_hdr = nullptr; m_slot0 = nullptr; }
    if (m_img)     { UnmapViewOfFile(m_img);     m_img = nullptr; }
    if (m_hCmdFile){ CloseHandle(static_cast<HANDLE>(m_hCmdFile)); m_hCmdFile = nullptr; }
    if (m_hImgFile){ CloseHandle(static_cast<HANDLE>(m_hImgFile)); m_hImgFile = nullptr; }
    if (m_hH2P)   { CloseHandle(static_cast<HANDLE>(m_hH2P));    m_hH2P = nullptr; }
    if (m_hP2H)   { CloseHandle(static_cast<HANDLE>(m_hP2H));    m_hP2H = nullptr; }
#endif
}

// ----------------------------------------------------------------
// writeHeartbeat — call from a QTimer in the GUI thread
// ----------------------------------------------------------------
void IpcClient::writeHeartbeat() {
#ifdef _WIN32
    if (!m_hdr) return;
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER ui;
    ui.LowPart  = ft.dwLowDateTime;
    ui.HighPart = ft.dwHighDateTime;
    m_hdr->host_heartbeat = ui.QuadPart / 10000ULL;
#endif
}

// §10.7: detect a crashed plugin immediately by probing its process handle
// instead of waiting for the 5 s heartbeat-staleness window. The plugin writes
// plugin_pid into the shared-memory header when it registers. A pid of 0 means
// the plugin has not registered yet — treat as alive so the caller falls back
// to heartbeat staleness for that short window.
bool IpcClient::pluginProcessAlive()
{
#ifdef _WIN32
    if (!m_hdr || m_hdr->plugin_pid == 0)
        return true;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, m_hdr->plugin_pid);
    if (!h)
        return false;   // cannot open the handle → process is gone
    DWORD code = 0;
    const BOOL ok = GetExitCodeProcess(h, &code);
    CloseHandle(h);
    return !ok || code == STILL_ACTIVE;
#else
    return true;
#endif
}

// P2-2 seqlock on the command slot: bump writer_seq to ODD before writing the
// command fields and to EVEN after committing, so the plugin only acts on a
// fully-committed, new command (rejects duplicate wakes / torn writes).
static void seqBegin(CamSlot* slot)  { slot->writer_seq += 1; }   // → odd  (writing)
static void seqCommit(CamSlot* slot) { slot->writer_seq += 1; }   // → even (committed)

// ----------------------------------------------------------------
// sendPushAndSearch
// ----------------------------------------------------------------
bool IpcClient::sendPushAndSearch(const cv::Mat& image, const QString& paramsJson) {
#ifdef _WIN32
    if (!m_hdr || !m_img) return false;

    // Write image into slot 0 of the image block
    uint8_t* imgSlot = getImgSlot(m_img, 0);
    ImageHeader* hdr = reinterpret_cast<ImageHeader*>(imgSlot);
    hdr->width    = static_cast<uint32_t>(image.cols);
    hdr->height   = static_cast<uint32_t>(image.rows);
    hdr->channels = static_cast<uint32_t>(image.channels());
    hdr->stride   = static_cast<uint32_t>(image.step);
    hdr->data_size= static_cast<uint32_t>(image.total() * image.elemSize());

    uint8_t* dst = imgSlot + sizeof(ImageHeader);
    if (image.isContinuous()) {
        memcpy(dst, image.data, hdr->data_size);
    } else {
        for (int r = 0; r < image.rows; ++r)
            memcpy(dst + r * hdr->stride, image.ptr(r), hdr->stride);
    }

    // Write params JSON into slot 0
    seqBegin(m_slot0);
    QByteArray utf8 = paramsJson.toUtf8();
    uint32_t plen = static_cast<uint32_t>(
        qMin(utf8.size(), static_cast<int>(sizeof(m_slot0->params_data) - 1)));
    memcpy(m_slot0->params_data, utf8.constData(), plen);
    m_slot0->params_data[plen] = '\0';
    m_slot0->params_len = plen;

    // Write command into slot 0
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::PushAndSearch);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    seqCommit(m_slot0);

    // Signal plugin via slot-0 event
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    Q_UNUSED(image) Q_UNUSED(paramsJson)
    return false;
#endif
}

// Write one image into image slot `slotIndex` (ImageHeader + pixels).
static void writeImageSlot(uint8_t* imgBase, int slotIndex, const cv::Mat& image)
{
    uint8_t* imgSlot = getImgSlot(imgBase, slotIndex);
    ImageHeader* hdr = reinterpret_cast<ImageHeader*>(imgSlot);
    hdr->width     = static_cast<uint32_t>(image.cols);
    hdr->height    = static_cast<uint32_t>(image.rows);
    hdr->channels  = static_cast<uint32_t>(image.channels());
    hdr->stride    = static_cast<uint32_t>(image.step);
    hdr->data_size = static_cast<uint32_t>(image.total() * image.elemSize());
    uint8_t* dst = imgSlot + sizeof(ImageHeader);
    if (image.isContinuous()) {
        memcpy(dst, image.data, hdr->data_size);
    } else {
        for (int r = 0; r < image.rows; ++r)
            memcpy(dst + r * hdr->stride, image.ptr(r), hdr->stride);
    }
}

// ----------------------------------------------------------------
// sendRunRecipe — multi-shot recipe over image slots 0..N-1
// ----------------------------------------------------------------
bool IpcClient::sendRunRecipe(const QVector<cv::Mat>& images, const QString& recipeJson) {
#ifdef _WIN32
    if (!m_hdr || !m_img) return false;
    if (images.isEmpty()) return false;
    const int n = qMin(images.size(), MAX_CAM_SLOTS);
    for (int s = 0; s < n; ++s)
        writeImageSlot(m_img, s, images[s]);
    // Clear the headers of unused slots so the plugin stops at the first empty
    // slot and never picks up stale data from a previous larger call.
    for (int s = n; s < MAX_CAM_SLOTS; ++s)
        memset(getImgSlot(m_img, s), 0, sizeof(ImageHeader));

    // Inline recipe JSON → params_data (mirrors sendPushAndSearch).
    seqBegin(m_slot0);
    QByteArray utf8 = recipeJson.toUtf8();
    uint32_t plen = static_cast<uint32_t>(
        qMin(utf8.size(), static_cast<int>(sizeof(m_slot0->params_data) - 1)));
    memcpy(m_slot0->params_data, utf8.constData(), plen);
    m_slot0->params_data[plen] = '\0';
    m_slot0->params_len = plen;

    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::RunRecipe);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    Q_UNUSED(images) Q_UNUSED(recipeJson)
    return false;
#endif
}

// ----------------------------------------------------------------
// Streaming recipe session (StartRecipe / PushShot / FinishRecipe)
// ----------------------------------------------------------------
bool IpcClient::sendStartRecipe(const QString& recipeJson) {
#ifdef _WIN32
    if (!m_slot0) return false;
    seqBegin(m_slot0);
    QByteArray utf8 = recipeJson.toUtf8();
    uint32_t plen = static_cast<uint32_t>(
        qMin(utf8.size(), static_cast<int>(sizeof(m_slot0->params_data) - 1)));
    memcpy(m_slot0->params_data, utf8.constData(), plen);
    m_slot0->params_data[plen] = '\0';
    m_slot0->params_len = plen;
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::StartRecipe);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    Q_UNUSED(recipeJson)
    return false;
#endif
}

bool IpcClient::sendPushShot(const cv::Mat& image, int shotIndex) {
#ifdef _WIN32
    if (!m_hdr || !m_img) return false;
    writeImageSlot(m_img, 0, image);   // streaming reuses slot 0 each time
    const QString params = QString(R"({"shot_index":%1})").arg(shotIndex);
    seqBegin(m_slot0);
    QByteArray utf8 = params.toUtf8();
    uint32_t plen = static_cast<uint32_t>(
        qMin(utf8.size(), static_cast<int>(sizeof(m_slot0->params_data) - 1)));
    memcpy(m_slot0->params_data, utf8.constData(), plen);
    m_slot0->params_data[plen] = '\0';
    m_slot0->params_len = plen;
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::PushShot);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    Q_UNUSED(image) Q_UNUSED(shotIndex)
    return false;
#endif
}

bool IpcClient::sendFinishRecipe() {
#ifdef _WIN32
    if (!m_slot0) return false;
    seqBegin(m_slot0);
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::FinishRecipe);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    m_slot0->params_len = 0;
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    return false;
#endif
}

bool IpcClient::sendResize(int x, int y, int w, int h) {
#ifdef _WIN32
    if (!m_slot0) return false;
    QString params = QString(R"({"x":%1,"y":%2,"w":%3,"h":%4})")
                         .arg(x).arg(y).arg(w).arg(h);
    seqBegin(m_slot0);
    QByteArray utf8 = params.toUtf8();
    uint32_t plen = static_cast<uint32_t>(
        qMin(utf8.size(), static_cast<int>(sizeof(m_slot0->params_data) - 1)));
    memcpy(m_slot0->params_data, utf8.constData(), plen);
    m_slot0->params_data[plen] = '\0';
    m_slot0->params_len = plen;
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::Resize);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    Q_UNUSED(x) Q_UNUSED(y) Q_UNUSED(w) Q_UNUSED(h)
    return false;
#endif
}

bool IpcClient::sendShutdown() {
#ifdef _WIN32
    if (!m_slot0) return false;
    seqBegin(m_slot0);
    m_slot0->cmd_id     = ++m_cmdSeq;
    m_slot0->cmd_type   = static_cast<uint32_t>(CmdType::Shutdown);
    m_slot0->cmd_status = static_cast<uint32_t>(CmdStatus::Pending);
    m_slot0->params_len = 0;
    seqCommit(m_slot0);
    SetEvent(static_cast<HANDLE>(m_hH2P));
    return true;
#else
    return false;
#endif
}

// ----------------------------------------------------------------
// run() — wait for P2H events (plugin results)
// ----------------------------------------------------------------
void IpcClient::run() {
#ifdef _WIN32
    if (!m_hdr && !openSharedMem()) return;  // fallback if not pre-opened

    // Waitable timer for heartbeat staleness check (2s interval)
    HANDLE hTimer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    LARGE_INTEGER due; due.QuadPart = -20000000LL; // 2s
    SetWaitableTimer(hTimer, &due, 2000, nullptr, nullptr, FALSE);

    HANDLE handles[2] = { static_cast<HANDLE>(m_hP2H), hTimer };

    while (!m_stopRequested) {
        DWORD r = WaitForMultipleObjects(2, handles, FALSE, 3000);

        if (r == WAIT_OBJECT_0) {
            // Plugin sent a result on slot 0
            if (m_stopRequested) break;
            uint32_t rlen = m_slot0->result_len;
            if (rlen > 0 && rlen < sizeof(m_slot0->result_data)) {
                QString json = QString::fromUtf8(m_slot0->result_data,
                                                 static_cast<int>(rlen));
                emit resultReceived(json);
            }
        } else if (r == WAIT_OBJECT_0 + 1) {
            // §10.7: detect a crashed plugin immediately via its PID, falling
            // back to heartbeat staleness (>5 s) only for the window before the
            // plugin registers or the PID-reuse edge case.
            if (!pluginProcessAlive()) {
                emit pluginDied();
            } else if (m_hdr->plugin_heartbeat > 0) {
                FILETIME ft; GetSystemTimeAsFileTime(&ft);
                ULARGE_INTEGER ui; ui.LowPart = ft.dwLowDateTime; ui.HighPart = ft.dwHighDateTime;
                uint64_t now = ui.QuadPart / 10000ULL;
                if (now - m_hdr->plugin_heartbeat > 5000)
                    emit pluginDied();
            }
        }
    }

    CancelWaitableTimer(hTimer);
    CloseHandle(hTimer);
    closeSharedMem();
#endif
}
