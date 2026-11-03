#!/usr/bin/env python
# -*- coding: utf-8 -*-
"""
出宣传图 + 顺带验证“两条鱼同时存在”：
写一个两条鱼、两株草、增氧器开着的 state.json，启动后截一张 _preview/hero.png。
用法： python tools/shot.py
"""
import ctypes
import ctypes.wintypes as wt
import json
import os
import subprocess
import time

from PIL import ImageGrab

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "x64", "Release", "PixelFishTank.exe")
EXEDIR = os.path.dirname(EXE)
STATE = os.path.join(EXEDIR, "state.json")
SHOTS = os.path.join(ROOT, "_preview")

user32 = ctypes.WinDLL("user32", use_last_error=True)
user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
EnumProc = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

state = {
    "version": 1,
    "window": {"x": 120, "y": 120, "w": 560, "h": 420},
    "environment": {"waterTemp": 25.4, "oxygen": 88.0, "ambientTemp": 24.0,
                     "weatherOnline": False},
    "equipment": {"aerator": True, "aeratorX": 34, "aeratorY": 224,
                   "heater": True, "heaterX": 292, "heaterY": 222, "heaterTarget": 26},
    "plants": [
        {"pack": "kelp", "x": 120, "y": 205, "scale": 1.0, "sway": 1.2},
        {"pack": "coral", "x": 250, "y": 215, "scale": 1.0, "sway": 2.0},
    ],
    "fish": [
        {"slot": 0, "pack": "goldfish", "x": 140, "y": 110, "targetX": 200,
         "targetY": 90, "facing": 1, "satiety": 82, "obesity": 0.2, "mood": 90,
         "alive": True, "deadTimer": 0},
        {"slot": 1, "pack": "guppy", "x": 210, "y": 150, "targetX": 160,
         "targetY": 170, "facing": -1, "satiety": 74, "obesity": 0.0, "mood": 88,
         "alive": True, "deadTimer": 0},
    ],
    "settings": {"autostart": False, "hotkeyMods": 3, "hotkeyVk": 70,
                  "clickThroughLocked": False, "weatherHost": "wttr.in",
                  "weatherPath": "/?format=j1", "heaterTarget": 26},
}


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


def main():
    os.makedirs(SHOTS, exist_ok=True)
    subprocess.run(["taskkill", "/F", "/IM", "PixelFishTank.exe"], capture_output=True)
    time.sleep(0.4)
    with open(STATE, "w", encoding="utf-8") as f:
        json.dump(state, f, ensure_ascii=False, indent=1)

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
        proc.kill()
        return 1
    time.sleep(1.6)
    r = wt.RECT()
    user32.GetWindowRect(hwnd, ctypes.byref(r))
    img = ImageGrab.grab(bbox=(r.left, r.top, r.right, r.bottom), all_screens=True)
    out = os.path.join(SHOTS, "hero.png")
    img.save(out)
    print("saved", out, img.size)

    user32.PostMessageW(hwnd, 0x0010, 0, 0)
    try:
        proc.wait(timeout=6)
    except subprocess.TimeoutExpired:
        proc.kill()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
