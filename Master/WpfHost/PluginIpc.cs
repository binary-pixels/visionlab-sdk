using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

namespace WpfHost
{
    // ── Shared memory layout (mirrors SharedMemLayout.h) ────────────────────

    // V3 shared-memory layout (mirrors shared/plugin_sdk/SharedMemLayout.h):
    //   Command block  20 KB = MultiCamHeader (4 KB) + 4 × CamSlot (4 KB each)
    //   Image block   128 MB = 4 × 32 MB slots, each a 64-B ImageHeader + pixels
    //   Events         Event_H2P/P2H_{shm}_0.._3  (per-slot)
    internal static class ShmConst
    {
        public const uint   Magic         = 0xCAFE1234u;
        public const uint   Version       = 3u;
        public const int    CamSlotSize   = 4096;
        public const int    MultiCamHeaderSize = 4096;
        public const int    CmdBlockSize  = MultiCamHeaderSize + 4 * CamSlotSize; // 20 KB
        public const int    ImgBlockSize  = 128 * 1024 * 1024; // 128 MB
        public const int    ImgSlotSize   = 32 * 1024 * 1024;  // 32 MB per slot
        public const int    ImgHeaderSize = 64;

        // MultiCamHeader field offsets
        public const int OffMagic       =   0;
        public const int OffVersion     =   4;
        public const int OffHostPid     =   8;
        public const int OffPluginPid   =  12;
        public const int OffCamCount    =  16;
        public const int OffHostHb      =  64;
        public const int OffPluginHb    =  72;

        // CamSlot[0] field offsets (absolute inside the command block)
        // CamSlot layout: cmd_id(0) cmd_type(4) cmd_status(8) timeout_ms(12)
        //   reserved[48] → params_len(64) params_data(68,2012) result_len(2080)
        //   result_data(2084,2012)
        public const int OffCmdId       = MultiCamHeaderSize +  0;
        public const int OffCmdType     = MultiCamHeaderSize +  4;
        public const int OffCmdStatus   = MultiCamHeaderSize +  8;
        public const int OffWriterSeq   = MultiCamHeaderSize + 16;  // P2-2 seqlock
        public const int OffParamsLen   = MultiCamHeaderSize + 64;
        public const int OffParamsData  = MultiCamHeaderSize + 68;
        public const int OffResultLen   = MultiCamHeaderSize + 2080;
        public const int OffResultData  = MultiCamHeaderSize + 2084;

        public const int MaxParamsData  = 2012;
        public const int MaxResultData  = 2012;
        public const int MaxCamSlots    = 4;
    }

    internal enum CmdType : uint
    {
        None          = 0,
        PushImage     = 1,
        LoadImageFile = 2,
        Search        = 3,
        PushAndSearch = 4,
        GrabAndSearch = 5,
        OpenCamera    = 6,
        GetParams     = 7,
        Resize        = 8,
        Shutdown      = 9,
        RunRecipe     = 10,  // V4 recipe: images in slots 0..N-1 + recipe JSON params
        StartRecipe   = 11,  // streaming session: params = full recipe JSON
        PushShot      = 12,  // streaming session: params {"shot_index":k} + image
        FinishRecipe  = 13,  // streaming session: aggregate staged shots, return
    }

    internal enum CmdStatus : uint
    {
        Idle    = 0,
        Pending = 1,
        Done    = 2,
        Error   = 3,
    }

    /// <summary>
    /// Pixel format of the image being sent to the plugin.
    /// </summary>
    public enum PixelFormat { Grayscale = 1, Bgr = 3 }

    /// <summary>
    /// Raw image data container.
    /// </summary>
    public sealed class RawImage
    {
        public int         Width    { get; }
        public int         Height   { get; }
        public PixelFormat Format   { get; }
        public int         Stride   { get; }
        public byte[]      Data     { get; }

        public RawImage(int width, int height, PixelFormat fmt, int stride, byte[] data)
        {
            Width  = width;
            Height = height;
            Format = fmt;
            Stride = stride;
            Data   = data;
        }

        /// <summary>Create from a BGR bitmap byte array (width × height × 3, row-major).</summary>
        public static RawImage FromBgr(int width, int height, byte[] bgrData)
            => new(width, height, PixelFormat.Bgr, width * 3, bgrData);

        /// <summary>Create from a grayscale byte array (width × height, row-major).</summary>
        public static RawImage FromGray(int width, int height, byte[] grayData)
            => new(width, height, PixelFormat.Grayscale, width, grayData);
    }

    /// <summary>
    /// Manages the CircleFitTester plugin process and the shared-memory IPC channel.
    /// Thread-safe for single concurrent command (send one command, await result, repeat).
    /// </summary>
    public sealed class PluginHost : IDisposable
    {
        private readonly string _shmName;
        private uint   _cmdSeq;
        private Process? _process;

        private MemoryMappedFile? _cmdMmf;
        private MemoryMappedFile? _imgMmf;
        private MemoryMappedViewAccessor? _cmdAcc;
        private MemoryMappedViewAccessor? _imgAcc;

        private EventWaitHandle? _h2p;   // Host → Plugin
        private EventWaitHandle? _p2h;   // Plugin → Host

        private CancellationTokenSource? _hbCts;
        private Task? _hbTask;

        public PluginHost(string shmName)
        {
            _shmName = shmName;
        }

        // ── Lifecycle ────────────────────────────────────────────────────────

        /// <summary>
        /// Create shared memory and named events.
        /// Call this before <see cref="Launch"/>.
        /// </summary>
        public void OpenSharedMem()
        {
            _cmdMmf = MemoryMappedFile.CreateOrOpen(
                $"SharedMem_Cmd_{_shmName}", ShmConst.CmdBlockSize,
                MemoryMappedFileAccess.ReadWrite);

            _imgMmf = MemoryMappedFile.CreateOrOpen(
                $"SharedMem_Img_{_shmName}", ShmConst.ImgBlockSize,
                MemoryMappedFileAccess.ReadWrite);

            _cmdAcc = _cmdMmf.CreateViewAccessor(0, ShmConst.CmdBlockSize);
            _imgAcc = _imgMmf.CreateViewAccessor(0, ShmConst.ImgBlockSize);

            // Write header (V3 MultiCamHeader); cam_count=1 — this host drives
            // CamSlot[0] only. Heartbeats live at MultiCamHeader offsets 64/72.
            _cmdAcc.Write(ShmConst.OffMagic,    ShmConst.Magic);
            _cmdAcc.Write(ShmConst.OffVersion,  ShmConst.Version);
            _cmdAcc.Write(ShmConst.OffHostPid,  (uint)Environment.ProcessId);
            _cmdAcc.Write(ShmConst.OffCamCount, (uint)1);

            // Named auto-reset events — V3 uses per-slot names (slot 0 here)
            _h2p = new EventWaitHandle(false, EventResetMode.AutoReset,
                                       $"Event_H2P_{_shmName}_0");
            _p2h = new EventWaitHandle(false, EventResetMode.AutoReset,
                                       $"Event_P2H_{_shmName}_0");
        }

        /// <summary>
        /// Launch the plugin process.
        /// </summary>
        /// <param name="pluginExe">Full path to CircleFitTester.exe</param>
        /// <param name="embeddedHwnd">
        ///   Window handle of the container to embed the plugin into,
        ///   or <see langword="null"/> for standalone mode.
        /// </param>
        public void Launch(string pluginExe, IntPtr? embeddedHwnd = null)
        {
            OpenSharedMem();
            Console.WriteLine($"[IPC] SharedMem opened, shmName={_shmName}");

            var args = $"--shm-name {_shmName}";
            if (embeddedHwnd.HasValue && embeddedHwnd.Value != IntPtr.Zero)
                args += $" --parent-hwnd 0x{embeddedHwnd.Value:X}";

            Console.WriteLine($"[IPC] Launching: {pluginExe} {args}");
            _process = new Process
            {
                StartInfo = new ProcessStartInfo(pluginExe, args)
                {
                    UseShellExecute = false,
                }
            };
            _process.Start();
            Console.WriteLine($"[IPC] Process started, PID={_process.Id}");

            // Start heartbeat
            _hbCts  = new CancellationTokenSource();
            _hbTask = HeartbeatLoopAsync(_hbCts.Token);
        }

        /// <summary>Send Shutdown and wait for plugin to exit.</summary>
        public async Task ShutdownAsync(TimeSpan? timeout = null)
        {
            var to = timeout ?? TimeSpan.FromSeconds(3);
            try
            {
                await SendCmdAndWaitAsync(CmdType.Shutdown, Array.Empty<byte>(),
                                          null, TimeSpan.FromSeconds(2));
            }
            catch { /* ignore */ }

            _hbCts?.Cancel();

            if (_process != null)
            {
                if (!_process.WaitForExit((int)to.TotalMilliseconds))
                    _process.Kill();
                _process.Dispose();
                _process = null;
            }
        }

        // ── Commands ─────────────────────────────────────────────────────────

        /// <summary>
        /// Send image + config to the plugin and wait for the fit result.
        /// </summary>
        /// <param name="image">Raw image (grayscale or BGR).</param>
        /// <param name="configJson">Algorithm config JSON string.</param>
        /// <param name="timeout">How long to wait for a result.</param>
        /// <returns>Parsed result as <see cref="JsonElement"/>.</returns>
        public async Task<JsonElement> PushAndSearchAsync(
            RawImage image, string configJson,
            TimeSpan? timeout = null)
        {
            var imgBytes    = EncodeImage(image);
            var paramsBytes = Encoding.UTF8.GetBytes(configJson);
            return await SendCmdAndWaitAsync(CmdType.PushAndSearch,
                                             paramsBytes, new[] { imgBytes },
                                             timeout ?? TimeSpan.FromSeconds(15));
        }

        /// <summary>
        /// Send a V4 multi-point recipe (CmdType::RunRecipe=10). Images are
        /// written to image slots 0..N-1 (N ≤ 4, one per recipe shot) and the
        /// recipe JSON is inlined in params_data. Uses the V3 shared-memory
        /// layout, identical to Master/QtHost.
        /// </summary>
        public async Task<JsonElement> RunRecipeAsync(
            IReadOnlyList<RawImage> images, string recipeJson,
            TimeSpan? timeout = null)
        {
            if (images == null || images.Count == 0 || images.Count > ShmConst.MaxCamSlots)
                throw new ArgumentException(
                    $"Recipe needs 1..{ShmConst.MaxCamSlots} images", nameof(images));

            var paramsBytes = Encoding.UTF8.GetBytes(recipeJson);
            var imgList     = images.Select(EncodeImage).ToList();
            return await SendCmdAndWaitAsync(CmdType.RunRecipe, paramsBytes, imgList,
                                             timeout ?? TimeSpan.FromSeconds(15),
                                             clearTrailingSlots: true);
        }

        /// <summary>Single-image convenience overload of <see cref="RunRecipeAsync"/>.</summary>
        public Task<JsonElement> RunRecipeAsync(
            RawImage image, string recipeJson, TimeSpan? timeout = null)
            => RunRecipeAsync(new[] { image }, recipeJson, timeout);

        /// <summary>
        /// Streaming recipe session (V3 offsets, one image slot per shot reused
        /// each time). Sequence:
        ///   StartRecipeAsync(recipeJson) → PushShotAsync(img, k) × N → FinishRecipeAsync()
        /// This lets the plugin process shot k while the stage moves to k+1.
        /// </summary>
        public async Task<JsonElement> StartRecipeAsync(string recipeJson, TimeSpan? timeout = null)
            => await SendCmdAndWaitAsync(CmdType.StartRecipe,
                                         Encoding.UTF8.GetBytes(recipeJson),
                                         null, timeout ?? TimeSpan.FromSeconds(10));

        public async Task<JsonElement> PushShotAsync(RawImage image, int shotIndex, TimeSpan? timeout = null)
        {
            var paramsBytes = Encoding.UTF8.GetBytes($"{{\"shot_index\":{shotIndex}}}");
            return await SendCmdAndWaitAsync(CmdType.PushShot, paramsBytes,
                                             new[] { EncodeImage(image) },
                                             timeout ?? TimeSpan.FromSeconds(10));
        }

        public async Task<JsonElement> FinishRecipeAsync(TimeSpan? timeout = null)
            => await SendCmdAndWaitAsync(CmdType.FinishRecipe, Array.Empty<byte>(),
                                         null, timeout ?? TimeSpan.FromSeconds(15));

        /// <summary>Request current algorithm parameters from the plugin.</summary>
        public async Task<JsonElement> GetParamsAsync(TimeSpan? timeout = null)
            => await SendCmdAndWaitAsync(CmdType.GetParams, Array.Empty<byte>(),
                                         null, timeout ?? TimeSpan.FromSeconds(5));

        /// <summary>Notify plugin of window resize (embedded mode).</summary>
        public void SendResize(int x, int y, int w, int h)
        {
            var p = Encoding.UTF8.GetBytes(
                $"{{\"x\":{x},\"y\":{y},\"w\":{w},\"h\":{h}}}");
            WriteCmd(CmdType.Resize, p, null);
            _h2p!.Set();
        }

        // ── Internal ─────────────────────────────────────────────────────────

        private static byte[] EncodeImage(RawImage img)
        {
            // V3 ImageHeader (64 B): width, height, channels, stride, data_size,
            //   roi_x, roi_y, orig_width, orig_height, reserved[28]
            // roi/orig stay 0 = full frame. The reserved bytes are already 0.
            byte[] header = new byte[ShmConst.ImgHeaderSize];
            BitConverter.GetBytes((uint)img.Width  ).CopyTo(header,  0);
            BitConverter.GetBytes((uint)img.Height ).CopyTo(header,  4);
            BitConverter.GetBytes((uint)img.Format ).CopyTo(header,  8);
            BitConverter.GetBytes((uint)img.Stride ).CopyTo(header, 12);
            BitConverter.GetBytes((uint)img.Data.Length).CopyTo(header, 16);

            byte[] result = new byte[header.Length + img.Data.Length];
            header.CopyTo(result, 0);
            img.Data.CopyTo(result, header.Length);
            return result;
        }

        private void WriteCmd(CmdType type, byte[] paramsBytes,
                              IReadOnlyList<byte[]>? images,
                              bool clearTrailingSlots = false)
        {
            // Write image slots 0..N-1 (each: V3 64-B header + pixels), N ≤ 4.
            if (images != null && images.Count > 0 && _imgAcc != null)
            {
                int n = Math.Min(images.Count, ShmConst.MaxCamSlots);
                for (int i = 0; i < n; ++i)
                {
                    byte[] img = images[i];
                    if (img.Length == 0) continue;
                    int len = Math.Min(img.Length, ShmConst.ImgSlotSize);
                    _imgAcc.WriteArray((long)i * ShmConst.ImgSlotSize, img, 0, len);
                }
                // Clear headers of unused slots so the plugin stops at the
                // first empty slot and never picks up stale data from a
                // previous, larger multi-image call (same policy as QtHost).
                if (clearTrailingSlots)
                {
                    byte[] zeroHeader = new byte[ShmConst.ImgHeaderSize];
                    for (int i = n; i < ShmConst.MaxCamSlots; ++i)
                        _imgAcc.WriteArray((long)i * ShmConst.ImgSlotSize,
                                           zeroHeader, 0, zeroHeader.Length);
                }
            }

            // P2-2 seqlock: bump to odd (writing) before touching the command
            // fields; bumped to even (committed) after, so the plugin only
            // acts on fully-committed, new commands.
            _cmdAcc!.Write(ShmConst.OffWriterSeq,
                           _cmdAcc.ReadUInt32(ShmConst.OffWriterSeq) + 1);

            // Write params
            int plen = Math.Min(paramsBytes.Length, ShmConst.MaxParamsData - 1);
            _cmdAcc.Write(ShmConst.OffParamsLen, (uint)plen);
            _cmdAcc.WriteArray(ShmConst.OffParamsData, paramsBytes, 0, plen);
            _cmdAcc.Write(ShmConst.OffParamsData + plen, (byte)0); // null-terminate

            // Write command (slot 0)
            unchecked { _cmdSeq++; }
            _cmdAcc.Write(ShmConst.OffCmdId,     _cmdSeq);
            _cmdAcc.Write(ShmConst.OffCmdType,   (uint)type);
            _cmdAcc.Write(ShmConst.OffCmdStatus, (uint)CmdStatus.Pending);

            // Commit (even)
            _cmdAcc.Write(ShmConst.OffWriterSeq,
                          _cmdAcc.ReadUInt32(ShmConst.OffWriterSeq) + 1);
        }

        private async Task<JsonElement> SendCmdAndWaitAsync(
            CmdType type, byte[] paramsBytes, IReadOnlyList<byte[]>? images,
            TimeSpan timeout, bool clearTrailingSlots = false)
        {
            if (_cmdAcc == null || _h2p == null || _p2h == null)
                throw new InvalidOperationException("Shared memory not open");

            int imgCount = images?.Count ?? 0;
            Console.WriteLine($"[IPC] SendCmd type={type}, paramsLen={paramsBytes.Length}, imgs={imgCount}");
            WriteCmd(type, paramsBytes, images, clearTrailingSlots);
            _h2p.Set();
            Console.WriteLine($"[IPC] H2P event set, waiting for P2H (timeout={timeout.TotalSeconds}s)...");

            // Wait for P2H on a thread-pool thread so we don't block the UI
            bool signalled = await Task.Run(() =>
                _p2h.WaitOne((int)timeout.TotalMilliseconds));

            if (!signalled)
            {
                Console.WriteLine("[IPC] TIMEOUT waiting for P2H");
                throw new TimeoutException("Plugin did not respond in time");
            }

            Console.WriteLine("[IPC] P2H signalled, reading result...");
            uint rlen = _cmdAcc.ReadUInt32(ShmConst.OffResultLen);
            Console.WriteLine($"[IPC] result_len={rlen}");
            if (rlen == 0 || rlen > ShmConst.MaxResultData)
                return JsonDocument.Parse("{}").RootElement;

            byte[] buf = new byte[rlen];
            _cmdAcc.ReadArray(ShmConst.OffResultData, buf, 0, (int)rlen);
            string json = Encoding.UTF8.GetString(buf);
            Console.WriteLine($"[IPC] result JSON: {json}");
            return JsonDocument.Parse(json).RootElement.Clone();
        }

        private async Task HeartbeatLoopAsync(CancellationToken ct)
        {
            while (!ct.IsCancellationRequested)
            {
                try { await Task.Delay(500, ct); }
                catch (OperationCanceledException) { break; }

                if (_cmdAcc == null) continue;

                // ms since Windows FILETIME epoch (matches plugin's writeHeartbeat)
                ulong now = (ulong)(DateTime.UtcNow
                    .ToFileTimeUtc() / 10000L);
                _cmdAcc.Write(ShmConst.OffHostHb, now);
            }
        }

        /// <summary>True if the plugin process is still running.</summary>
        public bool IsAlive => _process?.HasExited == false;

        /// <summary>Plugin process ID, or -1 if not running.</summary>
        public int ProcessId => _process?.Id ?? -1;

        /// <summary>True if the plugin heartbeat is fresh (not stale >5s).</summary>
        public bool IsPluginHeartbeatFresh(int staleMsThreshold = 5000)
        {
            if (_cmdAcc == null) return false;
            ulong hb = _cmdAcc.ReadUInt64(ShmConst.OffPluginHb);
            if (hb == 0) return false;
            ulong now = (ulong)(DateTime.UtcNow.ToFileTimeUtc() / 10000L);
            return (now - hb) < (ulong)staleMsThreshold;
        }

        // ── IDisposable ───────────────────────────────────────────────────────

        public void Dispose()
        {
            _hbCts?.Cancel();
            _hbCts?.Dispose();
            _cmdAcc?.Dispose();
            _imgAcc?.Dispose();
            _cmdMmf?.Dispose();
            _imgMmf?.Dispose();
            _h2p?.Dispose();
            _p2h?.Dispose();
            _process?.Dispose();
        }
    }
}
