#include "Persist.h"
#include "JsonExt.h"
#include "Util.h"
#include "Simulation.h"

namespace ft {

namespace {

constexpr const wchar_t* kRunKey  = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr const wchar_t* kRunName = L"PixelFishTank";

bool CanWriteToDirectory(const std::wstring& path) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    ::CloseHandle(h);
    return true;
}

} // namespace

std::wstring StateFilePath() {
    static std::wstring cached;
    if (!cached.empty()) return cached;

    const std::wstring exeDir = ExeDirectory();
    const std::wstring candidate = PathJoin(exeDir, L"state.json");
    if (CanWriteToDirectory(candidate)) {
        cached = candidate;
        return cached;
    }

    wchar_t buf[MAX_PATH * 2] = { 0 };
    DWORD n = ::GetEnvironmentVariableW(L"LOCALAPPDATA", buf, static_cast<DWORD>(std::size(buf)));
    if (n > 0 && n < std::size(buf)) {
        const std::wstring dir = PathJoin(std::wstring(buf), L"PixelFishTank");
        EnsureDirectory(dir);
        cached = PathJoin(dir, L"state.json");
    } else {
        cached = candidate;   // 实在不行就还是 exe 同级
    }
    return cached;
}

bool SaveState(const World& w) {
    json::Doc doc;
    if (!doc.root) return false;
    cJSON* root = doc.root;

    json::PutInt(root, "version", 1);

    // ---- 窗口 ----
    RECT wr{ 0, 0, 0, 0 };
    if (w.hwnd) ::GetWindowRect(w.hwnd, &wr);
    cJSON* win = doc.Obj("window");
    json::PutInt(win, "x", wr.left);
    json::PutInt(win, "y", wr.top);
    json::PutInt(win, "w", wr.right - wr.left);
    json::PutInt(win, "h", wr.bottom - wr.top);

    // ---- 环境 ----
    cJSON* env = doc.Obj("environment");
    json::PutNum(env, "waterTemp", w.env.waterTemp);
    json::PutNum(env, "oxygen", w.env.oxygen);
    json::PutNum(env, "ambientTemp", w.ambientTemp);
    json::PutBool(env, "weatherOnline", w.weatherOnline);

    // ---- 设备 ----
    cJSON* eq = doc.Obj("equipment");
    json::PutBool(eq, "aerator", w.equip.aerator);
    json::PutNum(eq, "aeratorX", w.equip.aeratorPos.x);
    json::PutNum(eq, "aeratorY", w.equip.aeratorPos.y);
    json::PutBool(eq, "heater", w.equip.heater);
    json::PutNum(eq, "heaterX", w.equip.heaterPos.x);
    json::PutNum(eq, "heaterY", w.equip.heaterPos.y);
    json::PutNum(eq, "heaterTarget", w.equip.heaterTarget);

    // ---- 水草 ----
    cJSON* plants = doc.Arr("plants");
    for (const auto& p : w.plants) {
        cJSON* o = cJSON_CreateObject();
        json::PutStrW(o, "pack", p.packName);
        json::PutNum(o, "x", p.pos.x);
        json::PutNum(o, "y", p.pos.y);
        json::PutNum(o, "scale", p.scale);
        json::PutNum(o, "sway", p.swayPhase);
        cJSON_AddItemToArray(plants, o);
    }

    // ---- 鱼 ----
    cJSON* fishArr = doc.Arr("fish");
    for (int i = 0; i < static_cast<int>(w.fish.size()); ++i) {
        const Fish& f = w.fish[i];
        if (!f.used) continue;
        cJSON* o = cJSON_CreateObject();
        json::PutInt(o, "slot", i);
        json::PutStrW(o, "pack", f.packName);
        json::PutNum(o, "x", f.pos.x);
        json::PutNum(o, "y", f.pos.y);
        json::PutNum(o, "targetX", f.target.x);
        json::PutNum(o, "targetY", f.target.y);
        json::PutNum(o, "facing", f.facing);
        json::PutNum(o, "satiety", f.satiety);
        json::PutNum(o, "obesity", f.obesity);
        json::PutNum(o, "mood", f.mood);
        json::PutBool(o, "alive", f.alive);
        json::PutNum(o, "deadTimer", f.deadTimer);
        cJSON_AddItemToArray(fishArr, o);
    }

    // ---- 设置 ----
    cJSON* st = doc.Obj("settings");
    json::PutBool(st, "autostart", w.settings.autostart);
    json::PutBool(st, "closeToTray", w.settings.closeToTray);   // 关窗时是隐藏到托盘还是直接退出
    json::PutInt(st, "hotkeyMods", static_cast<int>(w.settings.hotkeyMods));
    json::PutInt(st, "hotkeyVk", static_cast<int>(w.settings.hotkeyVk));
    json::PutBool(st, "clickThroughLocked", w.settings.clickThroughLocked);
    json::PutStrW(st, "weatherHost", w.settings.weatherHost);
    json::PutStrW(st, "weatherPath", w.settings.weatherPath);
    json::PutNum(st, "heaterTarget", w.settings.heaterTarget);

    const std::string text = doc.ToString(true);
    if (text.empty()) return false;
    return WriteWholeFileBytes(StateFilePath(), text);
}

bool LoadState(World& w, const ResourceManager& res, SavedWindow* window) {
    std::string bytes;
    if (!ReadWholeFileBytes(StateFilePath(), bytes)) return false;

    cJSON* root = cJSON_Parse(bytes.c_str());
    if (!root) return false;

    if (window) {
        const cJSON* win = json::Child(root, "window");
        if (win) {
            const int cw = json::GetInt(win, "w", 0);
            const int ch = json::GetInt(win, "h", 0);
            if (cw > 0 && ch > 0) {
                window->x = json::GetInt(win, "x", 0);
                window->y = json::GetInt(win, "y", 0);
                window->w = cw;
                window->h = ch;
                window->valid = true;
            }
        }
    }

    const cJSON* env = json::Child(root, "environment");
    if (env) {
        w.env.waterTemp  = Clampf(json::GetFloat(env, "waterTemp", 24.0f), 0.0f, 40.0f);
        w.env.oxygen     = Clampf(json::GetFloat(env, "oxygen", 85.0f), 0.0f, 100.0f);
        w.ambientTemp    = Clampf(json::GetFloat(env, "ambientTemp", 24.0f), -30.0f, 60.0f);
        w.weatherOnline  = json::GetBool(env, "weatherOnline", false);
    }

    const cJSON* eq = json::Child(root, "equipment");
    if (eq) {
        w.equip.aerator = json::GetBool(eq, "aerator", false);
        w.equip.aeratorPos.x = json::GetFloat(eq, "aeratorX", w.water.l + 16.0f);
        w.equip.aeratorPos.y = json::GetFloat(eq, "aeratorY", w.water.b - 12.0f);
        w.equip.heater = json::GetBool(eq, "heater", false);
        w.equip.heaterPos.x = json::GetFloat(eq, "heaterX", w.water.r - 16.0f);
        w.equip.heaterPos.y = json::GetFloat(eq, "heaterY", w.water.b - 14.0f);
        w.equip.heaterTarget = Clampf(json::GetFloat(eq, "heaterTarget", 26.0f), 18.0f, 32.0f);
    }

    const cJSON* plants = json::Child(root, "plants");
    if (plants && cJSON_IsArray(plants)) {
        w.plants.clear();
        const int n = json::Count(plants);
        for (int i = 0; i < n && static_cast<int>(w.plants.size()) < kMaxPlants; ++i) {
            const cJSON* o = json::At(plants, i);
            const std::wstring packName = json::GetStrW(o, "pack", L"");
            if (packName.empty() || !res.FindPlant(packName)) continue;
            Plant p;
            p.packName = packName;
            p.pos.x = Clampf(json::GetFloat(o, "x", w.water.Center().x), w.water.l, w.water.r);
            p.pos.y = Clampf(json::GetFloat(o, "y", w.water.b), w.water.t, w.water.b);
            p.scale = Clampf(json::GetFloat(o, "scale", 1.0f), 0.5f, 2.0f);
            p.swayPhase = json::GetFloat(o, "sway", 0.0f);
            w.plants.push_back(p);
        }
    }

    const cJSON* fishArr = json::Child(root, "fish");
    if (fishArr && cJSON_IsArray(fishArr)) {
        for (auto& f : w.fish) f = Fish{};
        const int n = json::Count(fishArr);
        for (int i = 0; i < n; ++i) {
            const cJSON* o = json::At(fishArr, i);
            if (!o) continue;
            int slot = json::GetInt(o, "slot", i);
            if (slot < 0 || slot >= static_cast<int>(w.fish.size())) slot = i;
            if (slot < 0 || slot >= static_cast<int>(w.fish.size())) continue;
            if (w.fish[slot].used) continue;

            const std::wstring packName = json::GetStrW(o, "pack", L"");
            const SpritePack* pack = res.FindFish(packName);
            if (!pack) continue;

            Fish f;
            f.used = true;
            f.packName = pack->name;
            f.displayName = pack->displayName;
            f.pos.x = Clampf(json::GetFloat(o, "x", w.water.Center().x), w.water.l, w.water.r);
            f.pos.y = Clampf(json::GetFloat(o, "y", w.water.Center().y), w.water.t, w.water.b);
            f.target.x = Clampf(json::GetFloat(o, "targetX", f.pos.x), w.water.l, w.water.r);
            f.target.y = Clampf(json::GetFloat(o, "targetY", f.pos.y), w.water.t, w.water.b);
            f.facing = (json::GetFloat(o, "facing", 1.0f) < 0.0f) ? -1.0f : 1.0f;
            f.satiety = Clampf(json::GetFloat(o, "satiety", 70.0f), 0.0f, 100.0f);
            f.obesity = Clampf(json::GetFloat(o, "obesity", 0.0f), 0.0f, 1.0f);
            f.mood    = Clampf(json::GetFloat(o, "mood", 80.0f), 0.0f, 100.0f);
            f.alive   = json::GetBool(o, "alive", true);
            f.deadTimer = Clampf(json::GetFloat(o, "deadTimer", 0.0f), 0.0f, 600.0f);
            f.speed = sim::kBaseSwimSpeed;
            f.bobPhase = static_cast<float>(i) * 1.7f;
            SetFishAnimation(f, f.alive ? FishAnim::Idle : FishAnim::Dead);
            w.fish[slot] = f;
        }
    }

    const cJSON* st = json::Child(root, "settings");
    if (st) {
        w.settings.autostart = json::GetBool(st, "autostart", w.settings.autostart);
        w.settings.closeToTray = json::GetBool(st, "closeToTray", w.settings.closeToTray);
        w.settings.hotkeyMods = static_cast<UINT>(json::GetInt(st, "hotkeyMods",
                                                               static_cast<int>(w.settings.hotkeyMods)));
        w.settings.hotkeyVk = static_cast<UINT>(json::GetInt(st, "hotkeyVk",
                                                             static_cast<int>(w.settings.hotkeyVk)));
        w.settings.clickThroughLocked = json::GetBool(st, "clickThroughLocked", false);
        w.settings.weatherHost = json::GetStrW(st, "weatherHost", w.settings.weatherHost);
        w.settings.weatherPath = json::GetStrW(st, "weatherPath", w.settings.weatherPath);
        w.settings.heaterTarget = Clampf(json::GetFloat(st, "heaterTarget", 26.0f), 18.0f, 32.0f);
    }

    cJSON_Delete(root);
    return true;
}

bool IsAutostartEnabled() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS) {
        return false;
    }
    DWORD type = 0;
    DWORD size = 0;
    LONG r = ::RegQueryValueExW(key, kRunName, nullptr, &type, nullptr, &size);
    ::RegCloseKey(key);
    return r == ERROR_SUCCESS && size > 0;
}

bool ApplyAutostart(bool enable) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0, KEY_SET_VALUE,
                          nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return false;
    }

    LONG result = ERROR_SUCCESS;
    if (enable) {
        wchar_t exePath[MAX_PATH * 4] = { 0 };
        DWORD n = ::GetModuleFileNameW(nullptr, exePath, static_cast<DWORD>(std::size(exePath)));
        if (n == 0 || n >= std::size(exePath)) {
            ::RegCloseKey(key);
            return false;
        }
        std::wstring quoted = L"\"" + std::wstring(exePath, n) + L"\"";
        const DWORD bytes = static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t));
        result = ::RegSetValueExW(key, kRunName, 0, REG_SZ,
                                  reinterpret_cast<const BYTE*>(quoted.c_str()), bytes);
    } else {
        result = ::RegDeleteValueW(key, kRunName);
        if (result == ERROR_FILE_NOT_FOUND) result = ERROR_SUCCESS;
    }

    ::RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

} // namespace ft
