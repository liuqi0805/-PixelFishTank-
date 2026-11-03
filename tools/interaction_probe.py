#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
交互验证（自动化）：用真实光标 + 真实点击驱动 PixelFishTank.exe
  1) 把光标移到鱼身上 -> 弹出【喂食 / 挑逗】
  2) 点【喂食】 -> 饱食度上升（退出后从 state.json 读回）
  3) 把光标移到水体空白 -> 弹出控制面板 -> 点【增氧器】
全程截图落在 _preview/ 下，肉眼可核对。

依赖程序里的调试出口 WM_APP+7（把命中矩形写到 %TEMP%/pft_debug.txt），
这样测试不用猜按钮坐标。
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import re
import subprocess
import time

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "x64", "Release", "PixelFishTank.exe")
EXEDIR = os.path.dirname(EXE)
STATE = os.path.join(EXEDIR, "state.json")
SHOTS = os.path.join(ROOT, "_preview")
DBG = os.path.join(os.environ.get("TEMP", "."), "pft_debug.txt")
WM_DUMP = 0x8000 + 7

os.makedirs(SHOTS, exist_ok=True)

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))

EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)


def find_window(pid):
    found = []

    def cb(hwnd, lparam):
        p = wt.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(p))
        if p.value == pid:
            buf = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, buf, 256)
            if buf.value == "PixelFishTankWindow":
                found.append(hwnd)
                return False
        return True

    user32.EnumWindows(EnumProc(cb), 0)
    return found[0] if found else None


def win_rect(hwnd):
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top


def grab(hwnd):
    x, y, w, h = win_rect(hwnd)
    return ImageGrab.grab(bbox=(x, y, x + w, y + h), all_screens=True)


def dump_state(hwnd):
    if os.path.exists(DBG):
        os.remove(DBG)
    user32.PostMessageW(hwnd, WM_DUMP, 0, 0)
    for _ in range(50):
        if os.path.exists(DBG):
            time.sleep(0.02)
            try:
                txt = open(DBG, encoding="utf-8-sig").read()
            except OSError:
                txt = ""
            if txt:
                return txt
        time.sleep(0.02)
    return ""


def parse_dump(txt):
    out = {}
    for part in txt.split("|"):
        part = part.strip()
        if not part or "=" not in part:
            continue
        k, v = part.split("=", 1)
        m = re.match(r"\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)$", v.strip())
        if m:
            out[k] = tuple(int(g) for g in m.groups())
        else:
            out[k] = v.strip()
    return out


def cur(hwnd, dx, dy, scale):
    """设计坐标 -> 屏幕坐标（窗口原点 + 设计坐标 * 缩放）"""
    x, y, _, _ = win_rect(hwnd)
    return x + int(dx * scale), y + int(dy * scale)


def move_to(sx, sy):
    user32.SetCursorPos(sx, sy)
    time.sleep(0.12)


class _MOUSEINPUT(ctypes.Structure):
    _fields_ = [("dx", ctypes.c_long), ("dy", ctypes.c_long), ("mouseData", ctypes.c_ulong),
                ("dwFlags", ctypes.c_ulong), ("time", ctypes.c_ulong),
                ("dwExtraInfo", ctypes.POINTER(ctypes.c_ulong))]


class _INPUT(ctypes.Structure):
    _fields_ = [("type", ctypes.c_ulong), ("mi", _MOUSEINPUT)]


def _send_mouse(flags):
    mi = _MOUSEINPUT(0, 0, 0, flags, 0, None)
    user32.SendInput(1, ctypes.byref(_INPUT(0, mi)), ctypes.sizeof(_INPUT))


def real_click(sx, sy):
    """真实左键点击。
    注意用 SendInput 而不是 mouse_event：后者在某些环境里会触发两次
    （一次点击被当成按下+抬起两次消息），对“一个按钮切一次状态”的逻辑等于没点。"""
    user32.SetCursorPos(sx, sy)
    time.sleep(0.10)
    _send_mouse(0x0002)   # LEFTDOWN
    time.sleep(0.06)
    _send_mouse(0x0004)   # LEFTUP
    time.sleep(0.20)


def main():
    subprocess.run(["taskkill", "/F", "/IM", "PixelFishTank.exe"], capture_output=True)
    time.sleep(0.4)
    if os.path.exists(STATE):
        os.remove(STATE)

    pt = wt.POINT()
    user32.GetCursorPos(ctypes.byref(pt))
    saved_cursor = (pt.x, pt.y)

    proc = subprocess.Popen([EXE], cwd=EXEDIR)
    hwnd = None
    t0 = time.time()
    while time.time() - t0 < 8:
        if proc.poll() is not None:
            print("进程提前退出 rc=%s" % proc.returncode)
            return 1
        hwnd = find_window(proc.pid)
        if hwnd:
            break
        time.sleep(0.02)
    if not hwnd:
        print("找不到窗口")
        return 1
    print("hwnd=%s rect=%s" % (hwnd, win_rect(hwnd)))
    time.sleep(0.5)

    ok_feed = False
    ok_panel = False
    st = parse_dump(dump_state(hwnd))
    scale = float(st.get("scale", 1.0))
    print("dump:", st)

    # ---------- 1) 悬停鱼 ----------
    for attempt in range(20):
        st = parse_dump(dump_state(hwnd))
        fish = None
        for k, v in st.items():
            if k.startswith("fish") and k.endswith(".pos"):
                fish = v
                break
        if not fish:
            print("没有鱼，跳过喂食")
            break
        fx, fy = (fish[0] + fish[2]) / 2.0, (fish[1] + fish[3]) / 2.0
        if not (40 < fx < 280 and 30 < fy < 200):
            time.sleep(0.3)
            continue
        sx, sy = cur(hwnd, fx, fy, scale)
        move_to(sx, sy)
        st2 = parse_dump(dump_state(hwnd))
        if st2.get("hoverFish", "-1") != "-1":
            print("hover fish ok, hoverFish =", st2.get("hoverFish"))
            grab(hwnd).save(os.path.join(SHOTS, "probe_1_fish_hover.png"))
            fb = st2.get("feedBtn")
            if fb and st2.get("buttonsVisible") == "1":
                cx = (fb[0] + fb[2]) / 2.0
                cy = (fb[1] + fb[3]) / 2.0
                bx, by = cur(hwnd, cx, cy, scale)
                real_click(bx, by)
                grab(hwnd).save(os.path.join(SHOTS, "probe_2_after_feed.png"))
                ok_feed = True
                break
        time.sleep(0.2)

    # ---------- 3) 悬停水体空白 + 点增氧器 ----------
    st3 = {}
    for cand in [(240, 60), (60, 70), (240, 190), (160, 60), (100, 200), (200, 120)]:
        sx, sy = cur(hwnd, cand[0], cand[1], scale)
        move_to(sx, sy)
        st3 = parse_dump(dump_state(hwnd))
        if st3.get("panelVisible") == "1":
            break
    if st3.get("panelVisible") == "1":
        print("panel visible:", st3.get("panelRect"))
        grab(hwnd).save(os.path.join(SHOTS, "probe_3_panel.png"))
        pr = st3.get("panelRect")
        if pr:
            # 面板第一个按钮 = 增氧器（Ui::UpdateLayout：pad=4, bh=13）
            bx, by = cur(hwnd, pr[0] + 4 + 20, pr[1] + 4 + 6, scale)
            real_click(bx, by)
            time.sleep(0.3)
            grab(hwnd).save(os.path.join(SHOTS, "probe_4_aerator.png"))
            ok_panel = True
    else:
        print("panel not visible, dump:", st3)

    user32.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    user32.SetCursorPos(saved_cursor[0], saved_cursor[1])
    print("exit code =", proc.returncode)

    if not os.path.exists(STATE):
        print("state.json 缺失")
        return 2
    with open(STATE, "r", encoding="utf-8-sig") as f:
        saved_state = json.load(f)
    fishes = saved_state.get("fish") or []
    sat = fishes[0].get("satiety") if fishes else None
    aer = (saved_state.get("equipment") or {}).get("aerator")
    print("satiety = %s, aerator = %s" % (sat, aer))
    ok_feed = ok_feed and sat is not None and sat > 75.0
    ok_panel = ok_panel and aer is True
    print("FEED %s / AERATOR %s" % ("OK" if ok_feed else "FAIL", "OK" if ok_panel else "FAIL"))
    return 0 if (ok_feed and ok_panel) else 2


if __name__ == "__main__":
    raise SystemExit(main())
