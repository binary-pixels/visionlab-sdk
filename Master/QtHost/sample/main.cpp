// Minimal Qt host sample for the VisionLab SDK (out-of-process integration).
//
//   1. the host creates the shared-memory session and listens for results;
//   2. it launches the runtime (VisionLab.exe) attached to that session;
//   3. it pushes an image + algorithm params (PushAndSearch) and prints the result.
//
// Build: see CMakeLists.txt in this folder (needs Qt6 Core + OpenCV).
// Run:   place VisionLab.exe next to the built executable.
#include <QCoreApplication>
#include <QProcess>
#include <QTimer>
#include <QDebug>
#include <opencv2/opencv.hpp>
#include "IpcClient.h"
#include "PlacementExpand.h"   // findPlacement / expandPlacement (for placement recipes)

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    const QString shmName = QString("SampleHost_%1").arg(QCoreApplication::applicationPid());

    // 1. Host creates the shared memory + events, then starts listening.
    IpcClient ipc(shmName);
    if (!ipc.openSharedMem()) { qCritical() << "openSharedMem failed"; return 1; }
    QObject::connect(&ipc, &IpcClient::resultReceived, [&](const QString& json) {
        qInfo().noquote() << "result:" << json;
        app.quit();
    });
    QObject::connect(&ipc, &IpcClient::pluginDied, [&]() {
        qCritical() << "runtime died"; app.quit();
    });
    ipc.start();

    // 2. Launch the runtime on this session (adjust the path/name if needed).
    const QString runtimeExe = QCoreApplication::applicationDirPath() + "/VisionLab.exe";
    QProcess runtime;
    runtime.start(runtimeExe, { "--shm-name=" + shmName });
    if (!runtime.waitForStarted(5000)) {
        qCritical() << "failed to start" << runtimeExe; return 1;
    }

    // 3. After the runtime connects, push an image + params.
    QTimer::singleShot(1500, [&]() {
        // Synthesize a test image: dark circle on a light background.
        cv::Mat img(480, 640, CV_8UC1, cv::Scalar(230));
        cv::circle(img, cv::Point(320, 240), 120, cv::Scalar(20), -1);

        const QString params = R"({
            "algorithm": "circle_fit",
            "parameters": {
                "roi": { "center": {"x":320,"y":240}, "inner_radius":90, "outer_radius":150 },
                "radius_range": {"min":100,"max":140}
            }
        })";
        if (!ipc.sendPushAndSearch(img, params)) qCritical() << "sendPushAndSearch failed";
    });

    QTimer::singleShot(15000, [&]() { qCritical() << "timeout"; app.quit(); });

    const int rc = app.exec();
    ipc.requestStop();
    ipc.wait(2000);
    runtime.terminate();
    runtime.waitForFinished(2000);
    return rc;
}
