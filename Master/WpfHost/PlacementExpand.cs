using System;
using System.Collections.Generic;
using System.Text.Json;

namespace WpfHost
{
    /// <summary>机器坐标贴装位姿（单位 = frame.unit，mm 优先）。</summary>
    public readonly struct PlacementPose
    {
        public double X { get; init; }
        public double Y { get; init; }
        public double An { get; init; }   // 姿态角（deg）
    }

    /// <summary>
    /// 把配方结果里的 placement_pattern 聚合展开为机器坐标位姿列表。
    /// 展开规则与插件端 DetectionWorker::runAggregateStep 完全一致：
    ///     machine(i) = O + R(θ)·local(i) ,  an = θ + local_angle(i)
    /// 见 docs/RECIPE_GUIDE.md §2.5 / docs/RECIPE_IPC_PROTOCOL.md §8。
    /// </summary>
    public static class PlacementExpand
    {
        private const double Pi = 3.14159265358979323846;

        /// <summary>从完整结果 JSON 里取指定名字的 placement_pattern（name 为空取第一个）。</summary>
        public static JsonElement? FindPlacement(JsonElement recipeResult, string? name = null)
        {
            if (!recipeResult.TryGetProperty("aggregates", out var aggs) ||
                aggs.ValueKind != JsonValueKind.Array)
                return null;
            foreach (var a in aggs.EnumerateArray())
            {
                if (!a.TryGetProperty("algorithm", out var algo) ||
                    algo.GetString() != "placement_pattern") continue;
                if (string.IsNullOrEmpty(name) ||
                    (a.TryGetProperty("name", out var nm) && nm.GetString() == name))
                    return a;
            }
            return null;
        }

        /// <summary>展开一个 placement_pattern 聚合对象为机器坐标位姿列表。</summary>
        public static List<PlacementPose> Expand(JsonElement agg)
        {
            var outList = new List<PlacementPose>();
            if (!agg.TryGetProperty("algorithm", out var algo0) ||
                algo0.GetString() != "placement_pattern")
                return outList;

            // 片数 ≤16：插件已给出 poses[]（机器坐标），直接使用。
            if (agg.TryGetProperty("poses", out var poses) &&
                poses.ValueKind == JsonValueKind.Array)
            {
                int count = agg.TryGetProperty("count", out var cv) ? cv.GetInt32() : poses.GetArrayLength();
                if (poses.GetArrayLength() == count)
                {
                    foreach (var p in poses.EnumerateArray())
                        outList.Add(new PlacementPose
                        {
                            X  = p.GetProperty("x").GetDouble(),
                            Y  = p.GetProperty("y").GetDouble(),
                            An = p.TryGetProperty("an", out var an) ? an.GetDouble() : 0.0,
                        });
                    return outList;
                }
            }

            var fr  = agg.GetProperty("frame");
            var pat = agg.GetProperty("pattern");
            double ox = fr.GetProperty("ox").GetDouble();
            double oy = fr.GetProperty("oy").GetDouble();
            double theta = fr.GetProperty("theta_deg").GetDouble();
            double th = theta * Pi / 180.0;
            double ct = Math.Cos(th), st = Math.Sin(th);

            var org = pat.TryGetProperty("origin", out var o) ? o : default;
            double lx0 = org.ValueKind == JsonValueKind.Object ? org.GetProperty("x").GetDouble() : 0.0;
            double ly0 = org.ValueKind == JsonValueKind.Object ? org.GetProperty("y").GetDouble() : 0.0;
            double baseAng = pat.TryGetProperty("angle", out var ba) ? ba.GetDouble() : 0.0;
            double dth = pat.TryGetProperty("dtheta", out var dt) ? dt.GetDouble() : 0.0;

            var locs = new List<(double x, double y, double an)>();
            string mode = pat.TryGetProperty("mode", out var mv) ? (mv.GetString() ?? "single") : "single";
            switch (mode)
            {
                case "single":
                    locs.Add((lx0, ly0, baseAng));
                    break;
                case "line":
                {
                    int n = pat.GetProperty("count").GetInt32();
                    double pitch = pat.GetProperty("pitch").GetDouble();
                    double dd = (pat.TryGetProperty("direction_deg", out var dv) ? dv.GetDouble() : 0.0) * Pi / 180.0;
                    for (int i = 0; i < n; i++)
                        locs.Add((lx0 + i * pitch * Math.Cos(dd),
                                  ly0 + i * pitch * Math.Sin(dd), baseAng + i * dth));
                    break;
                }
                case "grid":
                {
                    int rows = pat.GetProperty("rows").GetInt32();
                    int cols = pat.GetProperty("cols").GetInt32();
                    double px = pat.GetProperty("pitch_x").GetDouble();
                    double py = pat.GetProperty("pitch_y").GetDouble();
                    bool snake = pat.TryGetProperty("snake", out var sv) && sv.GetBoolean();
                    int idx = 0;
                    for (int rr = 0; rr < rows; rr++)
                        for (int cc = 0; cc < cols; cc++)
                        {
                            int c = (snake && (rr % 2) == 1) ? (cols - 1 - cc) : cc;
                            locs.Add((lx0 + c * px, ly0 + rr * py, baseAng + idx * dth));
                            idx++;
                        }
                    break;
                }
                case "list":
                    foreach (var p in pat.GetProperty("points").EnumerateArray())
                        locs.Add((p.GetProperty("x").GetDouble(), p.GetProperty("y").GetDouble(),
                                  p.TryGetProperty("an", out var an) ? an.GetDouble() : 0.0));
                    break;
            }

            foreach (var (lx, ly, lan) in locs)
                outList.Add(new PlacementPose
                {
                    X  = ox + ct * lx - st * ly,
                    Y  = oy + st * lx + ct * ly,
                    An = theta + lan,
                });
            return outList;
        }
    }
}
