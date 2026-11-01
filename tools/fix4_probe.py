import ctypes, os, subprocess, sys, time
from ctypes import wintypes as wt

sys.stdout.reconfigure(encoding="utf-8")
u = ctypes.windll.user32
u.SetProcessDPIAware()

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EXE = os.path.join(ROOT, "build", "x64", "Release", "PixelFishTank.exe")
DBG = os.path.join(os.environ["TEMP"], "pft_debug.txt")

WM_MOUSEMOVE, WM_LBUTTONDOWN, WM_LBUTTONUP, WM_CLOSE = 0x0200, 0x0201, 0x0202, 0x0010
WM_APP = 0x8000

def find_hwnd(pid):
    out = []
    @ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)
    def cb(h, l):
        p = wt.DWORD(); u.GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid:
            b = ctypes.create_unicode_buffer(128); u.GetClassNameW(h, b, 128)
            if b.value == "PixelFishTankWindow": out.append(h)
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
    if hwnd or p.poll() is not None: break
assert hwnd, "no window"

wx, wy, w0, h0 = window_rect(hwnd)
d = read_debug(hwnd); scale = float(d["scale"])
print("window: (%d,%d,%d,%d) scale=%.4f" % (wx, wy, w0, h0, scale))
ok("启动尺寸是 4:3", abs(w0 * 3 - h0 * 4) <= 4, "%dx%d" % (w0, h0))

# 1) 右下角拖拽 → 尺寸必须纹丝不动
corners = [(w0 - 6, h0 - 6), (w0 - 3, h0 // 2), (w0 // 2, h0 - 3)]
moved = []
for cx, cy in corners:
    post(hwnd, WM_MOUSEMOVE, cx, cy)
    post(hwnd, WM_LBUTTONDOWN, cx, cy, 0x0001)
    for step in range(1, 13):
        post(hwnd, WM_MOUSEMOVE, cx + step * 12, cy + step * 9)
    post(hwnd, WM_LBUTTONUP, cx + 144, cy + 108)
x, y, w1, h1 = window_rect(hwnd)
ok("拖右下角/右边缘/底边缘都不改变尺寸", (w1, h1) == (w0, h0), "%dx%d -> %dx%d" % (w0, h0, w1, h1))

# 2) 菜单仍是 8 项、能开能收
d = read_debug(hwnd); scale = float(d["scale"]); mb = rect_of(d, "menuBtn")
cx = (mb[0] + mb[2]) / 2 * scale; cy = (mb[1] + mb[3]) / 2 * scale
click(hwnd, cx, cy)
d = read_debug(hwnd); items = d.get("menu", "")
print("menu:", items)
ok("☰ 能打开菜单", d.get("menuOpen") == "1")
ok("菜单 8 项且无尺寸项", int(d.get("menuCount", "0") or 0) == 8 and "尺寸" not in items, items)
click(hwnd, cx, cy)

# 3) 锁定穿透：整窗穿透，☰ 压在光标下时不穿透
click(hwnd, cx, cy)                       # 打开
d = read_debug(hwnd); mr = rect_of(d, "menuRect"); n = int(d.get("menuCount", "8") or 8)
ih = (mr[3] - mr[1]) / n
lockIdx = 4
for i, t in enumerate(d.get("menu", "").split(",")):
    if "锁定鼠标穿透" in t: lockIdx = i
click(hwnd, (mr[0] + mr[2]) / 2 * scale, (mr[1] + (lockIdx + 0.5) * ih) * scale)
time.sleep(0.3)
d = read_debug(hwnd)
ok("菜单切到锁定穿透", d.get("locked") == "1", "locked=" + d.get("locked", "?"))
time.sleep(0.25)
ok("锁定时整窗穿透", (u.GetWindowLongW(hwnd, -20) & 0x20) != 0)

cp = wt.POINT(); u.GetCursorPos(ctypes.byref(cp))
d = read_debug(hwnd); scale = float(d["scale"]); mb = rect_of(d, "menuBtn")
mx = (mb[0] + mb[2]) / 2 * scale; my = (mb[1] + mb[3]) / 2 * scale
u.SetWindowPos(hwnd, 0, int(cp.x - mx), int(cp.y - my), 0, 0, 1 | 4 | 0x10)
time.sleep(0.40)
ok("光标在 ☰ 上时不穿透", (u.GetWindowLongW(hwnd, -20) & 0x20) == 0)
click(hwnd, mx, my)
d = read_debug(hwnd)
ok("锁定时 ☰ 仍能开菜单", d.get("menuOpen") == "1")
mr = rect_of(d, "menuRect"); ih = (mr[3] - mr[1]) / n
click(hwnd, (mr[0] + mr[2]) / 2 * scale, (mr[1] + (lockIdx + 0.5) * ih) * scale)
time.sleep(0.25)
ok("能解开锁定", read_debug(hwnd).get("locked") == "0")

# 4) 鱼缸空白处拖动仍可用（位置变化即通过；此处只验证尺寸不变）
x, y, w2, h2 = window_rect(hwnd)
post(hwnd, WM_MOUSEMOVE, w2 // 2, h2 // 2)
post(hwnd, WM_LBUTTONDOWN, w2 // 2, h2 // 2, 0x0001)
post(hwnd, WM_MOUSEMOVE, w2 // 2 + 30, h2 // 2 + 20)
post(hwnd, WM_LBUTTONUP, w2 // 2 + 30, h2 // 2 + 20)
x3, y3, w3, h3 = window_rect(hwnd)
ok("拖水体时尺寸仍不变", (w3, h3) == (w2, h2), "%dx%d" % (w3, h3))

u.SetWindowPos(hwnd, 0, 0, 0, 0, 0, 1 | 4)
u.PostMessageW(hwnd, WM_CLOSE, 0, 0); time.sleep(0.7)
ok("关窗仍是收进托盘", p.poll() is None, "alive=%s" % (p.poll() is None))
if p.poll() is None: p.kill()

bad = [r for r in results if not r[1]]
print("RESULT: " + ("ALL PASS" if not bad else "FAIL %d/%d" % (len(bad), len(results))))
sys.exit(0 if not bad else 1)
