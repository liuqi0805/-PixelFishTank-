#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
gen_assets.py  --  生成占位像素美术（PNG 序列 + sprite.json）

说明：
  * 美术正式版应由 Blender 渲染后替换，这里只是为了工程开箱可跑；
  * 产物全部落在 assets/fish、assets/plant、assets/ui 下，程序只读 PNG + JSON；
  * 依赖 Pillow：  pip install pillow
用法：
  python tools/gen_assets.py
"""

import json
import math
import os

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "assets"))

FISH_W, FISH_H = 32, 24


# -----------------------------------------------------------------------------
#  调色板
# -----------------------------------------------------------------------------
GOLDFISH = {
    "body": (250, 162, 52, 255),
    "body_dark": (206, 112, 26, 255),
    "belly": (255, 226, 160, 255),
    "fin": (244, 132, 44, 200),
    "outline": (104, 48, 8, 255),
    "eye": (32, 24, 20, 255),
}

GUPPY = {
    "body": (86, 196, 220, 255),
    "body_dark": (44, 148, 180, 255),
    "belly": (208, 246, 252, 255),
    "fin": (92, 176, 226, 205),
    "outline": (22, 76, 104, 255),
    "eye": (24, 32, 40, 255),
}


def _ellipse(d, box, color):
    d.ellipse([int(round(v)) for v in box], fill=color)


def draw_fish(pal, *, tail=0, body_y=0, body_h=12, body_w=19, mouth=0,
              eye="open", belly_up=False):
    """画一帧鱼。坐标全部用源像素，32x24。"""
    img = Image.new("RGBA", (FISH_W, FISH_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    cx = 9
    cy = (FISH_H - body_h) // 2 + body_y
    x0, y0 = cx, cy
    x1, y1 = cx + body_w, cy + body_h

    # 尾巴（左侧三角，带摆动偏移）
    tx, ty = x0 + 1, cy + body_h // 2
    d.polygon([(tx, ty - 1), (tx - 6 + tail, ty - 6), (tx - 2 + tail, ty),
               (tx - 6 + tail, ty + 6), (tx, ty + 1)], fill=pal["fin"])

    # 背鳍
    d.polygon([(x0 + 7, y0 + 1), (x0 + 13, y0 - 4), (x0 + 17, y0 + 1)],
              fill=pal["fin"])

    # 身体
    _ellipse(d, (x0, y0, x1, y1), pal["body"])
    # 暗部（背部）
    _ellipse(d, (x0 + 1, y0, x1 - 1, y0 + body_h * 0.55), pal["body_dark"])
    # 肚皮
    _ellipse(d, (x0 + 1, y0 + body_h * 0.45, x1 - 1, y1), pal["belly"])
    # 描边（近似：上下两条 1px，只贴住身体轮廓中段）
    d.line([(x0 + 5, y0), (x1 - 4, y0)], fill=pal["outline"])
    d.line([(x0 + 5, y1), (x1 - 4, y1)], fill=pal["outline"])

    # 胸鳍
    d.polygon([(x1 - 8, y1 - 4), (x1 - 3, y1 - 1), (x1 - 9, y1 - 2)], fill=pal["fin"])

    # 眼睛
    ex, ey = x1 - 5, y0 + body_h * 0.32
    if eye == "open":
        d.point([(int(ex), int(ey))], fill=pal["eye"])
        d.point([(int(ex), int(ey) + 1)], fill=pal["eye"])
        d.point([(int(ex) + 1, int(ey))], fill=(255, 255, 255, 255))
    elif eye == "half":
        d.line([(int(ex) - 1, int(ey)), (int(ex) + 1, int(ey))], fill=pal["eye"])
    elif eye == "x":
        d.line([(int(ex) - 1, int(ey) - 1), (int(ex) + 1, int(ey) + 1)], fill=pal["eye"])
        d.line([(int(ex) - 1, int(ey) + 1), (int(ex) + 1, int(ey) - 1)], fill=pal["eye"])

    # 嘴
    if mouth:
        d.line([(x1, int(y0 + body_h * 0.55)), (x1 + 1, int(y0 + body_h * 0.55) + mouth)],
               fill=pal["outline"])

    if belly_up:
        img = img.rotate(180)
    return img


def fish_frames(pal, anim):
    """返回 [(文件名, PIL 图), ...]"""
    out = []
    if anim == "idle":
        for i, (tail, dy) in enumerate([(-1, 0), (0, -1), (1, 0), (0, 1)]):
            out.append((f"idle_{i:02d}.png", draw_fish(pal, tail=tail, body_y=dy)))
    elif anim == "feed":
        for i, (dy, mouth) in enumerate([(1, 1), (0, 2), (-1, 1)]):
            out.append((f"feed_{i:02d}.png", draw_fish(pal, tail=1, body_y=dy, mouth=mouth)))
    elif anim == "tease":
        for i, (dy, tail) in enumerate([(-2, 1), (-1, -1), (0, 1)]):
            out.append((f"tease_{i:02d}.png", draw_fish(pal, tail=tail, body_y=dy, mouth=1)))
    elif anim == "fat":
        for i, (tail, dh) in enumerate([(-1, 0), (0, 1), (1, 0)]):
            out.append((f"fat_{i:02d}.png", draw_fish(pal, tail=tail, body_h=17, body_w=21,
                                                      body_y=dh - 1)))
    elif anim == "weak":
        for i, (tail, dh) in enumerate([(0, 0), (-1, 1), (0, 0)]):
            out.append((f"weak_{i:02d}.png", draw_fish(pal, tail=tail, body_h=10, body_w=18,
                                                       body_y=dh + 1, eye="half")))
    elif anim == "dead":
        for i, dy in enumerate([0, 1]):
            out.append((f"dead_{i:02d}.png", draw_fish(pal, tail=0, body_y=dy + 1,
                                                       eye="x", belly_up=True)))
    return out


FISH_CLIPS = {
    "idle":  {"loop": True,  "frameDuration": 0.16},
    "feed":  {"loop": True,  "frameDuration": 0.12},
    "tease": {"loop": True,  "frameDuration": 0.10},
    "fat":   {"loop": True,  "frameDuration": 0.22},
    "weak":  {"loop": True,  "frameDuration": 0.30},
    "dead":  {"loop": False, "frameDuration": 0.45},
}


def write_fish_pack(pack_name, display_name, pal, hitbox):
    pack_dir = os.path.join(ASSETS, "fish", pack_name)
    os.makedirs(pack_dir, exist_ok=True)

    anims = {}
    for anim, cfg in FISH_CLIPS.items():
        frames = fish_frames(pal, anim)
        names = []
        for fname, img in frames:
            img.save(os.path.join(pack_dir, fname))
            names.append(fname)
        anims[anim] = {
            "loop": cfg["loop"],
            "frameDuration": cfg["frameDuration"],
            "frames": names,
            "hitbox": hitbox,
        }

    doc = {
        "name": pack_name,
        "displayName": display_name,
        "frameWidth": FISH_W,
        "frameHeight": FISH_H,
        "renderScale": 2.0,
        "hitbox": hitbox,
        "animations": anims,
    }
    with open(os.path.join(pack_dir, "sprite.json"), "w", encoding="utf-8") as fp:
        json.dump(doc, fp, ensure_ascii=False, indent=2)
    print("fish  :", pack_name, "->", len(anims), "animations")


# -----------------------------------------------------------------------------
#  水草
# -----------------------------------------------------------------------------
def write_plant_pack(pack_name, display_name, w, h, frames, draw_fn):
    pack_dir = os.path.join(ASSETS, "plant", pack_name)
    os.makedirs(pack_dir, exist_ok=True)

    names = []
    for i in range(frames):
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        draw_fn(ImageDraw.Draw(img), i / max(1, frames))
        fname = f"idle_{i:02d}.png"
        img.save(os.path.join(pack_dir, fname))
        names.append(fname)

    doc = {
        "name": pack_name,
        "displayName": display_name,
        "frameWidth": w,
        "frameHeight": h,
        "renderScale": 1.0,
        "animations": {
            "idle": {"loop": True, "frameDuration": 0.35, "frames": names}
        },
    }
    with open(os.path.join(pack_dir, "sprite.json"), "w", encoding="utf-8") as fp:
        json.dump(doc, fp, ensure_ascii=False, indent=2)
    print("plant :", pack_name)


def kelp_draw(d, phase):
    h = 64
    for y in range(h):
        t = y / float(h)
        sway = math.sin((t * 2.2 + phase) * math.pi * 2.0) * (1.5 + t * 3.5)
        x = 24 + sway
        width = 2 + int((1.0 - t) * 3)
        # 主茎
        d.rectangle([int(x), h - y - 1, int(x) + width, h - y], fill=(46, 148, 84, 255))
        # 叶片
        if y % 6 == 0 and y > 4:
            side = 1 if (y // 6) % 2 == 0 else -1
            lx = int(x) + (width + 1 if side > 0 else -4)
            d.rectangle([lx, h - y - 2, lx + 3, h - y - 1], fill=(86, 196, 110, 255))
    # 底部小石头
    d.rectangle([16, 60, 32, 63], fill=(120, 112, 96, 255))


def coral_draw(d, phase):
    base_y = 39
    offsets = [-6, -2, 2, 6]
    for i, ox in enumerate(offsets):
        top = base_y - (18 + int(6 * math.sin(phase * math.pi * 2 + i)))
        d.rectangle([20 + ox, top, 21 + ox, base_y], fill=(228, 108, 132, 255))
        d.rectangle([20 + ox, top, 22 + ox, top + 2], fill=(255, 158, 178, 255))
    d.rectangle([8, base_y - 2, 32, base_y], fill=(214, 92, 118, 255))
    d.rectangle([10, 38, 30, 39], fill=(126, 92, 72, 255))


# -----------------------------------------------------------------------------
#  UI 素材
# -----------------------------------------------------------------------------
def write_ui_pack(pack_name, display_name, w, h, draw_fn):
    pack_dir = os.path.join(ASSETS, "ui", pack_name)
    os.makedirs(pack_dir, exist_ok=True)

    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(img))
    fname = "idle_00.png"
    img.save(os.path.join(pack_dir, fname))

    doc = {
        "name": pack_name,
        "displayName": display_name,
        "frameWidth": w,
        "frameHeight": h,
        "renderScale": 1.0,
        "animations": {
            "idle": {"loop": True, "frameDuration": 0.5, "frames": [fname]}
        },
    }
    with open(os.path.join(pack_dir, "sprite.json"), "w", encoding="utf-8") as fp:
        json.dump(doc, fp, ensure_ascii=False, indent=2)
    print("ui    :", pack_name)


def aerator_draw(d):
    d.rectangle([2, 8, 13, 15], fill=(66, 104, 132, 255))
    d.rectangle([2, 8, 13, 9], fill=(120, 176, 208, 255))
    d.rectangle([4, 4, 7, 7], fill=(150, 200, 226, 255))
    d.rectangle([9, 3, 11, 6], fill=(150, 200, 226, 255))
    d.rectangle([2, 14, 13, 15], fill=(32, 56, 74, 255))
    for i, (x, y) in enumerate([(12, 4), (14, 2), (11, 1)]):
        d.point([(x, y)], fill=(220, 246, 255, 255))


def heater_draw(d):
    d.rectangle([6, 2, 9, 14], fill=(112, 118, 128, 255))
    d.rectangle([7, 2, 8, 14], fill=(176, 182, 194, 255))
    d.rectangle([5, 0, 10, 3], fill=(226, 84, 72, 255))
    d.rectangle([6, 12, 9, 15], fill=(64, 70, 80, 255))
    d.rectangle([4, 14, 11, 15], fill=(150, 108, 60, 255))   # 吸盘


def tray_icon():
    """托盘图标：16x16 像素小鱼（金鱼配色），导出 32x32 便于高 DPI 托盘。"""
    img = Image.new("RGBA", (16, 16), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body, belly, fin, outline = GOLDFISH["body"], GOLDFISH["belly"], GOLDFISH["fin"], GOLDFISH["outline"]
    # 尾
    d.polygon([(2, 4), (5, 8), (2, 12)], fill=fin)
    # 身体
    d.ellipse([4, 4, 14, 12], fill=body)
    # 肚子
    d.ellipse([6, 8, 14, 12], fill=belly)
    # 背鳍
    d.polygon([(7, 4), (9, 1), (11, 4)], fill=fin)
    # 眼睛
    d.rectangle([11, 6, 12, 7], fill=outline)
    # 嘴
    d.rectangle([13, 8, 14, 9], fill=outline)
    return img.resize((32, 32), Image.NEAREST)


# -----------------------------------------------------------------------------
def main():
    os.makedirs(os.path.join(ASSETS, "fish"), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, "plant"), exist_ok=True)
    os.makedirs(os.path.join(ASSETS, "ui"), exist_ok=True)

    write_fish_pack("goldfish", "金鱼", GOLDFISH, {"x": 3, "y": 5, "w": 27, "h": 14})
    write_fish_pack("guppy", "孔雀鱼", GUPPY, {"x": 3, "y": 6, "w": 26, "h": 12})

    write_plant_pack("kelp", "海带", 48, 64, 4, kelp_draw)
    write_plant_pack("coral", "珊瑚", 40, 40, 4, coral_draw)

    write_ui_pack("aerator", "增氧器", 16, 16, aerator_draw)
    write_ui_pack("heater", "加温棒", 16, 16, heater_draw)

    # 托盘图标：assets/ui/tray/icon.png（没有 sprite.json，不会被当成动画包扫描）
    # 想换成自己的图，直接把同名 png 覆盖掉即可，程序会用最近邻缩到 32x32。
    os.makedirs(os.path.join(ASSETS, "ui", "tray"), exist_ok=True)
    tray_icon().save(os.path.join(ASSETS, "ui", "tray", "icon.png"))
    print("ui    : tray/icon.png")

    print("done ->", ASSETS)


if __name__ == "__main__":
    main()
