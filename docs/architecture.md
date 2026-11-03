# 架构说明

一句话概括：**消息线程只改“瞬时 UI 状态”，逻辑线程 10Hz 改“生态状态”，渲染线程 60fps 只读状态画图**，
三者之间用一把可重入 SRW 锁串起来。

---

## 1. 线程模型

```
UI 线程 (主线程)
  窗口消息：鼠标 / 右键 / 热键 / DPI / 关闭
  只做：命中测试、光标形状、拖动 SetWindowPos、切换开关（窗口尺寸固定，没有缩放路径）
        │  写 World.ui（瞬时态）
        ▼
  ┌─────────────────────────── SRWLOCK（可重入） ───────────────────────────┐
  │                                                                       │
  │  World（单份状态）                                                     │
  │   ├─ env / equip / fish / plants   ← 逻辑线程写                       │
  │   ├─ ui（悬停、按钮矩形、菜单）      ← UI 线程写、逻辑线程只读          │
  │   └─ settings / animClock                                            │
  │                                                                       │
  └───────────────────────────────────────────────────────────────────────┘
        ▲                                        │
        │ 10Hz 写                                 │ 60fps 只读
        │                                        ▼
  逻辑线程 LogicLoop                    渲染线程 RenderLoop
   StepSimulation(dt=0.1)                EnsureSize → Begin
   Ui::UpdateLayout                      Ui::Render（水体→水草→设备→鱼→UI）
   WeatherClient::Take                   End → UpdateLayeredWindow
```

要点：

- **渲染线程自己 Create/Destroy Renderer**：Direct2D 的 DC 渲染目标绑定窗口 DC，
  跨线程用容易出问题，所以整个 Renderer 生命周期都待在渲染线程里；
  初始化结果通过 `std::promise<bool>` 回传给 `Init()`，失败就弹框说明原因。
- 渲染线程第一件事是 `CoInitializeEx(COINIT_MULTITHREADED)`——WIC 的 `CoCreateInstance` 需要 COM。
- **可重入锁**：`SetWindowPos` 会同步回调 `WM_SIZE`，如果消息线程持着独占锁再进 `OnSize` 拿同一把锁就是自锁。
  所以 `WorldLock` 用 `thread_local` 深度计数（同线程重入直接跳过），`WorldReadLock` 在“已持独占锁”时退化成空操作。
- 逻辑和渲染的**解耦**体现在：`Simulation` 只吃 `dt`（固定 0.1s），不知道帧率；
  `Ui::Render` 只读 `World`，不改任何数值。

---

## 2. 坐标系

- **设计坐标系固定 320×240**（`kBaseWidth/kBaseHeight`）。水体 = 设计矩形内缩 6 像素。
- 实际窗口宽度 `clientW` 决定缩放系数：`scale = clientW / 320`，高度恒为 `clientW * 240/320`；
  `clientW` 由 `App::Init` 一次性算出（显示器面积/24 的 4:3），之后不再变化。
- 所有几何计算（鱼位置、按钮矩形、水草位置、设备位置、碰撞盒）都在设计坐标系里做，
  画之前 `rt->SetTransform(Matrix3x2F::Scale(scale))` 一把梭。
- 鼠标消息进来先 `ClientToDesign(x, y, scale)` 再进命中测试。

---

## 3. 渲染管线（顺序即需求里的分层）

```
Begin()
  SetTransform(Scale(scale))
  ┌ DrawWater        半透明水体底色 + 顶部高光 + 底部沙层 + 正弦水波 + 增氧器气泡
  │ DrawPlants       水草（每株按 sway 相位做轻微水平摆动）
  │ DrawEquipment    增氧器 / 加温棒 图标（关闭时半透明）
  │ DrawFish         鱼精灵（朝左用水平镜像位图）
  │ DrawFishButtons  【喂食】【挑逗】
  │ DrawPanel        【增氧器】【加温棒】
  │ DrawMenu         右键菜单
  │ DrawResizeHandle 右下角 13x13 小方块
  └ DrawToast        底部操作提示
  SetTransform(Identity)
End()  → UpdateLayeredWindow
```

- 采样：`SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED)`、
  `DrawBitmapNearest()` 用 `D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR`，
  所以补间缩放（含系统 DPI 缩放）都是硬边像素。
- 文字抗锯齿用灰度（`GRAYSCALE`），因为 ClearType 在透明背景上会出彩边。
- 位图缓存：`Renderer::GetBitmap(path, flipped)` 按“绝对路径 + 是否镜像”缓存 `ID2D1Bitmap`；
  朝左的那份是启动后第一次需要时用 `CopyPixels` 逐行翻转生成的，不占用渲染时的变换矩阵。
- 纹理格式统一转成 `GUID_WICPixelFormat32bppPBGRA`（预乘），否则半透明边缘会发黑。

---

## 4. 交互命中优先级

`Ui::HitTest()` 从上到下：

1. **右键菜单**（打开时吃掉菜单矩形内的所有命中）
2. **右上角常驻菜单入口 ☰**（`UiState::menuButton`）
3. ~~缩放角标 / 右边缘 / 底边缘~~（已移除：窗口固定尺寸，任何位置点击都只会移动窗口）
4. **鱼身上的【喂食】【挑逗】按钮**（按 `buttonOwner` 判，鼠标离开鱼身 0.4s 内仍然可点）
5. **鱼的碰撞盒**（来自 `sprite.json` 的 `hitbox`，朝左自动镜像）
6. **水体操作面板的按钮**
7. **水体空白区域** → 拖动窗口
8. 其余 = 不着色区域 → 由分层窗口的 alpha 命中测试直接穿透到桌面

光标形状：任何 UI/水体 `IDC_HAND`、剩下 `IDC_ARROW`（固定尺寸后不再有缩放光标）。

锁定穿透时 `UpdateClickThroughByCursor()` 每 40ms 查一次光标：落在 ☰ 或已打开的菜单上就临时去掉 `WS_EX_TRANSPARENT`（可点），否则加回去（整窗穿透）。

---

## 5. 生态参数表（`src/Simulation.h`，想调平衡改这里）

| 参数 | 值 | 含义 |
| --- | --- | --- |
| `kSatietyDecay` | 0.060/s | 饱食度自然衰减（约 23 分钟从满到零） |
| `kFeedGain` | +28 | 一次喂食回多少饱食度 |
| `kObesityPerFeed` | +0.07 | 饱食度 > 55 时喂一次增加的肥胖 |
| `kStarveDeathSeconds` | 90s | 饱食度归零后多久饿死 |
| `kBaseOxyDecay` | 0.060/s | 溶氧自然衰减 |
| `kFishOxyPerSec` | 0.030/s | 每条鱼基础耗氧 |
| `kObesityOxyPerSec` | 0.020/s | 肥胖额外汇率（obesity=1 时） |
| `kAeratorOxyGain` | +0.35/s | 增氧器补氧 |
| `kOxyThreshold` | 30 | 溶氧生存阈值 |
| `kLowOxyDeathSeconds` | 180s | 长期低于阈值多久死亡 |
| `kHeatRate` / `kCoolRate` | 0.15 / 0.05 ℃/s | 加温棒升温 / 自然降温 |
| `kBaseSwimSpeed` | 22 设计像素/s | 基础游速 |
| `kMoodDecaySingle` | 0.050/s | 单鱼饲养时心情衰减（双鱼不衰减） |
| `kMoodLow` | 20 | 心情过低阈值（低于就基本不动） |
| `kTeaseGain` | +26 | 一次挑逗回多少心情 |
| `kDeadRemoveSeconds` | 30s | 死后多久清空槽位 |

溶氧的有效值 = `自然衰减 + 鱼耗氧（数量 + 肥胖） - 增氧器补氧`，clamp 到 0~100。

---

## 6. 单条鱼的状态机

```
                   ┌──────────── 死亡（饿死 / 长期缺氧）────────────┐
                   │                                                ▼
  ┌────────┐  喂食/挑逗  ┌──────────────┐                      ┌─────────┐
  │  Idle  │ ──────────► │ Feed / Tease │ ── 播完一轮 ──►      │  Dead   │
  └────────┘             └──────────────┘                      └─────────┘
      ▲   │                                                        │
      │   │ 溶氧 < 阈值 或 饱食度很低          deadTimer > 30s → 槽位置空，可再添鱼
      │   ▼
      │  Weak（游得慢）
      │   │ obesity > 0.6
      │   ▼
      └── Fat（游得更慢、更耗氧）
```

动画选择优先级：`Dead > 动作动画(Feed/Tease) > Weak > Fat > Idle`。

其它规则：

- 每次挑逗/喂食会重置 `actionTimer`，动作动画播完自动回 `Idle`。
- **心情**：单鱼时持续衰减并且鼠标挑逗才回；有两条鱼时常量（不衰减）。
  水温 < 18℃ 或 > 30℃ 也会掉心情。心情 < 20 时速度系数压到 0.35，基本原地晃。
- **游动**：随机挑一个目标点（离水体边缘留 26 像素），朝目标走，到点停一会儿再换。
  `facing` 由目标方向决定，朝左时用镜像位图。
- 鱼死后不再接受任何交互（命中测试只看 `alive`），但还会占着槽位直到计时结束。

---

## 7. 天气（WinHTTP，异步 + 兜底）

- `WeatherClient` 起一个工作线程，循环 `WinHttpOpen(WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY)`
  → `WinHttpConnect("wttr.in", 443)` → `WinHttpOpenRequest(..., WINHTTP_FLAG_SECURE)`
  → 读 `current_condition[0].temp_C`（以及 `nearest_area[0].areaName[0].value` 当地名）。
- 成功 20 分钟后刷新，失败 3 分钟后重试；连接/接收超时 3s / 6s。
- 失败就用 `OfflineCurve()`：24℃ 基线 + 3℃ 振幅的正弦日周期（15 点最热）。
- 结果只在“有新数据”时才写回 `World.ambientTemp`，不会每帧覆盖。
- 水温再以 `kAmbientSmooth` 的速率向环境温度靠拢；开了加温棒则以 `kHeatRate` 冲 `heaterTarget`。

---

## 8. 交互与后台

### 8.0 悬停鱼的参数面板
`Ui::UpdateLayout` 在算完按钮后，若 `buttonOwner >= 0` 就调 `FontStatsRect` 同款逻辑算 `statRect`：
宽 = `max(38, frameWidth * renderScale * 0.55)`，高 = `pad*2 + 8*4`，优先贴鱼身右侧、放不下改左侧，
纵向居中后 `ClampInside` 回水体。`DrawFishStats` 画 4 行（饱食 / 肥胖 / 心情 / 状态），
名字用 `DrawTextLeft`、数值用 `DrawTextIn`（落在名字右侧的子矩形里）。
**它不参与 `HitTest`**——是纯展示，鼠标点上去等于点水面（会拖动窗口）。
（本轮增补）

**鱼按钮的 0.4 秒保持窗**

`Ui::UpdateLayout` 不再用“鼠标正压在鱼身上”决定按钮显不显示（那样鼠标一挪去点按钮，按钮就没了）。
现在记两件事：`buttonOwner`（按钮属于哪条鱼）和 `buttonHold`（保持到哪个时刻）。
鼠标在鱼身上、或者在按钮行矩形里，都会把 `buttonHold` 刷新成 `now + kFishButtonHold`（0.4s）；
`HitTest` 命中鱼按钮时用 `buttonOwner` 定位鱼，而不是已经被清成 -1 的 `hoverFish`。

**点击不卡死：世界动作排队**

以前 `WM_LBUTTONDOWN` 在独占锁里直接 `ApplyAction` + `SetWindowPos`，疯狂连点会让渲染线程
和逻辑线程都抢不到锁，看上去就是“程序卡住、鱼不游了”。现在：

- 消息线程只做 `EnqueueAction(action, fishIndex)`（`std::mutex` + 队列，上限 64，病态连点丢最老的）；
- 逻辑线程每个 tick（100ms）`DrainActions()` 一次性执行，再 `StepSimulation`；
- `WorldReadLock` 修掉了“靠 `t_worldLockDepth` 反推是否需要释放”的老毛病（那会让共享锁永久泄漏，
  逻辑线程再也拿不到独占锁），现在共享/独占深度分开计数，谁取谁放；
- 看门狗：`SetTimer(1500ms)` 查 `logicTick_`，连续 3 次没动就往 `%TEMP%\pft_freezelog.txt`
  写一行现场（tick、`TryAcquireSRWLockExclusive` 探测结果、是否出现过“共享锁内升级独占锁”）。

**常驻入口 / HUD**

- 右上角 ☰（`UiState::menuButton`，11×11 设计单位）常驻，点开和右键共用 `Ui::BuildMenu`；
- 右上角 HUD：`DrawHud` 画 `24.3°C  溶氧 85%`（离线时前缀 `~`），先画 0.7px 偏移黑影再画白色 alpha 0.58 的文字。

**托盘与后台运行**

- `CreateTray()`：`assets/ui/tray/icon.png` 用 WIC 解码后**最近邻**缩到 32×32，
  转 `BITMAPV5HEADER` + `CreateDIBSection` + `CreateIconIndirect` 得到 `HICON`（缺图退回系统默认图标）；
- `WM_CLOSE`：先 `SaveState`，若 `settings.closeToTray`（默认 true）且不是真退出，就 `HideTankWindow()`；
- 隐藏时 `visible_ = false`，渲染线程 `sleep(180ms)` 空转（~5fps），逻辑线程照常 10Hz；
- 托盘消息：左键/双击切显隐，右键弹 `TrackPopupMenu`（隐藏·显示 / 开机自启 / 关闭到托盘 / 退出程序），
  菜单 owner 是一个 0×0 的普通隐藏窗口——主窗口带 `WS_EX_NOACTIVATE`，直接拿它当 owner 菜单会一闪就没；
- 资源管理器重启后靠 `TaskbarCreated` 广播把图标重新挂上。

---

## 9. 持久化与系统集成

- `Persist::SaveState()` 在 `WM_CLOSE`、`WM_ENDSESSION`、`WM_QUERYENDSESSION` 都会调；
  写 `exe同级/state.json`，写不进去（比如装在 Program Files）就退到 `%LOCALAPPDATA%\PixelFishTank\state.json`。
- `Persist::LoadState()` 会做 clamp 和合法性校验：资源包不存在的鱼/水草直接丢弃，数值全部夹到合法区间。
- 窗口矩形异常（比如上次在副屏、这次只有主屏）会被重新夹到当前显示器工作区内。
- **全局热键**：`RegisterHotKey(hwnd, kHotkeyId, hotkeyMods, hotkeyVk)`，默认 `Ctrl+Alt+F`，
  `WM_HOTKEY` 里 `ShowWindow(SW_HIDE/SW_SHOWNOACTIVATE)` 并在显示时重新 `HWND_TOPMOST`。
- **开机自启**：`ApplyAutostart(bool)` 写/删 `HKCU\Software\Microsoft\Windows\CurrentVersion\Run\PixelFishTank`，
  值是带引号的 exe 全路径；只碰 `HKCU`，不需要管理员。

---

## 10. 想加东西从哪下手

| 想做的事 | 改哪 |
| --- | --- |
| 换/加一条鱼 | 往 `assets/fish/` 放目录，不用改代码；默认第一条鱼可改 `sim::InitializeWorld` |
| 新交互按钮 | `UiAction` 加枚举 → `Ui::UpdateLayout` 布局 → `Ui::HitTest` 命中 → `App::HandleAction` 处理 |
| 新天气源 | `settings.weatherHost/weatherPath`（仅 https）+ `WeatherClient::Fetch` 里换 JSON 取值路径 |
| 调生态平衡 | `Simulation.h` 的常量 |
| 换 UI 配色 | `Ui.cpp` 顶部的 `kPanelFill/kPanelLight/kPanelDark/kTextColor` |
| 改窗口比例 | `Common.h` 的 `kBaseWidth/kBaseHeight`，别的都跟着走 |
