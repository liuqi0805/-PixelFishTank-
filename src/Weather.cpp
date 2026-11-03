#include "Weather.h"
#include "Util.h"
#include "JsonExt.h"

#include <winhttp.h>
#include <condition_variable>
#include <thread>

namespace ft {

namespace {

constexpr int kConnectTimeoutMs = 3000;
constexpr int kReceiveTimeoutMs = 6000;

bool WinHttpGet(const std::wstring& host, const std::wstring& path,
                std::string& bodyOut, std::wstring& noteOut) {
    bodyOut.clear();

    HINTERNET session = ::WinHttpOpen(L"PixelFishTank/1.0 (Win32; D2D)",
                                      WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        noteOut = L"WinHttpOpen 失败";
        return false;
    }
    ::WinHttpSetTimeouts(session, kConnectTimeoutMs, kConnectTimeoutMs,
                         kReceiveTimeoutMs, kReceiveTimeoutMs);

    HINTERNET connect = ::WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        ::WinHttpCloseHandle(session);
        noteOut = L"连接失败：" + host;
        return false;
    }

    HINTERNET request = ::WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                             WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                             WINHTTP_FLAG_SECURE);
    if (!request) {
        ::WinHttpCloseHandle(connect);
        ::WinHttpCloseHandle(session);
        noteOut = L"创建请求失败";
        return false;
    }

    bool ok = false;
    if (::WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                             WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        ::WinHttpReceiveResponse(request, nullptr)) {

        DWORD status = 0;
        DWORD len = sizeof(status);
        ::WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);

        if (status == 200) {
            for (;;) {
                DWORD avail = 0;
                if (!::WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
                std::string chunk(avail, '\0');
                DWORD read = 0;
                if (!::WinHttpReadData(request, &chunk[0], avail, &read)) break;
                bodyOut.append(chunk.data(), read);
                if (bodyOut.size() > 512 * 1024) break;   // 防御性上限
            }
            ok = !bodyOut.empty();
            if (!ok) noteOut = L"响应为空";
        } else {
            noteOut = L"HTTP 状态码 " + std::to_wstring(status);
        }
    } else {
        noteOut = L"请求超时或网络不可用";
    }

    ::WinHttpCloseHandle(request);
    ::WinHttpCloseHandle(connect);
    ::WinHttpCloseHandle(session);
    return ok;
}

} // namespace

WeatherClient::~WeatherClient() {
    Stop();
}

void WeatherClient::Start(std::wstring host, std::wstring path) {
    Stop();
    {
        std::lock_guard<std::mutex> lk(cfgMu_);
        host_ = std::move(host);
        path_ = std::move(path);
    }
    running_.store(true);
    thread_ = std::thread(&WeatherClient::Loop, this);
}

void WeatherClient::Stop() {
    if (!running_.exchange(false)) {
        if (thread_.joinable()) thread_.join();
        return;
    }
    {
        std::lock_guard<std::mutex> lk(cvMu_);
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

bool WeatherClient::Take(WeatherResult& out) {
    if (!hasResult_.exchange(false)) return false;
    std::lock_guard<std::mutex> lk(resultMu_);
    out = result_;
    return true;
}

float WeatherClient::OfflineCurve(double unixSeconds) {
    // 以本地小时为相位：凌晨最冷、15 点最热，基线 24℃，振幅 3℃
    const double total = unixSeconds;
    const double secondsOfDay = std::fmod(total, 86400.0);
    const double hour = secondsOfDay / 3600.0;
    const double phase = (hour - 15.0) / 24.0 * 6.283185307179586;
    const float t = 24.0f + 3.0f * static_cast<float>(std::cos(phase));
    return Clampf(t, 12.0f, 34.0f);
}

bool WeatherClient::Fetch(const std::wstring& host, const std::wstring& path,
                          float& tempOut, std::wstring& noteOut) {
    std::string body;
    if (!WinHttpGet(host, path, body, noteOut)) return false;

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root) {
        noteOut = L"天气 JSON 解析失败";
        return false;
    }

    bool ok = false;
    const cJSON* cur = json::Child(root, "current_condition");
    if (cur && cJSON_IsArray(cur) && json::Count(cur) > 0) {
        const cJSON* cc = json::At(cur, 0);
        const double tempC = json::GetNum(cc, "temp_C", -999.0);
        if (tempC > -100.0 && tempC < 100.0) {
            tempOut = static_cast<float>(tempC);
            ok = true;
        }
    }

    if (ok) {
        // 尽量带上城市名，让 UI 显示更友好
        std::wstring area = L"";
        const cJSON* nearest = json::Child(root, "nearest_area");
        if (nearest && cJSON_IsArray(nearest) && json::Count(nearest) > 0) {
            const cJSON* a0 = json::At(nearest, 0);
            const cJSON* an = json::Child(a0, "areaName");
            if (an && cJSON_IsArray(an) && json::Count(an) > 0) {
                area = json::GetStrW(json::At(an, 0), "value", L"");
            }
        }
        noteOut = area.empty() ? L"在线" : area;
    }

    cJSON_Delete(root);
    return ok;
}

void WeatherClient::Loop() {
    while (running_.load()) {
        std::wstring host, path;
        {
            std::lock_guard<std::mutex> lk(cfgMu_);
            host = host_;
            path = path_;
        }

        float temp = 0.0f;
        std::wstring note;
        bool ok = false;
        if (!host.empty()) {
            ok = Fetch(host, path, temp, note);
        } else {
            note = L"未配置天气服务";
        }

        {
            std::lock_guard<std::mutex> lk(resultMu_);
            result_.online = ok;
            result_.tempC = ok ? temp : OfflineCurve(WallClockSeconds());
            result_.note = ok ? note : (L"离线模拟：" + note);
            result_.stamp = WallClockSeconds();
            hasResult_.store(true);
        }

        // 成功 20 分钟后再刷；失败 3 分钟后重试
        const auto waitFor = std::chrono::seconds(ok ? 1200 : 180);
        std::unique_lock<std::mutex> lk(cvMu_);
        cv_.wait_for(lk, waitFor, [this] { return !running_.load(); });
    }
}

} // namespace ft
