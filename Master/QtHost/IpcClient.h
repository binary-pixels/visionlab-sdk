#pragma once
#include <QThread>
#include <QString>
#include <QVector>
#include <opencv2/opencv.hpp>
#include "../../shared/plugin_sdk/SharedMemLayout.h"

// IpcClient runs in its own QThread (host side).
// It creates the shared memory and events, then waits for plugin results.
// The host GUI calls sendCmd*() to issue commands; results arrive via signals.
// Layout: V3 — MultiCamHeader + CamSlot[0..3]; this client uses slot 0 only.

class IpcClient : public QThread {
    Q_OBJECT

public:
    explicit IpcClient(const QString& shmName, QObject* parent = nullptr);
    ~IpcClient() override;

    // ---- Commands (call from GUI thread) ----
    // Send PushAndSearch: write image + paramsJson, signal plugin
    bool sendPushAndSearch(const cv::Mat& image, const QString& paramsJson);
    // Send RunRecipe (CmdType::RunRecipe=10): write images into slots 0..N-1
    // (≤ MAX_CAM_SLOTS) + inline recipe JSON to params_data; result is the
    // full semantic recipe JSON. See docs/RECIPE_IPC_PROTOCOL.md.
    bool sendRunRecipe(const QVector<cv::Mat>& images, const QString& recipeJson);
    // ── Streaming recipe session (StartRecipe / PushShot / FinishRecipe) ──
    // Feeds shots one at a time so the plugin can process shot k while the
    // stage moves to position k+1. Sequence:
    //   sendStartRecipe(recipeJson)
    //   sendPushShot(img0, 0); ... sendPushShot(imgN, N)
    //   sendFinishRecipe()  →  result = full semantic JSON
    bool sendStartRecipe (const QString& recipeJson);
    bool sendPushShot    (const cv::Mat& image, int shotIndex);
    bool sendFinishRecipe();
    // Send Resize command (plugin adjusts its window geometry)
    bool sendResize(int x, int y, int w, int h);
    // Send Shutdown
    bool sendShutdown();
    // Write host heartbeat (call periodically, e.g. from a QTimer)
    void writeHeartbeat();
    // Stop the result-listening thread
    void requestStop();
    // Open shared memory synchronously before start() — must be called from GUI thread
    bool openSharedMem();

signals:
    // Emitted when plugin replies with a result JSON
    void resultReceived(QString resultJson);
    // Emitted when plugin heartbeat goes stale (plugin likely crashed)
    void pluginDied();

protected:
    void run() override;   // listens for Event_P2H_*_0

private:
    void closeSharedMem();
    // §10.7: true while the plugin process (per header plugin_pid) is alive.
    // Returns true before the plugin registers (pid == 0) so the caller can
    // fall back to heartbeat staleness for the pre-registration window.
    bool pluginProcessAlive();

    QString   m_shmName;
    bool      m_stopRequested = false;

#ifdef _WIN32
    void*            m_hCmdFile = nullptr;
    void*            m_hImgFile = nullptr;
    void*            m_hH2P     = nullptr;   // Event_H2P_{name}_0
    void*            m_hP2H     = nullptr;   // Event_P2H_{name}_0
    void*            m_cmdBase  = nullptr;   // base of cmd mapping (for Unmap)
    uint8_t*         m_img      = nullptr;   // base of image mapping
    MultiCamHeader*  m_hdr      = nullptr;   // -> m_cmdBase + 0
    CamSlot*         m_slot0    = nullptr;   // -> m_cmdBase + sizeof(MultiCamHeader)
    uint32_t         m_cmdSeq   = 0;
#endif
};
