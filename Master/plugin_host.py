"""
CircleFitTester Plugin Host — Python / PyQt6
Communicates with CircleFitTester.exe via Windows shared memory + named events.

Requirements:
    pip install pyqt6 pywin32 numpy opencv-python

Usage:
    python plugin_host.py [CircleFitTester.exe]
"""

import ctypes
import ctypes.wintypes
import json
import math
import os
import struct
import subprocess
import sys
import threading
import time
from typing import Optional

import numpy as np
import cv2

import win32api
import win32con
import win32event

from PyQt6.QtCore import (Qt, QTimer, QThread, pyqtSignal, QSize)
from PyQt6.QtGui import (QImage, QPixmap, QFont, QColor)
from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QLabel, QPushButton,
    QComboBox, QCheckBox, QTextEdit, QFileDialog, QMessageBox,
    QHBoxLayout, QVBoxLayout, QSplitter, QSizePolicy,
)

# ── Shared memory layout V3 (mirrors shared/plugin_sdk/SharedMemLayout.h) ──
#   Command block  20 KB = MultiCamHeader (4 KB) + 4 × CamSlot (4 KB each)
#   Image block   128 MB = 4 × 32 MB slots, each a 64-B ImageHeader + pixels
#   Events         Event_H2P/P2H_{shm}_0.._3  (per-slot)
SHM_MAGIC       = 0xCAFE1234
SHM_VERSION     = 3
HEADER_SIZE     = 4096   # MultiCamHeader size
CAM_SLOT_SIZE   = 4096
CMD_BLOCK_SIZE  = HEADER_SIZE + 4 * CAM_SLOT_SIZE   # 20 KB
IMG_SLOT_SIZE   = 32 * 1024 * 1024
IMG_BLOCK_SIZE  = 4 * IMG_SLOT_SIZE                # 128 MB

CMD_PUSH_AND_SEARCH = 4
CMD_GET_PARAMS      = 7
CMD_RESIZE          = 8
CMD_SHUTDOWN        = 9
CMD_RUN_RECIPE      = 10   # V4 recipe: image(s) + inline recipe JSON in params
CMD_START_RECIPE    = 11   # streaming session: params = full recipe JSON
CMD_PUSH_SHOT       = 12   # streaming session: params {"shot_index":k} + image
CMD_FINISH_RECIPE   = 13   # streaming session: aggregate staged shots, return

STATUS_IDLE    = 0
STATUS_PENDING = 1
STATUS_DONE    = 2
STATUS_ERROR   = 3

# MultiCamHeader field offsets
OFF_MAGIC       =   0
OFF_VERSION     =   4
OFF_HOST_PID    =   8
OFF_PLUGIN_PID  =  12
OFF_CAM_COUNT   =  16
OFF_HOST_HB     =  64
OFF_PLUGIN_HB   =  72

# CamSlot[0] field offsets (absolute inside the command block)
# CamSlot layout: cmd_id(0) cmd_type(4) cmd_status(8) timeout_ms(12)
#   reserved[48] → params_len(64) params_data(68,2012) result_len(2080)
#   result_data(2084,2012)
OFF_CMD_ID      = HEADER_SIZE +  0
OFF_CMD_TYPE    = HEADER_SIZE +  4
OFF_CMD_STATUS  = HEADER_SIZE +  8
OFF_WRITER_SEQ  = HEADER_SIZE + 16   # P2-2 seqlock (odd=writing, even=committed)
OFF_PARAMS_LEN  = HEADER_SIZE + 64
OFF_PARAMS_DATA = HEADER_SIZE + 68
OFF_RESULT_LEN  = HEADER_SIZE + 2080
OFF_RESULT_DATA = HEADER_SIZE + 2084
MAX_PARAMS      = 2012
MAX_RESULT      = 2012
MAX_CAM_SLOTS   = 4

IMG_HEADER_SIZE = 64   # V3: w/h/ch/stride/data_size + roi_x/y/orig_w/h + reserved[28]


# ── 贴装阵列展开（frame + pattern → 机器坐标位姿列表）─────────────────────────
# 配方结果 aggregates[] 里的 placement_pattern 输出 frame + pattern（+ 片数少时的
# poses[]）。宿主据此展开每片芯片的机器坐标位姿：
#     machine(i) = O + R(θ)·local(i) ,  an = θ + local_angle(i)
# 与插件端 DetectionWorker::runAggregateStep 的展开规则完全一致。
# 详见 docs/RECIPE_PLUGIN… 见 docs/RECIPE_GUIDE.md §2.5 / RECIPE_IPC_PROTOCOL §8。
def expand_placement(agg: dict) -> list:
    """把 placement_pattern 聚合展开为机器坐标位姿列表 [{x,y,an}, ...]。
    agg 为结果 JSON aggregates[] 中的一项（含 frame/pattern/poses/count）。
    单位与 agg['frame']['unit'] 一致（mm 优先）。"""
    if not isinstance(agg, dict) or agg.get("algorithm") != "placement_pattern":
        raise ValueError("not a placement_pattern aggregate")
    # 片数 ≤16 时插件已给出 poses[]（机器坐标），直接用。
    poses = agg.get("poses")
    if isinstance(poses, list) and len(poses) == int(agg.get("count", len(poses))):
        return [{"x": float(p["x"]), "y": float(p["y"]), "an": float(p.get("an", 0.0))}
                for p in poses]

    fr = agg["frame"]
    pat = agg["pattern"]
    ox, oy = float(fr["ox"]), float(fr["oy"])
    theta = float(fr["theta_deg"])
    th = math.radians(theta)
    ct, st = math.cos(th), math.sin(th)
    org = pat.get("origin") or {"x": 0.0, "y": 0.0}
    lx0, ly0 = float(org.get("x", 0.0)), float(org.get("y", 0.0))
    base = float(pat.get("angle", 0.0))
    dth = float(pat.get("dtheta", 0.0))

    mode = pat.get("mode", "single")
    locs = []   # (lx, ly, lan)
    if mode == "single":
        locs.append((lx0, ly0, base))
    elif mode == "line":
        n = int(pat.get("count", 1))
        pitch = float(pat.get("pitch", 0.0))
        dd = math.radians(float(pat.get("direction_deg", 0.0)))
        for i in range(n):
            locs.append((lx0 + i * pitch * math.cos(dd),
                         ly0 + i * pitch * math.sin(dd), base + i * dth))
    elif mode == "grid":
        rows, cols = int(pat.get("rows", 1)), int(pat.get("cols", 1))
        px, py = float(pat.get("pitch_x", 0.0)), float(pat.get("pitch_y", 0.0))
        snake = bool(pat.get("snake", False))
        idx = 0
        for rr in range(rows):
            for cc in range(cols):
                c = (cols - 1 - cc) if (snake and rr % 2) else cc
                locs.append((lx0 + c * px, ly0 + rr * py, base + idx * dth))
                idx += 1
    elif mode == "list":
        for p in pat.get("points", []):
            locs.append((float(p["x"]), float(p["y"]), float(p.get("an", 0.0))))
    else:
        raise ValueError("unknown pattern mode: %s" % mode)

    return [{"x": ox + ct * lx - st * ly,
             "y": oy + st * lx + ct * ly,
             "an": theta + lan} for (lx, ly, lan) in locs]


def find_placement(recipe_result: dict, name: Optional[str] = None) -> Optional[dict]:
    """从结果 JSON 里取 placement_pattern 聚合（按 name，缺省取第一个）。"""
    for agg in recipe_result.get("aggregates", []):
        if agg.get("algorithm") == "placement_pattern":
            if name is None or agg.get("name") == name:
                return agg
    return None


def _ms_now() -> int:
    ft = ctypes.c_ulonglong()
    ctypes.windll.kernel32.GetSystemTimeAsFileTime(ctypes.byref(ft))
    return ft.value // 10000


# ── IPC core (no Qt dependency) ──────────────────────────────────────────────

class IpcClient:
    """Low-level shared memory + event IPC, host side."""

    def __init__(self, shm_name: str):
        self.shm_name = shm_name
        self._seq     = 0
        self._k32     = ctypes.windll.kernel32
        self._cmd_buf = None
        self._img_buf = None
        self._h_h2p   = None
        self._h_p2h   = None

    def open(self):
        PAGE_READWRITE = 0x04
        FILE_MAP_ALL   = 0xF001F

        # Must set restype to c_void_p so 64-bit pointers are not truncated
        # to signed 32-bit integers (which would make from_address() crash).
        self._k32.CreateFileMappingW.restype  = ctypes.wintypes.HANDLE
        self._k32.MapViewOfFile.restype       = ctypes.c_void_p

        hcmd = self._k32.CreateFileMappingW(
            ctypes.wintypes.HANDLE(-1), None, PAGE_READWRITE,
            0, CMD_BLOCK_SIZE, f"SharedMem_Cmd_{self.shm_name}")
        himg = self._k32.CreateFileMappingW(
            ctypes.wintypes.HANDLE(-1), None, PAGE_READWRITE,
            0, IMG_BLOCK_SIZE, f"SharedMem_Img_{self.shm_name}")
        if not hcmd or not himg:
            raise OSError(f"CreateFileMapping failed (err={ctypes.GetLastError()})")

        cp = self._k32.MapViewOfFile(hcmd, FILE_MAP_ALL, 0, 0, CMD_BLOCK_SIZE)
        ip = self._k32.MapViewOfFile(himg, FILE_MAP_ALL, 0, 0, IMG_BLOCK_SIZE)
        if not cp or not ip:
            raise OSError(f"MapViewOfFile failed (err={ctypes.GetLastError()})")

        self._cmd_buf = (ctypes.c_char * CMD_BLOCK_SIZE).from_address(cp)
        self._img_buf = (ctypes.c_char * IMG_BLOCK_SIZE).from_address(ip)

        # magic, version, host_pid, plugin_pid(0), cam_count(1) — this host
        # drives CamSlot[0] only.
        struct.pack_into("<IIIII", self._cmd_buf, OFF_MAGIC,
                         SHM_MAGIC, SHM_VERSION, os.getpid(), 0, 1)

        # V3 uses per-slot event names (slot 0 here)
        self._h_h2p = win32event.CreateEvent(None, False, False,
                                              f"Event_H2P_{self.shm_name}_0")
        self._h_p2h = win32event.CreateEvent(None, False, False,
                                              f"Event_P2H_{self.shm_name}_0")

    def write_heartbeat(self):
        if self._cmd_buf:
            struct.pack_into("<Q", self._cmd_buf, OFF_HOST_HB, _ms_now())

    def plugin_heartbeat_fresh(self, stale_ms=5000) -> bool:
        if not self._cmd_buf:
            return False
        hb = struct.unpack_from("<Q", self._cmd_buf, OFF_PLUGIN_HB)[0]
        return hb > 0 and (_ms_now() - hb) < stale_ms

    def send_push_and_search(self, image: np.ndarray, params_json: str) -> bool:
        if not self._cmd_buf:
            return False
        self._write_image(image)
        self._bump_writer_seq()                     # odd (writing)
        self._write_params(params_json.encode("utf-8"))
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_PUSH_AND_SEARCH, STATUS_PENDING)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)
        return True

    def send_run_recipe(self, images, recipe_json: str) -> bool:
        """
        Send a V4 multi-point recipe (CmdType::RunRecipe=10).

        `images` is a single np.ndarray (written to slot 0) or a sequence of
        np.ndarrays written to image slots 0..N-1 (N ≤ MAX_CAM_SLOTS). Uses the
        V3 shared-memory layout, identical to Master/QtHost.sendRunRecipe.
        """
        if not self._cmd_buf:
            return False
        if isinstance(images, np.ndarray):
            images = [images]
        if not images:
            return False
        self._write_images(images, clear_trailing=True)
        self._bump_writer_seq()                     # odd (writing)
        self._write_params(recipe_json.encode("utf-8"))
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_RUN_RECIPE, STATUS_PENDING)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)
        return True

    def send_start_recipe(self, recipe_json: str) -> bool:
        """Streaming session: cache the recipe, reset the staged-shot buffer."""
        if not self._cmd_buf:
            return False
        self._bump_writer_seq()                     # odd (writing)
        self._write_params(recipe_json.encode("utf-8"))
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_START_RECIPE, STATUS_PENDING)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)
        return True

    def send_push_shot(self, image: np.ndarray, shot_index: int) -> bool:
        """
        Streaming session: feed ONE shot image (slot 0, reused per shot); the
        plugin runs it immediately (overlapping with stage motion) and acks
        when done. V3 offsets.
        """
        if not self._cmd_buf:
            return False
        self._write_image(image)
        self._bump_writer_seq()                     # odd (writing)
        self._write_params(f'{{"shot_index":{shot_index}}}'.encode("utf-8"))
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_PUSH_SHOT, STATUS_PENDING)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)
        return True

    def send_finish_recipe(self) -> bool:
        """Streaming session: aggregate staged shots, return the full result."""
        if not self._cmd_buf:
            return False
        self._bump_writer_seq()                     # odd (writing)
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_FINISH_RECIPE, STATUS_PENDING)
        struct.pack_into("<I", self._cmd_buf, OFF_PARAMS_LEN, 0)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)
        return True

    def send_shutdown(self):
        if not self._cmd_buf:
            return
        self._bump_writer_seq()                     # odd (writing)
        self._seq += 1
        struct.pack_into("<III", self._cmd_buf, OFF_CMD_ID,
                         self._seq, CMD_SHUTDOWN, STATUS_PENDING)
        struct.pack_into("<I", self._cmd_buf, OFF_PARAMS_LEN, 0)
        self._bump_writer_seq()                     # even (committed)
        win32event.SetEvent(self._h_h2p)

    def wait_result(self, timeout_ms=10000) -> Optional[dict]:
        rc = win32event.WaitForSingleObject(self._h_p2h, timeout_ms)
        if rc != win32con.WAIT_OBJECT_0:
            return None
        rlen = struct.unpack_from("<I", self._cmd_buf, OFF_RESULT_LEN)[0]
        if rlen == 0 or rlen > MAX_RESULT:
            return {}
        raw = bytes(self._cmd_buf[OFF_RESULT_DATA: OFF_RESULT_DATA + rlen])
        try:
            return json.loads(raw.decode("utf-8"))
        except Exception:
            return {"status": "error", "raw": raw.decode("utf-8", errors="replace")}

    def _write_image(self, img: np.ndarray):
        """Write a single image to image slot 0 (legacy single-slot path)."""
        self._write_image_at(img, 0)

    def _write_images(self, images, clear_trailing: bool = False):
        """Write each image (V3 64-B header + pixels) into slots 0..N-1."""
        n = min(len(images), MAX_CAM_SLOTS)
        for i in range(n):
            self._write_image_at(images[i], i)
        # Clear headers of unused slots so the plugin stops at the first empty
        # slot and never picks up stale data from a previous larger call
        # (same policy as Master/QtHost).
        if clear_trailing:
            zero = bytes(IMG_HEADER_SIZE)
            for i in range(n, MAX_CAM_SLOTS):
                off = i * IMG_SLOT_SIZE
                self._img_buf[off: off + IMG_HEADER_SIZE] = zero

    def _write_image_at(self, img: np.ndarray, slot: int):
        arr = np.ascontiguousarray(img)
        h, w = arr.shape[:2]
        ch   = 1 if arr.ndim == 2 else arr.shape[2]
        stride = arr.strides[0]
        data   = arr.tobytes()
        # V3 ImageHeader (64 B): w, h, ch, stride, data_size,
        #   roi_x, roi_y, orig_width, orig_height, reserved[28]
        header = struct.pack("<IIIIIIIII28x", w, h, ch, stride, len(data),
                             0, 0, 0, 0)
        payload = header + data
        off = slot * IMG_SLOT_SIZE
        if off + len(payload) > IMG_BLOCK_SIZE:
            payload = payload[:IMG_BLOCK_SIZE - off]
        self._img_buf[off: off + len(payload)] = payload

    def _write_params(self, params: bytes):
        plen = min(len(params), MAX_PARAMS - 1)
        struct.pack_into("<I", self._cmd_buf, OFF_PARAMS_LEN, plen)
        ctypes.memmove(
            ctypes.c_char_p(ctypes.addressof(self._cmd_buf) + OFF_PARAMS_DATA),
            params[:plen], plen)
        struct.pack_into("B", self._cmd_buf, OFF_PARAMS_DATA + plen, 0)

    def _bump_writer_seq(self):
        """P2-2 seqlock: odd = writing, even = committed (read-modify-write)."""
        v = struct.unpack_from("<I", self._cmd_buf, OFF_WRITER_SEQ)[0]
        struct.pack_into("<I", self._cmd_buf, OFF_WRITER_SEQ, v + 1)


# ── Worker thread: waits for P2H result ─────────────────────────────────────

class ResultWatcher(QThread):
    result_received = pyqtSignal(str)
    plugin_died     = pyqtSignal()

    # Allow this many seconds after start before checking heartbeat
    STARTUP_GRACE_S = 6

    def __init__(self, ipc: IpcClient):
        super().__init__()
        self._ipc       = ipc
        self._stop      = False
        self._start_time = time.monotonic()

    def request_stop(self):
        self._stop = True
        if self._ipc._h_p2h:
            win32event.SetEvent(self._ipc._h_p2h)

    def run(self):
        print("[Watcher] thread started")
        while not self._stop:
            result = self._ipc.wait_result(timeout_ms=2000)
            if self._stop:
                break
            if result is None:
                elapsed = time.monotonic() - self._start_time
                print(f"[Watcher] timeout, elapsed={elapsed:.1f}s, grace={self.STARTUP_GRACE_S}s")
                if elapsed < self.STARTUP_GRACE_S:
                    continue
                fresh = self._ipc.plugin_heartbeat_fresh()
                print(f"[Watcher] heartbeat_fresh={fresh}")
                if not fresh:
                    print("[Watcher] emitting plugin_died")
                    self.plugin_died.emit()
                    break
            else:
                print(f"[Watcher] result received: {result}")
                self.result_received.emit(json.dumps(result, ensure_ascii=False))
        print("[Watcher] thread exiting")


# ── Module-level ctypes callbacks (must not be GC'd during EnumWindows) ──────

_EnumProc = ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_long)

def _make_enum_child_cb(pid: int, found: ctypes.c_void_p):
    def _cb(hwnd, _):
        wpid = ctypes.c_ulong(0)
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            found.value = hwnd
            return False
        return True
    return _EnumProc(_cb)

def _make_enum_top_cb(pid: int, found: ctypes.c_void_p):
    def _cb(hwnd, _):
        wpid = ctypes.c_ulong(0)
        ctypes.windll.user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and ctypes.windll.user32.IsWindowVisible(hwnd):
            found.value = hwnd
            return False
        return True
    return _EnumProc(_cb)


# ── Main window ──────────────────────────────────────────────────────────────

STYLE = """
QMainWindow, QWidget { background: #2b2b2b; color: #d4d4d4; }
QPushButton {
    background: #3c3f41; border: 1px solid #555; border-radius: 4px;
    padding: 5px 14px; color: #d4d4d4; min-width: 110px;
}
QPushButton:hover   { background: #4c5052; }
QPushButton:pressed { background: #2d5a8e; }
QPushButton:disabled { color: #555; }
QLabel   { color: #d4d4d4; }
QTextEdit {
    background: #1e1e1e; color: #9cdcfe;
    border: 1px solid #444; font-family: Consolas, monospace; font-size: 11px;
}
QComboBox {
    background: #3c3f41; border: 1px solid #555; border-radius: 4px;
    padding: 4px 8px; color: #d4d4d4;
}
QComboBox QAbstractItemView {
    background: #3c3f41; color: #d4d4d4;
    selection-background-color: #2d5a8e;
}
QSplitter::handle { background: #444; }
"""


class HostWindow(QMainWindow):
    def __init__(self, default_plugin_exe: str = ""):
        super().__init__()
        self.setWindowTitle("Vision Plugin Host")
        self.resize(1400, 800)
        self.setStyleSheet(STYLE)

        self._plugin_exe   = default_plugin_exe
        self._image: Optional[np.ndarray] = None
        self._config_json  = ""
        self._ipc: Optional[IpcClient]       = None
        self._process: Optional[subprocess.Popen] = None
        self._watcher: Optional[ResultWatcher]    = None

        # 配方流式模拟器状态
        self._recipe_json   = ""                 # 载入的配方 JSON
        self._recipe_path   = ""                 # 配方文件路径（大配方按路径发送）
        self._shot_images   = []                 # 各 shot 的图（按文件名排序）
        self._stream_active = False              # 流式会话进行中
        self._stream_next   = 0                  # 下一个待推 shot 下标
        self._recipe_cached_once = False         # 插件侧已缓存配方（常驻模式）

        self._hb_timer = QTimer(self)
        self._hb_timer.setInterval(500)
        self._hb_timer.timeout.connect(self._on_heartbeat)

        self._build_ui()

    # ── UI construction ──────────────────────────────────────────────────────

    def _build_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        root = QVBoxLayout(central)
        root.setContentsMargins(6, 6, 6, 6)
        root.setSpacing(6)

        # Toolbar
        toolbar = QWidget()
        toolbar.setFixedHeight(44)
        tb = QHBoxLayout(toolbar)
        tb.setContentsMargins(0, 0, 0, 0)
        tb.setSpacing(6)

        self._btn_launch = QPushButton("Launch Plugin")
        self._btn_open   = QPushButton("Open Image...")
        self._btn_config = QPushButton("Load Config...")
        self._btn_search = QPushButton("PushAndSearch")
        self._btn_search.setEnabled(False)

        # 配方模拟器（流式）
        self._btn_load_recipe = QPushButton("Load Recipe...")
        self._btn_shot_imgs   = QPushButton("Shot Images...")
        self._btn_stream_run  = QPushButton("Stream Run")
        self._chk_send_once   = QCheckBox("Send recipe once")
        self._chk_send_once.setToolTip(
            "配方常驻：仅首周期 StartRecipe 带配方，之后各周期发空 params 复用插件缓存")

        self._cmb_mode = QComboBox()
        self._cmb_mode.addItems(["Embedded", "Standalone"])
        self._cmb_mode.setFixedWidth(110)

        self._lbl_status = QLabel("Ready")
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")

        tb.addWidget(self._btn_launch)
        tb.addWidget(self._cmb_mode)
        tb.addSpacing(12)
        tb.addWidget(self._btn_open)
        tb.addWidget(self._btn_config)
        tb.addSpacing(12)
        tb.addWidget(self._btn_search)
        tb.addSpacing(12)
        tb.addWidget(self._btn_load_recipe)
        tb.addWidget(self._btn_shot_imgs)
        tb.addWidget(self._btn_stream_run)
        tb.addWidget(self._chk_send_once)
        tb.addStretch(1)
        tb.addWidget(self._lbl_status)
        root.addWidget(toolbar)

        # Splitter
        splitter = QSplitter(Qt.Orientation.Horizontal)
        splitter.setHandleWidth(4)

        # Left pane
        left = QWidget()
        left.setMinimumWidth(160)
        left.setMaximumWidth(300)
        lv = QVBoxLayout(left)
        lv.setContentsMargins(0, 0, 0, 0)
        lv.setSpacing(4)

        src_title = QLabel("Source Image")
        src_title.setStyleSheet("color: #888; font-size: 11px;")
        lv.addWidget(src_title)

        self._lbl_src = QLabel("No image")
        self._lbl_src.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._lbl_src.setStyleSheet("background: #1a1a1a; border: 1px solid #3a3a3a;")
        self._lbl_src.setSizePolicy(QSizePolicy.Policy.Expanding,
                                    QSizePolicy.Policy.Expanding)
        lv.addWidget(self._lbl_src, 1)

        res_title = QLabel("Result")
        res_title.setStyleSheet("color: #888; font-size: 11px;")
        lv.addWidget(res_title)

        self._txt_result = QTextEdit()
        self._txt_result.setReadOnly(True)
        self._txt_result.setFixedHeight(160)
        lv.addWidget(self._txt_result)

        splitter.addWidget(left)

        # Right pane — plugin window area
        right = QWidget()
        right.setMinimumWidth(400)
        rv = QVBoxLayout(right)
        rv.setContentsMargins(0, 0, 0, 0)
        rv.setSpacing(2)

        plugin_title = QLabel("Plugin Window")
        plugin_title.setStyleSheet("color: #888; font-size: 11px;")
        rv.addWidget(plugin_title)

        self._plugin_container = QWidget()
        self._plugin_container.setStyleSheet(
            "background: #1a1a1a; border: 1px solid #3a3a3a;")
        self._plugin_container.setSizePolicy(QSizePolicy.Policy.Expanding,
                                             QSizePolicy.Policy.Expanding)

        self._lbl_placeholder = QLabel("Plugin running as standalone window",
                                       self._plugin_container)
        self._lbl_placeholder.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._lbl_placeholder.setStyleSheet("color: #555; font-size: 13px;")
        self._lbl_placeholder.hide()
        ph_layout = QVBoxLayout(self._plugin_container)
        ph_layout.addWidget(self._lbl_placeholder)

        rv.addWidget(self._plugin_container, 1)
        splitter.addWidget(right)
        splitter.setSizes([220, 1180])
        splitter.setStretchFactor(0, 0)
        splitter.setStretchFactor(1, 1)
        root.addWidget(splitter, 1)

        # Poll timer for embedded HWND positioning
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(100)
        self._poll_timer.timeout.connect(self._reposition_plugin)

        # Connections
        self._btn_launch.clicked.connect(self._on_launch_clicked)
        self._btn_open.clicked.connect(self._on_open_image)
        self._btn_config.clicked.connect(self._on_load_config)
        self._btn_search.clicked.connect(self._on_send_search)
        self._btn_load_recipe.clicked.connect(self._on_load_recipe)
        self._btn_shot_imgs.clicked.connect(self._on_pick_shot_images)
        self._btn_stream_run.clicked.connect(self._on_stream_run)

    # ── Slots ────────────────────────────────────────────────────────────────

    def _on_launch_clicked(self):
        if self._process and self._process.poll() is None:
            self._stop_plugin()
        else:
            self._launch_plugin()

    def _launch_plugin(self):
        import traceback
        exe = self._plugin_exe
        print(f"[Host] _launch_plugin called, exe='{exe}'")
        if not exe or not os.path.exists(exe):
            exe, _ = QFileDialog.getOpenFileName(
                self, "Locate CircleFitTester.exe", "",
                "Executable (CircleFitTester.exe);;All files (*)")
            if not exe:
                print("[Host] No exe selected, aborting")
                return
            self._plugin_exe = exe

        print(f"[Host] exe resolved: {exe}")
        shm_name = f"PyHost_{os.getpid()}"
        print(f"[Host] shm_name={shm_name}")
        self._ipc = IpcClient(shm_name)
        try:
            self._ipc.open()
            print("[Host] IPC open() OK")
        except OSError as e:
            print(f"[Host] IPC open() FAILED: {e}")
            QMessageBox.critical(self, "Error", f"Failed to create shared memory:\n{e}")
            self._ipc = None
            return

        args = [exe, "--shm-name", shm_name]
        embedded = self._cmb_mode.currentIndex() == 0
        print(f"[Host] embedded={embedded}")
        if embedded:
            hwnd = int(self._plugin_container.winId())
            print(f"[Host] container winId={hwnd} ({hex(hwnd)})")
            args += ["--parent-hwnd", hex(hwnd)]
            self._lbl_placeholder.hide()
        else:
            self._lbl_placeholder.show()

        print(f"[Host] Popen args={args}")
        try:
            self._process = subprocess.Popen(args)
            print(f"[Host] Popen OK, pid={self._process.pid}")
        except Exception as e:
            print(f"[Host] Popen FAILED: {e}")
            traceback.print_exc()
            self._ipc = None
            return

        self._watcher = ResultWatcher(self._ipc)
        self._watcher.result_received.connect(self._on_result)
        self._watcher.plugin_died.connect(self._on_plugin_died)
        self._watcher.start()

        self._hb_timer.start()
        if embedded:
            self._plugin_hwnd = None
            self._poll_timer.start()

        self._cmb_mode.setEnabled(False)
        self._btn_launch.setText("Stop Plugin")
        self._btn_search.setEnabled(True)
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(f"Plugin running (PID {self._process.pid})")

    def _stop_plugin(self):
        self._hb_timer.stop()
        self._poll_timer.stop()
        self._btn_search.setEnabled(False)

        if self._ipc:
            self._ipc.send_shutdown()

        if self._watcher:
            self._watcher.request_stop()
            self._watcher.wait(2000)
            self._watcher = None

        if self._process:
            try:
                self._process.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self._process.kill()
            self._process = None

        self._ipc = None
        self._btn_launch.setText("Launch Plugin")
        self._cmb_mode.setEnabled(True)
        self._lbl_placeholder.hide()
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText("Plugin stopped")

    def _on_open_image(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Open Image", "",
            "Images (*.bmp *.png *.jpg *.jpeg)")
        if not path:
            return
        img = cv2.imread(path, cv2.IMREAD_GRAYSCALE)
        if img is None:
            QMessageBox.warning(self, "Error", "Failed to load image")
            return
        self._image = img
        self._update_src_label(img)
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(
            f"Image: {os.path.basename(path)}  ({img.shape[1]}×{img.shape[0]})")

    def _on_load_config(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "Load Config JSON", "", "JSON (*.json)")
        if not path:
            return
        with open(path, "r", encoding="utf-8") as f:
            self._config_json = f.read()
        self._txt_result.setPlainText("Config loaded:\n" + self._config_json)
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(f"Config: {os.path.basename(path)}")

    def _on_send_search(self):
        if not self._ipc:
            QMessageBox.warning(self, "Error", "Plugin not running")
            return
        if self._image is None:
            QMessageBox.warning(self, "Error", "No image loaded")
            return
        if not self._config_json:
            QMessageBox.warning(self, "Error", "No config JSON loaded")
            return

        self._btn_search.setEnabled(False)
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText("Searching...")
        if not self._ipc.send_push_and_search(self._image, self._config_json):
            self._lbl_status.setText("Failed to send command")
            self._btn_search.setEnabled(True)

    # ── 配方流式模拟器 ──────────────────────────────────────────────────────

    def _on_load_recipe(self):
        path, _ = QFileDialog.getOpenFileName(self, "Load Recipe JSON", "", "JSON (*.json)")
        if not path:
            return
        try:
            with open(path, "r", encoding="utf-8") as f:
                text = f.read()
            obj = json.loads(text)
        except Exception as e:
            QMessageBox.warning(self, "Error", f"Recipe parse error: {e}")
            return
        self._recipe_json = text
        self._recipe_path = path
        self._recipe_cached_once = False   # 新配方 → 需重新缓存
        n_shots = len(obj.get("shots", [])) if isinstance(obj, dict) else 0
        n_agg   = len(obj.get("aggregates", [])) if isinstance(obj, dict) else 0
        self._txt_result.setPlainText(
            "Recipe loaded:\n" + json.dumps(obj, indent=2, ensure_ascii=False))
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(
            f"Recipe: {os.path.basename(path)} (shots {n_shots}, aggregates {n_agg})")

    def _on_pick_shot_images(self):
        folder = QFileDialog.getExistingDirectory(
            self, "Select folder (one image per shot)")
        if not folder:
            return
        exts = (".bmp", ".png", ".jpg", ".jpeg")
        files = sorted(f for f in os.listdir(folder) if f.lower().endswith(exts))
        imgs = []
        for fn in files:
            img = cv2.imread(os.path.join(folder, fn), cv2.IMREAD_GRAYSCALE)
            if img is not None:
                imgs.append(img)
        if not imgs:
            QMessageBox.warning(self, "Error", "No usable images in folder")
            return
        self._shot_images = imgs
        self._update_src_label(imgs[0])
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(
            f"Shot images: {len(imgs)} (sorted by name → slots 0..{len(imgs)-1})")

    @staticmethod
    def _recipe_payload(recipe_json: str, recipe_path: str) -> str:
        # IPC 参数槽 ~2KB；大配方改送 {"recipe_file":"<路径>"}（<2KB），插件读文件。
        if len(recipe_json.encode("utf-8")) <= 1900 or not recipe_path:
            return recipe_json
        return json.dumps({"recipe_file": recipe_path})

    def _on_stream_run(self):
        if not self._ipc:
            QMessageBox.warning(self, "Error", "Plugin not running")
            return
        if not self._recipe_json:
            QMessageBox.warning(self, "Stream Run", "No recipe loaded (Load Recipe...)")
            return
        imgs = self._shot_images if self._shot_images else (
            [self._image] if self._image is not None else [])
        if not imgs:
            QMessageBox.warning(self, "Stream Run",
                                "No images (Open Image... or Shot Images...)")
            return
        if self._stream_active:
            QMessageBox.information(self, "Stream Run", "A stream session is already running")
            return
        self._stream_images = list(imgs)
        self._stream_active = True
        self._stream_next   = 0
        send_once = self._chk_send_once.isChecked()
        payload = self._recipe_payload(self._recipe_json, self._recipe_path)
        if send_once and self._recipe_cached_once:
            payload = ""   # 复用插件侧缓存
        self._btn_stream_run.setEnabled(False)
        self._txt_result.setPlainText(
            f"Starting stream session ({len(self._stream_images)} shots)...\n")
        self._lbl_status.setStyleSheet("color: #888; font-size: 11px;")
        self._lbl_status.setText(f"Streaming: StartRecipe ({len(self._stream_images)} shots)...")
        if not self._ipc.send_start_recipe(payload):
            self._stream_active = False
            self._btn_stream_run.setEnabled(True)
            self._lbl_status.setText("Failed to send StartRecipe")
        elif send_once:
            self._recipe_cached_once = True

    def _stream_push_next(self):
        if not self._ipc:
            self._stream_active = False
            self._btn_stream_run.setEnabled(True)
            return
        if self._stream_next < len(self._stream_images):
            self._lbl_status.setText(
                f"Streaming: PushShot {self._stream_next+1}/{len(self._stream_images)}...")
            if not self._ipc.send_push_shot(self._stream_images[self._stream_next], self._stream_next):
                self._stream_active = False
                self._btn_stream_run.setEnabled(True)
                self._lbl_status.setText("Failed to send PushShot")
        else:
            self._lbl_status.setText("Streaming: FinishRecipe...")
            if not self._ipc.send_finish_recipe():
                self._stream_active = False
                self._btn_stream_run.setEnabled(True)
                self._lbl_status.setText("Failed to send FinishRecipe")

    def _on_result(self, json_str: str):
        self._btn_search.setEnabled(True)
        try:
            data = json.loads(json_str)
            pretty = json.dumps(data, indent=2, ensure_ascii=False)
        except Exception:
            pretty = json_str
            data = {}

        # 流式会话：按 ack 顺序推进（Start ack → PushShot×N → Finish 结果）
        if self._stream_active:
            if data.get("status") == "error":
                self._stream_active = False
                self._btn_stream_run.setEnabled(True)
                self._txt_result.appendPlainText("session error: " + str(data.get("reason", "")))
                self._lbl_status.setStyleSheet("color:#d32f2f; font-weight:bold; font-size:11px;")
                self._lbl_status.setText("Stream error: " + str(data.get("reason", "")))
                return
            if "shot_index" in data:
                self._txt_result.appendPlainText(
                    f"shot {data['shot_index']} acked (ok={data.get('ok')})")
                self._stream_next += 1
                self._stream_push_next()
                return
            if isinstance(data.get("shots"), int):
                self._stream_next = 0
                self._stream_push_next()
                return
            self._stream_active = False   # 最终语义结果 → 落到下面显示
            self._btn_stream_run.setEnabled(True)

        self._txt_result.setPlainText(pretty)

        # 贴装阵列展开（frame+pattern → 机器坐标位姿）
        if isinstance(data, dict) and "aggregates" in data:
            place = find_placement(data)
            if place:
                try:
                    poses = expand_placement(place)
                    unit = (place.get("frame") or {}).get("unit", "mm")
                    lines = [f"  #{i+1}  x={p['x']:.3f}  y={p['y']:.3f}  an={p['an']:.2f}"
                             for i, p in enumerate(poses)]
                    self._txt_result.appendPlainText(
                        f"\n\nPlacement poses ({unit}) — machine coords:\n" + "\n".join(lines))
                except Exception:
                    pass

        if data.get("status") == "ok":
            self._lbl_status.setStyleSheet(
                "color: #16825d; font-weight: bold; font-size: 11px;")
            self._lbl_status.setText(data.get("detail", "Search OK"))
        else:
            self._lbl_status.setStyleSheet(
                "color: #d32f2f; font-weight: bold; font-size: 11px;")
            self._lbl_status.setText("Failed: " + data.get("reason", "unknown"))

    def _on_plugin_died(self):
        print("[Host] _on_plugin_died signal received")
        self._lbl_status.setStyleSheet(
            "color: #d32f2f; font-weight: bold; font-size: 11px;")
        self._lbl_status.setText("Plugin heartbeat lost")
        self._btn_search.setEnabled(False)
        self._stream_active = False
        self._recipe_cached_once = False   # 插件重启后其配方缓存失效
        self._btn_launch.setText("Launch Plugin")
        self._hb_timer.stop()

    def _on_heartbeat(self):
        if self._ipc:
            self._ipc.write_heartbeat()

    # ── Embedded window positioning ──────────────────────────────────────────

    def _reposition_plugin(self):
        if not self._process or self._process.poll() is not None:
            return

        w = self._plugin_container.width()
        h = self._plugin_container.height()
        if w <= 0 or h <= 0:
            return

        container_hwnd = int(self._plugin_container.winId())
        pid = self._process.pid

        hwnd = self._find_plugin_hwnd(container_hwnd, pid)
        if not hwnd:
            return

        user32 = ctypes.windll.user32
        # Ensure it's a child of the container
        if user32.GetParent(hwnd) != container_hwnd:
            user32.SetParent(hwnd, container_hwnd)

        user32.SetWindowPos(hwnd, None, 0, 0, w, h,
                            0x0004 | 0x0010)  # SWP_NOZORDER | SWP_NOACTIVATE
        self._poll_timer.stop()

    def _find_plugin_hwnd(self, container_hwnd: int, pid: int) -> int:
        found = ctypes.c_void_p(0)

        cb_child = _make_enum_child_cb(pid, found)
        ctypes.windll.user32.EnumChildWindows(container_hwnd, cb_child, 0)
        if found.value:
            return found.value

        cb_top = _make_enum_top_cb(pid, found)
        ctypes.windll.user32.EnumWindows(cb_top, 0)
        return found.value or 0

    # ── Helpers ──────────────────────────────────────────────────────────────

    def _update_src_label(self, img: np.ndarray):
        rgb = cv2.cvtColor(img, cv2.COLOR_GRAY2RGB) \
              if img.ndim == 2 else cv2.cvtColor(img, cv2.COLOR_BGR2RGB)
        h, w, ch = rgb.shape
        qi = QImage(rgb.data, w, h, w * ch, QImage.Format.Format_RGB888)
        px = QPixmap.fromImage(qi).scaled(
            self._lbl_src.size(),
            Qt.AspectRatioMode.KeepAspectRatio,
            Qt.TransformationMode.SmoothTransformation)
        self._lbl_src.setPixmap(px)

    def resizeEvent(self, event):
        super().resizeEvent(event)
        QTimer.singleShot(0, self._reposition_plugin)

    def closeEvent(self, event):
        self._stop_plugin()
        super().closeEvent(event)


# ── Entry point ──────────────────────────────────────────────────────────────

if __name__ == "__main__":
    import traceback

    def _excepthook(exc_type, exc_value, exc_tb):
        print("=== UNCAUGHT EXCEPTION ===")
        traceback.print_exception(exc_type, exc_value, exc_tb)
    sys.excepthook = _excepthook

    app = QApplication(sys.argv)
    app.setFont(QFont("Microsoft YaHei", 9))

    exe = sys.argv[1] if len(sys.argv) > 1 else ""
    win = HostWindow(default_plugin_exe=exe)
    win.show()
    sys.exit(app.exec())
