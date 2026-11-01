// =============================================================================
//  Weather.h  --  WinHTTP 异步天气（工作线程 + 离线模拟曲线兜底）
//
//  说明：WinHTTP 的调用本身是阻塞 API，这里放到独立工作线程执行，
//        对逻辑线程/渲染线程完全异步，不会卡帧；联网失败自动切到
//        离线模拟温度曲线（sin 日周期）。
// =============================================================================
#pragma once

#include "Common.h"
#include <thread>
#include <condition_variable>

namespace ft {

struct WeatherResult {
    bool         online = false;
    float        tempC  = 24.0f;
    std::wstring note;            // 城市 / 错误原因
    double       stamp  = 0.0;    // 拉取时刻（WallClockSeconds）
};

class WeatherClient {
public:
    ~WeatherClient();

    void Start(std::wstring host, std::wstring path);
    void Stop();

    // 有新结果时返回 true（取走结果）
    bool Take(WeatherResult& out);

    // 离线模拟：以小时为周期的正弦温度曲线，不需要网络
    static float OfflineCurve(double unixSeconds);

private:
    void Loop();
    bool Fetch(const std::wstring& host, const std::wstring& path,
               float& tempOut, std::wstring& noteOut);

    std::thread              thread_;
    std::atomic<bool>        running_{ false };
    std::atomic<bool>        hasResult_{ false };
    std::mutex               resultMu_;
    WeatherResult            result_;

    std::mutex               cfgMu_;
    std::wstring             host_;
    std::wstring             path_;

    std::mutex               cvMu_;
    std::condition_variable  cv_;
};

} // namespace ft
