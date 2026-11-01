// =============================================================================
//  Simulation.h  --  生态模拟与鱼的行为（逻辑线程 10Hz 驱动）
//
//  所有"玩法"参数都在这里集中，方便调参；渲染层完全不参与逻辑。
// =============================================================================
#pragma once

#include "Common.h"
#include "Model.h"
#include "ResourceManager.h"

namespace ft {

// ---- 可调参数 ---------------------------------------------------------------
namespace sim {

constexpr float kSatietyDecay        = 0.060f;  // 饱食度衰减（每秒）
constexpr float kFeedGain            = 28.0f;   // 一次喂食增加饱食度
constexpr float kObesityPerFeed      = 0.070f;  // 一次喂食增加的肥胖等级
constexpr float kStarveDeathSeconds  = 90.0f;   // 饱食度归零后多久饿死

constexpr float kBaseOxyDecay        = 0.060f;  // 溶氧自然衰减（每秒）
constexpr float kFishOxyPerSec       = 0.030f;  // 每条鱼基础耗氧
constexpr float kObesityOxyPerSec    = 0.020f;  // 肥胖额外汇率（obesity = 1 时）
constexpr float kAeratorOxyGain      = 0.350f;  // 增氧器每秒补氧
constexpr float kOxyThreshold        = 30.0f;   // 生存阈值
constexpr float kLowOxyDeathSeconds  = 180.0f;  // 长期低于阈值致死时间

constexpr float kHeatRate            = 0.150f;  // 加温棒升温速度（℃/秒）
constexpr float kCoolRate            = 0.050f;  // 自然降温速度（℃/秒）
constexpr float kAmbientSmooth       = 0.010f;  // 环境温度向天气基准靠拢的系数

constexpr float kBaseSwimSpeed       = 22.0f;   // 设计像素/秒
constexpr float kMoodDecaySingle     = 0.050f;  // 单鱼饲养时心情衰减
constexpr float kMoodLow             = 20.0f;   // 心情过低阈值
constexpr float kTeaseGain           = 26.0f;   // 一次挑逗增加心情
constexpr float kDeadRemoveSeconds   = 30.0f;   // 死亡后多久清除（空位可再放鱼）
constexpr float kFishMargin          = 26.0f;   // 游动目标距离水体边缘的留白
constexpr float kArriveRadius        = 2.5f;

} // namespace sim

// 初始化默认鱼缸（只做一次；之后状态由 state.json 覆盖）
void InitializeWorld(World& w, const ResourceManager& res);

// 推进一次生态模拟；dt 通常为 0.1s
void StepSimulation(World& w, const ResourceManager& res, float dt);

// ---- 玩家操作 ---------------------------------------------------------------
bool AddFish(World& w, const ResourceManager& res);
bool AddPlant(World& w, const ResourceManager& res);
void ClearTank(World& w);
void ResetEnvironment(World& w);
void ApplyAction(World& w, const ResourceManager& res, UiAction action, int fishIndex = -1);

// ---- 小工具 -----------------------------------------------------------------
int  CountFishInTank(const World& w);     // 占用的槽位（含死亡待清除）
int  CountAliveFish(const World& w);
void SetFishAnimation(Fish& f, FishAnim anim);
void ShowToast(World& w, const std::wstring& text);

} // namespace ft
