import ctypes, os, subprocess, sys, time
from ctypes import wintypes as wt

sys.stdout.reconfigure(encoding="utf-8")
u = ctypes.windll.user32
u.SetProcessDPIAware()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "x64", "Release", "PixelFishTank.exe")
DBG = os.path.join(os.environ["TEMP"], "pft_debug.txt")

WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP = 0x0200, 0x0201, 0x0202
WM_CLOSE = 0x0010
WM_APP = 0x8000

def find_hwnd(pid):
    out = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            buf = ctypes.create_unicode_buffer(128); u.GetClassNameW(h, buf, 128)
            if buf.value == "PixelFishTankWindow": out.append(h)
        return True
    u.EnumWindows(cb, 0)
    return out[0] if out else None

def window_rect(hwnd):
    r = wt.RECT(); u.GetWindowRect(hwnd, ctypes.byref(r))
    return r.left, r.top, r.right - r.left, r.bottom - r.top

def read_debug(hwnd):
    if os.path.exists(DBG): os.remove(DBG)
    u.PostMessageW(hwnd, WM_APP + 7, 0, 0)
    txt = ""
    for _ in range(50):
        if os.path.exists(DBG):
            txt = open(DBG, encoding="utf-8", errors="replace").read()
            if txt.strip(): break
        time.sleep(0.02)
    d = {}
    for part in txt.strip().split("|"):
        if "=" in part:
            k, v = part.split("=", 1); d[k.strip()] = v.strip()
    return d

def rect_of(d, key):
    v = d.get(key) or ""
    try: return [float(x) for x in v.strip("()").split(",")]
    except Exception: return None

def post(hwnd, msg, x, y, wp=0):
    u.PostMessageW(hwnd, msg, wp, (int(y) << 16) | (int(x) & 0xFFFF))
    time.sleep(0.10)

def click(hwnd, x, y):
    post(hwnd, WM_MOUSEMOVE, x, y)
    post(hwnd, WM_LBUTTONDOWN, x, y, 0x0001)
    post(hwnd, WM_LBUTTONUP, x, y)

results = []
def ok(name, cond, extra=""):
    results.append((name, bool(cond), extra))
    print(("PASS  " if cond else "FAIL  ") + name + (" | " + extra if extra else ""))

subprocess.run(["taskkill", "/F", "/IM", "PixelFishTank.exe"], capture_output=True)
sp = os.path.join(ROOT, "build", "x64", "Release", "state.json")
if os.path.exists(sp): os.remove(sp)
p = subprocess.Popen([EXE])
hwnd = None
for _ in range(40):
    time.sleep(0.25)
    hwnd = find_hwnd(p.pid)
    if hwnd: break
    if p.poll() is not None: break
assert hwnd, "no window (exit=%s)" % p.poll()

wx, wy, w0, h0 = window_rect(hwnd)
d = read_debug(hwnd); scale = float(d["scale"])
print("window: (%d, %d, %d, %d) scale=%.4f  initial=%dx%d" % (wx, wy, w0, h0, scale, w0, h0))

# ---- B) 缩放 ----
u.PostMessageW(hwnd, WM_APP + 9, 300, 1); time.sleep(0.35)
wx, wy, w1, h1 = window_rect(hwnd)
ok("grow beyond old 1/16 cap", w1 > w0 + 250, "%dx%d -> %dx%d" % (w0, h0, w1, h1))
ok("grow keeps 4:3", abs(w1 * 3 - h1 * 4) <= 4, "%dx%d" % (w1, h1))
u.PostMessageW(hwnd, WM_APP + 9, -9999, 1); time.sleep(0.35)
wx, wy, w2, h2 = window_rect(hwnd)
min_w = int(w0 * 2 / 3)
ok("min = 2/3 of initial", abs(w2 - min_w) <= 4, "want %d got %d" % (min_w, w2))
ok("shrink keeps 4:3", abs(w2 * 3 - h2 * 4) <= 4, "%dx%d" % (w2, h2))
u.PostMessageW(hwnd, WM_APP + 9, 120, 3); time.sleep(0.35)
wx, wy, w3, h3 = window_rect(hwnd)
ok("bottom edge resize works", w3 > w2 + 60 and abs(w3 * 3 - h3 * 4) <= 4, "%dx%d -> %dx%d" % (w2, h2, w3, h3))

# ---- A) 菜单内容 ----
d = read_debug(hwnd); scale = float(d["scale"])
mb = rect_of(d, "menuBtn")
cx = (mb[0] + mb[2]) / 2 * scale; cy = (mb[1] + mb[3]) / 2 * scale
click(hwnd, cx, cy)
d = read_debug(hwnd)
items = d.get("menu", "")
print("menu:", items)
ok("click hamburger opens menu", d.get("menuOpen") == "1")
ok("resize items removed", "resize" not in items and "尺寸" not in items, items)
ok("menu has 8 items", int(d.get("menuCount", "0") or 0) == 8, "menuCount=" + d.get("menuCount", "?"))
click(hwnd, cx, cy)

# ---- C) 锁定穿透 ----
d = read_debug(hwnd); scale = float(d["scale"]); mb = rect_of(d, "menuBtn")
click(hwnd, (mb[0] + mb[2]) / 2 * scale, (mb[1] + mb[3]) / 2 * scale)
d = read_debug(hwnd)
mr = rect_of(d, "menuRect"); n = int(d.get("menuCount", "8") or 8)
ih = (mr[3] - mr[1]) / n
lockIdx = 4
for i, t in enumerate(d.get("menu", "").split(",")):
    if "锁定鼠标穿透" in t: lockIdx = i
click(hwnd, (mr[0] + mr[2]) / 2 * scale, (mr[1] + (lockIdx + 0.5) * ih) * scale)
time.sleep(0.25)
d = read_debug(hwnd)
ok("toggle lock via menu", d.get("locked") == "1", "locked=" + d.get("locked", "?"))

time.sleep(0.25)
ex1 = u.GetWindowLongW(hwnd, -20) & 0xFFFFFFFF
ok("locked: whole window click-through", (ex1 & 0x20) != 0, "exstyle=0x%08X" % ex1)

cp = wt.POINT(); u.GetCursorPos(ctypes.byref(cp))
d = read_debug(hwnd); scale = float(d["scale"]); mb = rect_of(d, "menuBtn")
mx = (mb[0] + mb[2]) / 2 * scale; my = (mb[1] + mb[3]) / 2 * scale
u.SetWindowPos(hwnd, 0, int(cp.x - mx), int(cp.y - my), 0, 0, 0x0001 | 0x0004 | 0x0010)
time.sleep(0.40)
print("moved to:", window_rect(hwnd), "target:", int(cp.x - mx), int(cp.y - my), "cursor:", cp.x, cp.y)
ex2 = u.GetWindowLongW(hwnd, -20) & 0xFFFFFFFF
d2 = read_debug(hwnd)
ok("cursor over hamburger: not click-through", (ex2 & 0x20) == 0, "exstyle=0x%08X hot=%s mouse=%s menuBtn=%s" % (ex2, d2.get("hot"), d2.get("mouse"), d2.get("menuBtn")))

d = read_debug(hwnd)
mb = rect_of(d, "menuBtn")
click(hwnd, (mb[0] + mb[2]) / 2 * scale, (mb[1] + mb[3]) / 2 * scale)
d = read_debug(hwnd)
ok("locked: hamburger still opens menu", d.get("menuOpen") == "1", "menuOpen=" + d.get("menuOpen", "?"))

mr = rect_of(d, "menuRect"); ih = (mr[3] - mr[1]) / n
click(hwnd, (mr[0] + mr[2]) / 2 * scale, (mr[1] + (lockIdx + 0.5) * ih) * scale)
time.sleep(0.25)
d = read_debug(hwnd)
ok("unlock works", d.get("locked") == "0", "locked=" + d.get("locked", "?"))

u.SetWindowPos(hwnd, 0, 0, 0, 0, 0, 0x0001 | 0x0004)
u.PostMessageW(hwnd, WM_CLOSE, 0, 0); time.sleep(0.7)
ok("close parks to tray", p.poll() is None, "alive=%s" % (p.poll() is None))
if p.poll() is None: p.kill()

bad = [r for r in results if not r[1]]
print("RESULT: " + ("ALL PASS" if not bad else "FAIL %d/%d" % (len(bad), len(results))))
sys.exit(0 if not bad else 1)
