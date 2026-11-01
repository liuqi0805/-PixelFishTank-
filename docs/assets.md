# assets 资源目录说明

程序**只读 PNG 和 JSON**，不读 `.blend`、`.gltf`、`.psd` 之类源文件。
美术在 Blender 里做完动画、渲染成 PNG 序列、丢进下面的目录、配一个 `sprite.json`，
程序下次启动就会自己扫出来（不需要改代码）。

> ⚠️ 下面的 JSON 代码块为了写注释用了 `jsonc` 风格（`//`）。
> **真实的 `sprite.json` / `state.json` 里绝对不能有注释**，否则 cJSON 直接解析失败。

---

## 1. 目录结构

```
assets\
├─ fish\                      每条鱼一个子目录
│  ├─ goldfish\
│  │  ├─ sprite.json
│  │  ├─ idle_00.png  idle_01.png  ...
│  │  ├─ feed_00.png  ...
│  │  ├─ tease_00.png ...
│  │  ├─ fat_00.png   ...
│  │  ├─ weak_00.png  ...
│  │  └─ dead_00.png  ...
│  └─ guppy\
│     ├─ sprite.json
│     └─ *.png
├─ plant\                     每株水草一个子目录
│  ├─ kelp\
│  └─ coral\
└─ ui\                        设备图标 + 托盘图标
   ├─ aerator\
   ├─ heater\
   └─ tray\
      └─ icon.png             系统托盘图标（没有 sprite.json，不会被当动画包扫描）
```

规则：

- **子目录名 = 资源包名**（`sprite.json` 里的 `name` 建议同名，程序用目录名做主键）。
- 目录按名字排序后加载，所以 `fish\` 下第一条被扫到的鱼就是“默认第一条鱼”。
- 子目录里没有合法 `sprite.json` 会被跳过，并在 `ResourceManager::Errors()` 里留一条原因。
- PNG 必须带 alpha 通道（透明背景），否则会画成方块。
- **占位美术可以同名替换**：所有素材都是“文件名即契约”，直接把同名的 PNG 覆盖掉就生效。
  - 动画帧尺寸以 `sprite.json` 的 `frameWidth/frameHeight` 为准，换图时两者必须一致，
    比例变了就同步改 `sprite.json`（碰撞盒 `hitbox` 也是按这组尺寸算的）；
  - `assets/ui/tray/icon.png` 例外：**任意尺寸都行**，程序用最近邻缩到 32×32 再交给系统，
    随手丢一张 16×16 或 256×256 都能用。

---

## 2. `sprite.json` 字段表（鱼）

| 字段 | 类型 | 必填 | 说明 |
| --- | --- | --- | --- |
| `name` | string | 是 | 资源名（一般同目录名） |
| `displayName` | string | 否 | 中文显示名，UI 里用 |
| `frameWidth` | int | 是 | 单帧像素宽（源图尺寸，不是屏幕尺寸） |
| `frameHeight` | int | 是 | 单帧像素高 |
| `renderScale` | number | 否 | 渲染放大倍数，默认 1。像素鱼建议 2（32×24 → 64×48 设计像素） |
| `frameDuration` | number | 否 | 全局默认帧时长（秒），默认 0.15 |
| `hitbox` | object | 否 | 鼠标碰撞盒，相对**单帧图像**的像素坐标 `{x,y,w,h}`；缺省按帧的 15%/20%/70%/60% 估 |
| `animations` | object | 是 | 动画集合，见下 |

`animations` 里程序固定读这 6 个键（缺哪个就用 `idle` 顶，全缺则资源作废）：

| 键 | 用途 |
| --- | --- |
| `idle` | 平时游动 |
| `feed` | 点【喂食】 |
| `tease` | 点【挑逗】 |
| `fat` | 肥胖等级 > 0.6 时的游动 |
| `weak` | 溶氧 / 饱食度过低时的虚弱游动 |
| `dead` | 死亡（一般 `loop: false`） |

每个 animation 的字段：

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `frames` | array | 帧列表，元素可以是文件名 `"idle_00.png"`，也可以是 `{"file":"x.png","duration":0.1}` |
| `count` + `pattern` | int + string | `frames` 的替代写法：按 `%02d` 之类的格式自动拼文件名 |
| `frameDuration` | number | 本动画的帧时长，覆盖全局 |
| `loop` | bool | 是否循环；`feed`/`tease` 播一轮后自动回 `idle` |
| `hitbox` | object | 本动画单独的碰撞盒（例如 `fat` 比 `idle` 胖一圈） |

---

## 3. 完整样例（带注释）

### 3.1 `assets/fish/goldfish/sprite.json`

```jsonc
{
  "name": "goldfish",
  "displayName": "金鱼",

  // 单帧尺寸：Blender 渲染出来的 PNG 就是这个大小
  "frameWidth": 32,
  "frameHeight": 24,

  // 屏幕上放大 2 倍显示（32x24 -> 64x48 设计像素）。
  // 渲染用最近邻，放大多少倍都不会糊
  "renderScale": 2.0,

  // 鼠标碰撞盒：只框住鱼身子，别把尾巴、鱼鳍的空白框进去，
  // 否则鼠标明明在空白处也算“悬停在鱼上”，出按钮会很烦
  "hitbox": { "x": 3, "y": 5, "w": 27, "h": 14 },

  "animations": {
    "idle": {
      "loop": true,
      "frameDuration": 0.16,          // 每帧 0.16 秒，4 帧一轮 0.64 秒
      "frames": ["idle_00.png", "idle_01.png", "idle_02.png", "idle_03.png"],
      "hitbox": { "x": 3, "y": 5, "w": 27, "h": 14 }
    },
    "feed": {
      "loop": true,
      "frameDuration": 0.12,
      "frames": ["feed_00.png", "feed_01.png", "feed_02.png"],
      "hitbox": { "x": 3, "y": 5, "w": 27, "h": 14 }
    },
    "tease": {
      "loop": true,
      "frameDuration": 0.10,
      "frames": ["tease_00.png", "tease_01.png", "tease_02.png"],
      "hitbox": { "x": 3, "y": 5, "w": 27, "h": 14 }
    },
    "fat": {
      "loop": true,
      "frameDuration": 0.22,
      "frames": ["fat_00.png", "fat_01.png", "fat_02.png"],
      // 胖了以后碰撞盒也胖一圈
      "hitbox": { "x": 2, "y": 3, "w": 29, "h": 18 }
    },
    "weak": {
      "loop": true,
      "frameDuration": 0.30,
      "frames": ["weak_00.png", "weak_01.png", "weak_02.png"],
      "hitbox": { "x": 3, "y": 5, "w": 27, "h": 14 }
    },
    "dead": {
      "loop": false,                  // 死亡动画只播一次，之后停在最后一帧
      "frameDuration": 0.45,
      "frames": ["dead_00.png", "dead_01.png"]
    }
  }
}
```

### 3.2 用 `count` + `pattern` 的等价写法

```jsonc
"idle": {
  "loop": true,
  "frameDuration": 0.16,
  "count": 4,                 // idle_00.png ... idle_03.png
  "pattern": "idle_%02d.png"
}
```

### 3.3 `assets/plant/kelp/sprite.json`

```jsonc
{
  "name": "kelp",
  "displayName": "海带",
  "frameWidth": 48,
  "frameHeight": 64,
  "renderScale": 1.0,
  "animations": {
    "idle": {                       // 水草只有一个“摆动”动画，键名固定 idle
      "loop": true,
      "frameDuration": 0.35,
      "frames": ["idle_00.png", "idle_01.png", "idle_02.png", "idle_03.png"]
    }
  }
}
```

### 3.4 `assets/ui/aerator/sprite.json`（增氧器图标）

```jsonc
{
  "name": "aerator",
  "displayName": "增氧器",
  "frameWidth": 16,
  "frameHeight": 16,
  "renderScale": 1.0,
  "animations": {
    "idle": { "loop": true, "frameDuration": 0.5, "frames": ["idle_00.png"] }
  }
}
```

`assets/ui/heater/sprite.json` 同理（`name` 用 `heater`）。按键查找的固定名字：
`Ui::DrawPanel` 里用 `res.FindUi(L"aerator")` / `res.FindUi(L"heater")` 取图标，
所以**目录名必须是 `aerator` 和 `heater`**（`name` 字段随意）。

---

## 4. 从 Blender 到资源包（美术侧流程）

1. 建模/绑定后做 6 组动作，每组指定帧数：
   - `idle` 4 帧、`feed` 3 帧、`tease` 3 帧、`fat` 3 帧、`weak` 3 帧、`dead` 2 帧（这只是占位美术的规格，可以自由改）。
2. 相机用**正交**，背景透明（Film → Transparent），输出 PNG RGBA。
3. 每帧分辨率固定（例如 32×24 或 64×64），**所有动画的帧尺寸必须一致**——
   程序按 `frameWidth/frameHeight` 算精灵矩形，尺寸不一致会错位。
4. 命名：`<动画名>_<两位序号>.png`，从 `00` 开始。
5. 新建 `assets/fish/<鱼名>/`，把 PNG 拷进去，写 `sprite.json`（见 §3.1）。
6. 碰撞盒怎么定：把鱼身子（不含尾鳍/背鳍）在单帧里的像素包围盒填进 `hitbox`，
   鱼朝左时程序会自动水平镜像，不用做两套。

> 占位美术不是手画的，是 `tools/gen_assets.py` 用 Pillow 程序化生成的，
> 想重画可以直接改那个脚本里的调色板和尺寸，然后 `python tools/gen_assets.py`。

---

## 5. `state.json` 样例（带注释）

退出时自动写、启动时自动读；删掉它就等于恢复出厂设置。

```jsonc
{
  "version": 1,

  // 窗口位置和尺寸（物理像素，Per-Monitor DPI 下就是真实像素）
  "window": { "x": 120, "y": 120, "w": 453, "h": 339 },

  "environment": {
    "waterTemp": 23.72,        // 水温 ℃
    "oxygen": 84.38,           // 溶氧 0~100
    "ambientTemp": 22.33,      // 环境温度（天气或离线曲线）
    "weatherOnline": false     // 上次取天气成功与否
  },

  "equipment": {
    "aerator": true,           // 增氧器是否开启
    "aeratorX": 28,            // 增氧器位置（设计坐标 320x240 系）
    "aeratorY": 222,
    "heater": false,           // 加温棒是否开启
    "heaterX": 292,
    "heaterY": 220,
    "heaterTarget": 26         // 加温棒目标水温
  },

  "plants": [
    { "pack": "kelp",  "x": 157.2, "y": 198.7, "scale": 1.04, "sway": 5.12 },
    { "pack": "coral", "x": 246.8, "y": 213.3, "scale": 0.94, "sway": 5.23 }
  ],

  "fish": [
    {
      "slot": 0,               // 槽位 0/1
      "pack": "goldfish",      // 对应 assets/fish/goldfish
      "x": 140.1, "y": 120.0,  // 当前位置（设计坐标）
      "targetX": 200.0, "targetY": 96.0,
      "facing": 1,             // 1 朝右，-1 朝左
      "satiety": 97.9,         // 饱食度 0~100
      "obesity": 0.0,          // 肥胖等级 0~1
      "mood": 80.0,            // 心情 0~100
      "alive": true,
      "deadTimer": 0.0         // 死后计时，超过 30 秒清空槽位
    }
  ],

  "settings": {
    "autostart": false,
    "hotkeyMods": 3,           // MOD_ALT|MOD_CTRL
    "hotkeyVk": 70,            // 'F'
    "clickThroughLocked": false,
    "weatherHost": "wttr.in",  // 天气服务（只接受 https）
    "weatherPath": "/?format=j1",
    "heaterTarget": 26
  }
}
```
