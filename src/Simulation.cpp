#include "Simulation.h"
#include "Util.h"

namespace ft {

namespace {

Rng g_rng;

Vec2 RandomPointInWater(const World& w, const SpritePack* pack) {
    const Rect& water = w.water;
    float halfW = 12.0f;
    float halfH = 10.0f;
    if (pack) {
        halfW = static_cast<float>(pack->frameWidth) * pack->renderScale * 0.5f + 2.0f;
        halfH = static_cast<float>(pack->frameHeight) * pack->renderScale * 0.5f + 2.0f;
    }

    const float minX = water.l + std::max(sim::kFishMargin, halfW);
    const float maxX = water.r - std::max(sim::kFishMargin, halfW);
    const float minY = water.t + std::max(sim::kFishMargin, halfH);
    const float maxY = water.b - std::max(sim::kFishMargin, halfH);

    Vec2 p;
    p.x = (minX < maxX) ? g_rng.Range(minX, maxX) : water.Center().x;
    p.y = (minY < maxY) ? g_rng.Range(minY, maxY) : water.Center().y;
    return p;
}

// 水温 / 溶氧对鱼的影响系数
float OxygenActivityFactor(const World& w) {
    if (w.env.oxygen >= sim::kOxyThreshold) return 1.0f;
    // 略低于生存阈值：游动频率降低（最低 45%）
    const float t = Clamp01(w.env.oxygen / sim::kOxyThreshold);
    return 0.45f + 0.55f * t;
}

void KillFish(Fish& f) {
    if (!f.alive) return;
    f.alive = false;
    f.deadTimer = 0.0f;
    f.target = f.pos;
    f.idleWait = 0.0f;
    f.actionTimer = 0.0f;
    SetFishAnimation(f, FishAnim::Dead);
}

void ChooseNewTarget(World& w, Fish& f, const SpritePack* pack) {
    f.target = RandomPointInWater(w, pack);
    // 溶氧低 / 心情差时更"懒"，发呆时间更长
    const float oxy = OxygenActivityFactor(w);
    const float moody = (f.mood < sim::kMoodLow) ? 2.2f : 1.0f;
    f.idleWait = g_rng.Range(0.6f, 2.6f) * moody / oxy;
}

void UpdateFishAnimation(Fish& f, const World& w) {
    // 优先级：死亡 > 喂食/挑逗(action) > 虚弱 > 肥胖 > idle
    if (!f.alive) {
        SetFishAnimation(f, FishAnim::Dead);
        return;
    }
    if (f.actionTimer > 0.0f) {
        SetFishAnimation(f, f.actionAnim);
        return;
    }
    if (w.env.oxygen < sim::kOxyThreshold || f.mood < sim::kMoodLow) {
        SetFishAnimation(f, FishAnim::Weak);
        return;
    }
    if (f.obesity > 0.6f) {
        SetFishAnimation(f, FishAnim::Fat);
        return;
    }
    SetFishAnimation(f, FishAnim::Idle);
}

} // namespace

void SetFishAnimation(Fish& f, FishAnim anim) {
    if (f.anim != anim) {
        f.anim = anim;
        f.animTime = 0.0f;
    }
}

void ShowToast(World& w, const std::wstring& text) {
    w.ui.toast = text;
    w.ui.toastTimer = 2.4f;
}

int CountFishInTank(const World& w) {
    int n = 0;
    for (const auto& f : w.fish) if (f.used) ++n;
    return n;
}

int CountAliveFish(const World& w) {
    int n = 0;
    for (const auto& f : w.fish) if (f.used && f.alive) ++n;
    return n;
}

void InitializeWorld(World& w, const ResourceManager& res) {
    w.water = MakeWaterRect();
    w.fish.assign(kMaxFish, Fish{});
    w.plants.clear();
    w.equip = Equipment{};
    w.equip.heaterTarget = w.settings.heaterTarget;
    w.equip.aeratorPos = Vec2{ w.water.l + 22.0f, w.water.b - 12.0f };
    w.equip.heaterPos  = Vec2{ w.water.r - 22.0f, w.water.b - 14.0f };
    w.env = Env{};
    w.env.waterTemp = 24.0f;
    w.env.oxygen = 85.0f;
    w.ui = UiState{};
    w.animClock = 0.0f;
    w.logicClock = 0.0f;
    w.slotRotation = 0;
    w.quitRequested = false;

    // 默认：一条鱼 + 两株水草
    AddFish(w, res);
    AddPlant(w, res);
    AddPlant(w, res);
}

bool AddFish(World& w, const ResourceManager& res) {
    if (res.FishPacks().empty()) {
        ShowToast(w, L"assets\\fish 下没有可用资源");
        return false;
    }
    int slot = -1;
    for (int i = 0; i < static_cast<int>(w.fish.size()); ++i) {
        if (!w.fish[i].used) { slot = i; break; }
    }
    if (slot < 0) {
        ShowToast(w, L"鱼缸最多同时容纳 2 条鱼");
        return false;
    }

    const SpritePack* pack = res.FishAt(static_cast<size_t>(w.slotRotation));
    if (!pack) return false;
    ++w.slotRotation;

    Fish f;
    f.used = true;
    f.packName = pack->name;
    f.displayName = pack->displayName;
    f.pos = RandomPointInWater(w, pack);
    f.target = f.pos;
    f.bobPhase = g_rng.Range(0.0f, 6.28f);
    f.facing = (g_rng.Next01() < 0.5f) ? -1.0f : 1.0f;
    f.satiety = 70.0f;
    f.obesity = 0.0f;
    f.mood = 80.0f;
    f.alive = true;
    f.idleWait = g_rng.Range(0.4f, 1.4f);
    f.speed = sim::kBaseSwimSpeed;
    SetFishAnimation(f, FishAnim::Idle);

    w.fish[slot] = f;
    return true;
}

bool AddPlant(World& w, const ResourceManager& res) {
    if (res.PlantPacks().empty()) {
        ShowToast(w, L"assets\\plant 下没有可用资源");
        return false;
    }
    if (static_cast<int>(w.plants.size()) >= kMaxPlants) {
        ShowToast(w, L"水草已达上限");
        return false;
    }

    // 按添加顺序在可用水草包里轮换，避免整屏都是同一种草
    const size_t idx = (w.plants.size() + static_cast<size_t>(w.slotRotation)) % res.PlantPacks().size();
    const SpritePack* pack = &res.PlantPacks()[idx];

    Plant p;
    p.packName = pack->name;
    p.scale = g_rng.Range(0.85f, 1.15f);
    p.swayPhase = g_rng.Range(0.0f, 6.28f);

    const float plantW = static_cast<float>(pack->frameWidth) * pack->renderScale * p.scale;
    const float plantH = static_cast<float>(pack->frameHeight) * pack->renderScale * p.scale;
    p.pos.x = g_rng.Range(w.water.l + plantW * 0.5f + 2.0f, w.water.r - plantW * 0.5f - 2.0f);
    p.pos.y = w.water.b - plantH * 0.5f - 2.0f;

    w.plants.push_back(p);
    return true;
}

void ClearTank(World& w) {
    for (auto& f : w.fish) f = Fish{};
    w.plants.clear();
    ShowToast(w, L"鱼缸已清空");
}

void ResetEnvironment(World& w) {
    w.env.waterTemp = 24.0f;
    w.env.oxygen = 85.0f;
    w.equip.aerator = false;
    w.equip.heater = false;
    for (auto& f : w.fish) {
        if (!f.used) continue;
        f.satiety = 70.0f;
        f.mood = 80.0f;
        f.obesity = 0.0f;
        f.starveTimer = 0.0f;
        f.lowOxyTimer = 0.0f;
        if (f.alive) SetFishAnimation(f, FishAnim::Idle);
    }
    ShowToast(w, L"环境已重置");
}

void ApplyAction(World& w, const ResourceManager& res, UiAction action, int fishIndex) {
    // 需要鼠标指向的那条鱼
    int target = fishIndex;
    if (target < 0) target = w.ui.hoverFish;

    switch (action) {
    case UiAction::Feed: {
        if (target < 0 || target >= static_cast<int>(w.fish.size())) return;
        Fish& f = w.fish[target];
        if (!f.used || !f.alive) return;

        const SpritePack* pack = res.FindFish(f.packName);
        f.satiety = Clampf(f.satiety + sim::kFeedGain, 0.0f, 100.0f);
        // 频繁喂食 -> 肥胖等级上升（饱食度本来就高的时候最容易胖）
        if (f.satiety > 55.0f) f.obesity = Clampf(f.obesity + sim::kObesityPerFeed, 0.0f, 1.0f);
        f.actionAnim = FishAnim::Feed;
        float dur = 1.2f;
        if (pack) {
            if (const AnimClip* c = pack->FindClip(L"feed")) {
                if (!c->frames.empty()) dur = c->totalDuration;
            }
        }
        f.actionTimer = dur;
        SetFishAnimation(f, FishAnim::Feed);
        ShowToast(w, L"喂食 +" + std::to_wstring(static_cast<int>(sim::kFeedGain)) + L" 饱食度");
        break;
    }
    case UiAction::Tease: {
        if (target < 0 || target >= static_cast<int>(w.fish.size())) return;
        Fish& f = w.fish[target];
        if (!f.used || !f.alive) return;

        const SpritePack* pack = res.FindFish(f.packName);
        f.mood = Clampf(f.mood + sim::kTeaseGain, 0.0f, 100.0f);
        f.actionAnim = FishAnim::Tease;
        float dur = 1.0f;
        if (pack) {
            if (const AnimClip* c = pack->FindClip(L"tease")) {
                if (!c->frames.empty()) dur = c->totalDuration;
            }
        }
        f.actionTimer = dur;
        SetFishAnimation(f, FishAnim::Tease);
        // 被挑逗也会游起来，避免心情恢复后还杵在原地
        ChooseNewTarget(w, f, pack);
        break;
    }
    case UiAction::ToggleAerator: {
        w.equip.aerator = !w.equip.aerator;
        if (w.equip.aerator) {
            w.equip.aeratorPos = Vec2{ w.water.l + 16.0f, w.water.b - 12.0f };
            ShowToast(w, L"增氧器已放置");
        } else {
            ShowToast(w, L"增氧器已移除");
        }
        break;
    }
    case UiAction::ToggleHeater: {
        w.equip.heater = !w.equip.heater;
        if (w.equip.heater) {
            w.equip.heaterPos = Vec2{ w.water.r - 16.0f, w.water.b - 14.0f };
            w.equip.heaterTarget = w.settings.heaterTarget;
            ShowToast(w, L"加温棒已放置");
        } else {
            ShowToast(w, L"加温棒已移除");
        }
        break;
    }
    case UiAction::AddPlant:
        AddPlant(w, res);
        break;
    case UiAction::AddFish:
        AddFish(w, res);
        break;
    case UiAction::ClearTank:
        ClearTank(w);
        break;
    case UiAction::ResetEnv:
        ResetEnvironment(w);
        break;
    default:
        break;
    }
}

void StepSimulation(World& w, const ResourceManager& res, float dt) {
    if (dt <= 0.0f) return;
    if (dt > 0.5f) dt = 0.5f;   // 防止挂起后一次跳太多
    w.logicClock += dt;

    const int aliveCount = CountAliveFish(w);
    const bool twoFish = (aliveCount >= 2);

    // ---------------- 1. 溶氧 ----------------
    float oxygenDelta = -sim::kBaseOxyDecay;
    for (const auto& f : w.fish) {
        if (!f.used || !f.alive) continue;
        oxygenDelta -= sim::kFishOxyPerSec;
        oxygenDelta -= sim::kObesityOxyPerSec * Clamp01(f.obesity);
    }
    if (w.equip.aerator) oxygenDelta += sim::kAeratorOxyGain;
    w.env.oxygen = Clampf(w.env.oxygen + oxygenDelta * dt, 0.0f, 100.0f);

    // ---------------- 2. 水温 ----------------
    const float targetTemp = w.equip.heater ? w.equip.heaterTarget : w.ambientTemp;
    const float rate = (targetTemp > w.env.waterTemp) ? sim::kHeatRate : sim::kCoolRate;
    const float step = rate * dt;
    const float diff = targetTemp - w.env.waterTemp;
    if (std::fabs(diff) <= step) {
        w.env.waterTemp = targetTemp;
    } else {
        w.env.waterTemp += (diff > 0.0f) ? step : -step;
    }
    w.env.waterTemp = Clampf(w.env.waterTemp, 0.0f, 40.0f);

    // ---------------- 3. 每条鱼 ----------------
    const float oxygenFactor = OxygenActivityFactor(w);
    for (int i = 0; i < static_cast<int>(w.fish.size()); ++i) {
        Fish& f = w.fish[i];
        if (!f.used) continue;

        const SpritePack* pack = res.FindFish(f.packName);

        if (!f.alive) {
            f.deadTimer += dt;
            SetFishAnimation(f, FishAnim::Dead);
            if (f.deadTimer >= sim::kDeadRemoveSeconds) {
                f = Fish{};   // 清除，空位可再放鱼
            }
            continue;
        }

        f.age += dt;
        f.animTime += dt;

        // 喂食 / 挑逗 动画计时
        if (f.actionTimer > 0.0f) {
            f.actionTimer -= dt;
            if (f.actionTimer <= 0.0f) {
                f.actionTimer = 0.0f;
                f.animTime = 0.0f;
            }
        }

        // 饱食度
        f.satiety = Clampf(f.satiety - sim::kSatietyDecay * dt, 0.0f, 100.0f);
        if (f.satiety <= 0.0f) {
            f.starveTimer += dt;
            if (f.starveTimer >= sim::kStarveDeathSeconds) {
                KillFish(f);
                ShowToast(w, L"一条鱼饿死了…");
                continue;
            }
        } else {
            f.starveTimer = 0.0f;
            // 长期吃不饱，肥胖会慢慢降下来
            if (f.satiety < 45.0f) {
                f.obesity = Clampf(f.obesity - 0.012f * dt, 0.0f, 1.0f);
            }
        }

        // 心情：单条鱼饲养会持续衰减；两条鱼共存时不衰减
        if (!twoFish) {
            f.mood = Clampf(f.mood - sim::kMoodDecaySingle * dt, 0.0f, 100.0f);
        }

        // 溶氧过低：累计时间，久了会死
        if (w.env.oxygen < sim::kOxyThreshold) {
            f.lowOxyTimer += dt;
            if (f.lowOxyTimer >= sim::kLowOxyDeathSeconds) {
                KillFish(f);
                ShowToast(w, L"溶氧过低，一条鱼死了…");
                continue;
            }
        } else {
            f.lowOxyTimer = std::max(0.0f, f.lowOxyTimer - dt * 0.5f);
        }

        // 温度不适宜时心情也会掉（过冷 / 过热）
        if (w.env.waterTemp < 18.0f || w.env.waterTemp > 30.0f) {
            f.mood = Clampf(f.mood - 0.03f * dt, 0.0f, 100.0f);
        }

        // ---- 游动 ----
        float speed = sim::kBaseSwimSpeed
                    * (1.0f - 0.35f * Clamp01(f.obesity))   // 胖鱼游得慢
                    * oxygenFactor
                    * ((f.mood < sim::kMoodLow) ? 0.35f : 1.0f);
        f.speed = speed;

        if (f.actionTimer <= 0.0f) {
            if (f.idleWait > 0.0f) {
                f.idleWait -= dt;
            } else {
                const Vec2 delta = f.target - f.pos;
                const float dist = Length(delta);
                if (dist <= sim::kArriveRadius) {
                    ChooseNewTarget(w, f, pack);
                } else {
                    const float move = speed * dt;
                    if (move >= dist) {
                        f.pos = f.target;
                        ChooseNewTarget(w, f, pack);
                    } else {
                        f.pos.x += delta.x / dist * move;
                        f.pos.y += delta.y / dist * move;
                        if (delta.x > 0.2f) f.facing = 1.0f;
                        else if (delta.x < -0.2f) f.facing = -1.0f;
                    }
                }
            }
        }

        // 别游出水体
        f.pos.x = Clampf(f.pos.x, w.water.l + 6.0f, w.water.r - 6.0f);
        f.pos.y = Clampf(f.pos.y, w.water.t + 6.0f, w.water.b - 6.0f);

        UpdateFishAnimation(f, w);
    }

    // ---------------- 4. UI 计时 ----------------
    if (w.ui.toastTimer > 0.0f) {
        w.ui.toastTimer -= dt;
        if (w.ui.toastTimer <= 0.0f) {
            w.ui.toastTimer = 0.0f;
            w.ui.toast.clear();
        }
    }
}

} // namespace ft
