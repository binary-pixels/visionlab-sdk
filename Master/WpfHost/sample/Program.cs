// Minimal C# host sample for the VisionLab SDK (out-of-process integration).
//
//   1. create the shared-memory session;
//   2. launch the runtime (VisionLab.exe) attached to it;
//   3. push an image + algorithm params (PushAndSearch) and print the result.
//
// Build: dotnet build   (see SampleHost.csproj; needs the .NET SDK)
// Run:   place VisionLab.exe next to the built executable.
using System;
using System.Text.Json;
using System.Threading.Tasks;
using WpfHost;

internal static class Program
{
    private static async Task<int> Main()
    {
        // Synthesize a test image: a dark circle on a light background.
        const int W = 640, H = 480;
        var gray = new byte[W * H];
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
            {
                int dx = x - 320, dy = y - 240;
                gray[y * W + x] = (dx * dx + dy * dy <= 120 * 120) ? (byte)20 : (byte)230;
            }

        using var host = new PluginHost("SampleHost_" + Environment.ProcessId);
        host.OpenSharedMem();
        host.Launch("VisionLab.exe");       // adjust the path if needed

        var img = RawImage.FromGray(W, H, gray);
        const string parms = """
            { "algorithm": "circle_fit",
              "parameters": {
                "roi": { "center": { "x": 320, "y": 240 }, "inner_radius": 90, "outer_radius": 150 },
                "radius_range": { "min": 100, "max": 140 }
              } }
            """;

        var result = await host.PushAndSearchAsync(img, parms, TimeSpan.FromSeconds(10));
        Console.WriteLine(JsonSerializer.Serialize(result,
            new JsonSerializerOptions { WriteIndented = true }));

        await host.ShutdownAsync();
        return 0;
    }
}
