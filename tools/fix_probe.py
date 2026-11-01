"""验证本轮三个问题 + 两个改进的探针。

覆盖：
  1) 鼠标离开鱼身、移向【喂食/挑逗】按钮的途中，按钮是否还留着（0.4s 保持窗）；
  2) 连点几十下之后，逻辑线程是否还在推进（鱼有没有卡死）；
  3) 常驻菜单入口（右上角 ☰）能否点开/收起菜单；
  4) HUD（水温 / 溶氧）是否绘制在界面上；
  5) 托盘图标是否建立、WM_CLOSE 是否变成“隐藏到托盘”。

用法：python tools/fix_probe.py
截图落在 _preview/ 下。
"""

import ctypes
import json
import os
import subprocess
import sys
import time
from ctypes import wintypes

EXE = os.path.join("build", "x64", "Release", "PixelFishTank.exe")
STATE = os.path.join("build", "x64", "Release", "state.json")
PREVIEW = "_preview"
TEMP = os.environ.get("TEMP", ".")
DEBUG_FILE = os.path.join(TEMP, "pft_debug.txt")

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
gdi32 = ctypes.windll.gdi32

WM_CLOSE = 0x0010
WM_APP7 = 0x8000 + 7

RESULTS = []

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")


def ok(name, cond, extra=""):
    RESULTS.append((name, bool(cond), extra))
    print(("PASS  " if cond else "FAIL  ") + name + ((" | " + extra) if extra else ""))


# ---------------------------------------------------------------- 屏幕相关
class RECT(ctypes.Structure):
    _fields_ = [("left", wintypes.LONG), ("top", wintypes.LONG),
                ("right", wintypes.LONG), ("bottom", wintypes.LONG)]


user32.SetProcessDPIAware()


def screen_size():
    return user32.GetSystemMetrics(0), user32.GetSystemMetrics(1)


def window_rect(hwnd):
    r = RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


# ---------------------------------------------------------------- 鼠标注入
PUL = ctypes.POINTER(ctypes.c_ulong)


class MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG),
                ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", PUL)]


class INPUT(ctypes.Structure):
    class _U(ctypes.Union):
        _fields_ = [("mi", MOUSEINPUT)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", _U)]


def send_input(inp):
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


def move_to(x, y):
    sw, sh = screen_size()
    inp = INPUT()
    inp.type = 0
    inp.mi.dx = int(x * 65535 / (sw - 1))
    inp.mi.dy = int(y * 65535 / (sh - 1))
    inp.mi.dwFlags = 0x0001 | 0x8000 | 0x4000   # MOVE|ABSOLUTE|VIRTUALDESK
    send_input(inp)
    time.sleep(0.05)


def click(x, y):
    move_to(x, y)
    for flag in (0x0002, 0x0004):              # LEFTDOWN, LEFTUP
        inp = INPUT()
        inp.type = 0
        inp.mi.dwFlags = flag
        send_input(inp)
        time.sleep(0.02)


# ---------------------------------------------------------------- 窗口查找
def find_window(pid):
    found = []

    @ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            buf = ctypes.create_unicode_buffer(128)
            user32.GetClassNameW(hwnd, buf, 128)
            if buf.value == "PixelFishTankWindow":
                found.append(hwnd)
        return True

    user32.EnumWindows(cb, 0)
    return found[0] if found else None


# ---------------------------------------------------------------- 调试快照
def read_debug(hwnd, wait=0.25):
    if os.path.exists(DEBUG_FILE):
        os.remove(DEBUG_FILE)
    user32.PostMessageW(hwnd, WM_APP7, 0, 0)
    for _ in range(int(wait / 0.02) + 5):
        time.sleep(0.02)
        if os.path.exists(DEBUG_FILE):
            try:
                with open(DEBUG_FILE, "r", encoding="utf-8") as fp:
                    text = fp.read()
            except OSError:
                continue
            if text.strip():
                data = {}
                for part in text.split("|"):
                    if "=" in part:
                        k, v = part.split("=", 1)
                        data[k] = v
                return data
    return {}


def count_button_pixels(img, btn, scale):
    """数一数按钮区域里“浅青边框”颜色的像素，用来判断按钮还画没画在屏幕上。"""
    px = img.load()
    hits = 0
    x0 = max(0, int(btn[0] * scale) - 8)
    y0 = max(0, int(btn[1] * scale) - 8)
    x1 = min(img.width, int(btn[2] * scale) + 8)
    y1 = min(img.height, int(btn[3] * scale) + 8)
    for y in range(y0, y1):
        for x in range(x0, x1):
            r, g, b = px[x, y][:3]
            if r < 200 and g > 150 and b > 180:      # 面板亮边 / 按钮边框
                hits += 1
    return hits


def rect_of(data, key):
    raw = data.get(key, "")
    if not raw.startswith("("):
        return None
    nums = [int(float(v)) for v in raw.strip("()").split(",")]
    return nums


# ---------------------------------------------------------------- 截图
def shot(hwnd, path):
    from PIL import ImageGrab
    x, y, w, h = window_rect(hwnd)
    img = ImageGrab.grab(bbox=(x, y, x + w, y + h))
    img.save(os.path.join(PREVIEW, path))
    return img


def main():
    os.makedirs(PREVIEW, exist_ok=True)
    subprocess.run(["taskkill", "/F", "/IM", "PixelFishTank.exe"],
                   capture_output=True)
    time.sleep(0.3)
    if os.path.exists(STATE):
        os.remove(STATE)

    proc = subprocess.Popen([EXE], cwd=os.path.dirname(EXE))
    hwnd = None
    for _ in range(60):
        time.sleep(0.1)
        hwnd = find_window(proc.pid)
        if hwnd:
            break
    if not hwnd:
        print("FAIL  找不到窗口")
        return 1
    time.sleep(1.0)

    wx, wy, ww, wh = window_rect(hwnd)
    d0 = read_debug(hwnd)
    scale = float(d0.get("scale", "1").replace(",", ".")) if d0 else 1.0
    print("窗口:", (wx, wy, ww, wh), "scale =", scale)

    # ---- 改进 1：HUD 与菜单入口都在界面上（截图人工核对 + 命中测试辅助）----
    shot(hwnd, "fix_1_idle.png")
    menu_btn = rect_of(d0, "menuBtn")
    ok("常驻菜单入口存在", menu_btn is not None,
       str(menu_btn))
    ok("托盘图标已建立", d0.get("tray") == "1", "tray=" + d0.get("tray", "?"))

    # ---- 问题 1：鼠标离开鱼、去点按钮，按钮得留得住 ----
    fish = rect_of(d0, "fish0.pos")
    if not fish:
        print("FAIL  没找到鱼")
        return 1
    fx = wx + fish[0] * scale
    fy = wy + fish[1] * scale
    move_to(fx, fy)
    time.sleep(0.35)
    d1 = read_debug(hwnd)
    ok("悬停鱼身弹出按钮", d1.get("buttonsVisible") == "1",
       "buttonsVisible=" + d1.get("buttonsVisible", "?"))
    btn = rect_of(d1, "feedBtn")
    if not btn:
        print("FAIL  没有按钮矩形")
        return 1

    # 移到“鱼和按钮之间”的缝隙（离开鱼身、也还没进按钮）。
    # 注意：鱼一直在游，debug 往返本身就要 0.1~0.3 秒，所以这里用**截图**判可见性
    # （截图比 PostMessage 往返快得多），再补一次 debug 读数当参考。
    gap_x = wx + (btn[0] + 4) * scale
    gap_y = wy + (btn[3] + 3) * scale
    move_to(gap_x, gap_y)
    time.sleep(0.08)                      # 远小于 0.4s 的保持窗
    kept_shot = shot(hwnd, "fix_2_buttons_kept.png")
    buttons_on_screen = count_button_pixels(kept_shot, btn, scale)
    d2 = read_debug(hwnd)
    hold_left = float(d2.get("holdLeft", "0").replace(",", ".")) if d2.get("holdLeft") else -1.0
    ok("离开鱼身后按钮仍在（保持窗内）",
       buttons_on_screen > 40 or (d2.get("buttonsVisible") == "1" and hold_left > 0.05),
       "按钮像素=%d, buttonsVisible=%s, 保持窗剩余=%.2fs"
       % (buttons_on_screen, d2.get("buttonsVisible", "?"), hold_left))

    # 真的点一下喂食：坐标用最新读到的按钮矩形
    wx, wy, ww, wh = window_rect(hwnd)
    btn = rect_of(d2, "feedBtn") or btn
    bx = wx + (btn[0] + btn[2]) / 2 * scale
    by = wy + (btn[1] + btn[3]) / 2 * scale
    before = float(d1.get("satiety", "0").replace(",", ".")) if d1.get("satiety") else 0.0
    click(bx, by)
    time.sleep(0.45)
    d3 = read_debug(hwnd)
    after = float(d3.get("satiety", "0").replace(",", ".")) if d3.get("satiety") else 0.0
    ok("离开鱼身也能点到【喂食】", after > before + 5.0,
       "satiety %.1f -> %.1f" % (before, after))
    shot(hwnd, "fix_3_after_feed.png")

    # ---- 问题 2：连点压测，逻辑线程不能被饿死 ----
    tick0 = int(d3.get("logicTick", "0"))
    for i in range(40):
        click(fx, fy)                     # 连点鱼身 = 连续挑逗
    time.sleep(0.15)
    d4 = read_debug(hwnd, wait=0.5)
    tick1 = int(d4.get("logicTick", "0"))
    # 挑逗动画期间鱼会停在原地（这是设计），等动画播完再比较位置
    time.sleep(2.2)
    pos_a = rect_of(read_debug(hwnd, wait=0.5), "fish0.pos")
    time.sleep(0.9)
    d5 = read_debug(hwnd, wait=0.5)
    tick2 = int(d5.get("logicTick", "0"))
    pos_b = rect_of(d5, "fish0.pos")
    ok("连点 40 下后逻辑线程仍在跑", tick2 > tick1 and tick1 >= tick0,
       "logicTick %d -> %d -> %d" % (tick0, tick1, tick2))
    ok("连点后鱼仍在游动", pos_a != pos_b,
       "pos %s -> %s" % (pos_a, pos_b))
    shot(hwnd, "fix_4_after_spam.png")

    # ---- 问题 3：常驻菜单入口 ----
    wx, wy, ww, wh = window_rect(hwnd)      # 窗口万一被拖走过，这里重新对齐
    d_now = read_debug(hwnd)
    menu_btn = rect_of(d_now, "menuBtn") or menu_btn
    scale = float(d_now.get("scale", scale))
    mbx = wx + (menu_btn[0] + menu_btn[2]) / 2 * scale
    mby = wy + (menu_btn[1] + menu_btn[3]) / 2 * scale
    click(mbx, mby)
    time.sleep(0.3)
    d6 = read_debug(hwnd)
    ok("点 ☰ 打开常驻菜单", d6.get("menuOpen") == "1",
       "menuOpen=" + d6.get("menuOpen", "?"))
    shot(hwnd, "fix_5_menu.png")
    click(mbx, mby)
    time.sleep(0.3)
    d7 = read_debug(hwnd)
    ok("再点 ☰ 收起菜单", d7.get("menuOpen") == "0",
       "menuOpen=" + d7.get("menuOpen", "?"))

    # ---- 改进 2：关闭 -> 隐藏到托盘，进程还活着 ----
    user32.PostMessageW(hwnd, WM_CLOSE, 0, 0)
    time.sleep(0.6)
    d8 = read_debug(hwnd)
    alive = proc.poll() is None
    ok("关闭窗口后隐藏到托盘（未退出）", alive and d8.get("visible") == "0",
       "alive=%s visible=%s tray=%s" % (alive, d8.get("visible"), d8.get("tray")))

    # 任务栏通知区域打个截图，人工核对托盘图标
    sw, sh = screen_size()
    from PIL import ImageGrab
    ImageGrab.grab(bbox=(sw - 420, sh - 60, sw, sh)).save(
        os.path.join(PREVIEW, "fix_6_tray.png"))

    subprocess.run(["taskkill", "/F", "/IM", "PixelFishTank.exe"],
                   capture_output=True)

    failed = [r for r in RESULTS if not r[1]]
    print("")
    print("RESULT:", "ALL PASS" if not failed else ("%d FAIL" % len(failed)))
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
