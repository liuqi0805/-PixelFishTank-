#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
窗口行为验证：拖动移动 / 右下角缩放 / 尺寸上下限 / 全局热键隐藏显示。
全部用真实鼠标键盘事件驱动（SetCursorPos + mouse_event + keybd_event）。

用法： python tools/window_probe.py
"""
import ctypes
import ctypes.wintypes as wt
import os
import re
import subprocess
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "x64", "Release", "PixelFishTank.exe")
EXEDIR = os.path.dirname(EXE)
STATE = os.path.join(EXEDIR, "state.json")
DBG = os.path.join(os.environ.get("TEMP", "."), "pft_debug.txt")
WM_DUMP = 0x8000 + 7

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
    return (r.left, r.top, r.right - r.left, r.bottom - r.top)


def visible(hwnd):
    return bool(user32.IsWindowVisible(hwnd))


def dump(hwnd):
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
                out = {}
                for part in txt.split("|"):
                    part = part.strip()
                    if "=" not in part:
                        continue
                    k, v = part.split("=", 1)
                    m = re.match(r"\((-?\d+),(-?\d+),(-?\d+),(-?\d+)\)$", v.strip())
                    out[k] = tuple(int(g) for g in m.groups()) if m else v.strip()
                return out
        time.sleep(0.02)
    return {}


def drag(from_xy, to_xy, steps=12):
    user32.SetCursorPos(*from_xy)
    time.sleep(0.15)
    user32.mouse_event(0x0002, 0, 0, 0, 0)
    time.sleep(0.12)
    fx, fy = from_xy
    tx, ty = to_xy
    for i in range(1, steps + 1):
        x = int(fx + (tx - fx) * i / steps)
        y = int(fy + (ty - fy) * i / steps)
        user32.SetCursorPos(x, y)
        time.sleep(0.02)
    time.sleep(0.1)
    user32.mouse_event(0x0004, 0, 0, 0, 0)
    time.sleep(0.25)


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
        hwnd = find_window(proc.pid)
        if hwnd:
            break
        time.sleep(0.02)
    if not hwnd:
        print("找不到窗口")
        return 1
    time.sleep(0.6)

    rect0 = win_rect(hwnd)
    st = dump(hwnd)
    scale = float(st.get("scale", 1.0))
    print("rect0 =", rect0, "scale =", scale)
    results = {}

    # ---------- 1) 拖动移动窗口 ----------
    # 选一块远离鱼的空白水体（设计坐标），换算成屏幕坐标
    fish = None
    for k, v in st.items():
        if k.startswith("fish") and k.endswith(".pos"):
            fish = v
    cand = (60, 70)
    if fish and abs(fish[0] - cand[0]) < 50 and abs(fish[1] - cand[1]) < 50:
        cand = (250, 70)
    fx = rect0[0] + int(cand[0] * scale)
    fy = rect0[1] + int(cand[1] * scale)
    drag((fx, fy), (fx + 60, fy + 40))
    rect1 = win_rect(hwnd)
    print("after drag =", rect1)
    results["drag"] = (rect1[0] == rect0[0] + 60 and rect1[1] == rect0[1] + 40 and
                       rect1[2] == rect0[2] and rect1[3] == rect0[3])

    # ---------- 2) 右下角缩放 ----------
    # 角标 = 水体右下 13x13 设计单位（Ui::ResizeHandleRect）
    hx = rect1[0] + int((320 - 6 - 6) * scale)
    hy = rect1[1] + int((240 - 6 - 6) * scale)
    drag((hx, hy), (hx + 90, hy + 68))
    rect2 = win_rect(hwnd)
    print("after resize =", rect2)
    grew = rect2[2] > rect1[2] + 20
    ratio_ok = abs(rect2[2] / float(rect2[3]) - 320.0 / 240.0) < 0.03
    results["resize"] = grew and ratio_ok

    # 屏幕面积决定宽度的上下限（窗口面积 = 屏幕面积 / 32 ~ / 16，保持 4:3）
    sw = user32.GetSystemMetrics(0)
    sh = user32.GetSystemMetrics(1)
    area = sw * sh
    min_w = int((area / 32.0 * 320.0 / 240.0) ** 0.5)
    max_w = int((area / 16.0 * 320.0 / 240.0) ** 0.5)
    min_w = max(min_w, 180)
    print("screen = %dx%d, expected width in [%d, %d]" % (sw, sh, min_w, max_w))

    # 缩放后窗口尺寸变了，设计坐标系比例要重算
    def handle_screen(rect):
        sc = rect[2] / 320.0
        return (rect[0] + int((320 - 12) * sc), rect[1] + int((240 - 12) * sc))

    # ---------- 3) 缩得比下限还小：应被夹住 ----------
    rect3 = rect2
    hx, hy = handle_screen(rect3)
    drag((hx, hy), (hx - 600, hy - 450))
    rect4 = win_rect(hwnd)
    print("after shrink =", rect4)
    results["min_clamp"] = abs(rect4[2] - min_w) <= 3

    # ---------- 4) 放得比上限还大：应被夹住 ----------
    hx, hy = handle_screen(rect4)
    drag((hx, hy), (hx + 1200, hy + 900))
    rect5 = win_rect(hwnd)
    print("after grow =", rect5)
    results["max_clamp"] = abs(rect5[2] - max_w) <= 3

    # ---------- 5) 全局热键 Ctrl+Alt+F 隐藏/显示 ----------
    was = visible(hwnd)
    user32.keybd_event(0x11, 0, 0, 0)      # Ctrl down
    user32.keybd_event(0x12, 0, 0, 0)      # Alt down
    user32.keybd_event(0x46, 0, 0, 0)      # F down
    time.sleep(0.05)
    user32.keybd_event(0x46, 0, 2, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.keybd_event(0x11, 0, 2, 0)
    time.sleep(0.5)
    after1 = visible(hwnd)
    user32.keybd_event(0x11, 0, 0, 0)
    user32.keybd_event(0x12, 0, 0, 0)
    user32.keybd_event(0x46, 0, 0, 0)
    time.sleep(0.05)
    user32.keybd_event(0x46, 0, 2, 0)
    user32.keybd_event(0x12, 0, 2, 0)
    user32.keybd_event(0x11, 0, 2, 0)
    time.sleep(0.5)
    after2 = visible(hwnd)
    print("hotkey visible: %s -> %s -> %s" % (was, after1, after2))
    results["hotkey"] = (was and not after1 and after2)

    user32.PostMessageW(hwnd, 0x0010, 0, 0)
    try:
        proc.wait(timeout=8)
    except subprocess.TimeoutExpired:
        proc.kill()
    user32.SetCursorPos(saved_cursor[0], saved_cursor[1])

    for k, v in results.items():
        print("%-10s %s" % (k, "OK" if v else "FAIL"))
    return 0 if all(results.values()) else 2


if __name__ == "__main__":
    raise SystemExit(main())
