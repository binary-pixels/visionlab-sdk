#pragma once
// PlacementExpand — 把配方结果里的 placement_pattern 聚合展开为机器坐标位姿列表。
// 宿主据此驱动贴装头逐片贴装。展开规则与插件端
// DetectionWorker::runAggregateStep 完全一致：
//     machine(i) = O + R(θ)·local(i) ,  an = θ + local_angle(i)
// 见 docs/RECIPE_GUIDE.md §2.5 / docs/RECIPE_IPC_PROTOCOL.md §8。

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <cmath>

struct PlacementPose {
    double x  = 0.0;   // 机器坐标（单位 = frame.unit，mm 优先）
    double y  = 0.0;
    double an = 0.0;   // 姿态角（deg）
};

inline QVector<PlacementPose> expandPlacement(const QJsonObject& agg)
{
    constexpr double kPi = 3.14159265358979323846;
    QVector<PlacementPose> out;
    if (agg.value("algorithm").toString() != QStringLiteral("placement_pattern"))
        return out;

    // 片数 ≤16：插件已给出 poses[]（机器坐标），直接使用。
    const QJsonArray poses = agg.value("poses").toArray();
    const int count = agg.value("count").toInt(int(poses.size()));
    if (!poses.isEmpty() && int(poses.size()) == count) {
        for (const auto& pv : poses) {
            const QJsonObject p = pv.toObject();
            out.append({ p.value("x").toDouble(), p.value("y").toDouble(),
                         p.value("an").toDouble() });
        }
        return out;
    }

    const QJsonObject fr  = agg.value("frame").toObject();
    const QJsonObject pat = agg.value("pattern").toObject();
    const double ox = fr.value("ox").toDouble(), oy = fr.value("oy").toDouble();
    const double theta = fr.value("theta_deg").toDouble();
    const double th = theta * kPi / 180.0;
    const double ct = std::cos(th), st = std::sin(th);

    const QJsonObject org = pat.value("origin").toObject();
    const double lx0  = org.value("x").toDouble(), ly0 = org.value("y").toDouble();
    const double base = pat.value("angle").toDouble();
    const double dth  = pat.value("dtheta").toDouble();

    struct Loc { double x, y, an; };
    QVector<Loc> locs;
    const QString mode = pat.value("mode").toString(QStringLiteral("single"));
    if (mode == QStringLiteral("single")) {
        locs.append({ lx0, ly0, base });
    } else if (mode == QStringLiteral("line")) {
        const int n = pat.value("count").toInt(1);
        const double pitch = pat.value("pitch").toDouble();
        const double dd = pat.value("direction_deg").toDouble() * kPi / 180.0;
        for (int i = 0; i < n; ++i)
            locs.append({ lx0 + i * pitch * std::cos(dd),
                          ly0 + i * pitch * std::sin(dd), base + i * dth });
    } else if (mode == QStringLiteral("grid")) {
        const int rows = pat.value("rows").toInt(1), cols = pat.value("cols").toInt(1);
        const double px = pat.value("pitch_x").toDouble(), py = pat.value("pitch_y").toDouble();
        const bool snake = pat.value("snake").toBool(false);
        int idx = 0;
        for (int rr = 0; rr < rows; ++rr)
            for (int cc = 0; cc < cols; ++cc) {
                const int c = (snake && (rr % 2)) ? (cols - 1 - cc) : cc;
                locs.append({ lx0 + c * px, ly0 + rr * py, base + idx * dth });
                ++idx;
            }
    } else if (mode == QStringLiteral("list")) {
        for (const auto& pv : pat.value("points").toArray()) {
            const QJsonObject p = pv.toObject();
            locs.append({ p.value("x").toDouble(), p.value("y").toDouble(),
                          p.value("an").toDouble() });
        }
    }

    out.reserve(locs.size());
    for (const auto& L : locs)
        out.append({ ox + ct * L.x - st * L.y, oy + st * L.x + ct * L.y, theta + L.an });
    return out;
}

// 从完整结果 JSON 里取指定名字的 placement_pattern（name 为空则取第一个）。
inline QJsonObject findPlacement(const QJsonObject& recipeResult, const QString& name = {})
{
    for (const auto& av : recipeResult.value("aggregates").toArray()) {
        const QJsonObject a = av.toObject();
        if (a.value("algorithm").toString() != QStringLiteral("placement_pattern")) continue;
        if (name.isEmpty() || a.value("name").toString() == name) return a;
    }
    return {};
}
