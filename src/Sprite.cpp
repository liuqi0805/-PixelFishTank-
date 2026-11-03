#include "Sprite.h"
#include "Util.h"
#include "JsonExt.h"

namespace ft {

int AnimClip::FrameAt(float time) const {
    if (frames.empty()) return 0;
    if (frames.size() == 1) return 0;
    if (totalDuration <= 0.0f) return 0;

    float t = time;
    if (loop) {
        t = std::fmod(t, totalDuration);
        if (t < 0.0f) t += totalDuration;
    } else if (t >= totalDuration) {
        return static_cast<int>(frames.size()) - 1;
    }

    float acc = 0.0f;
    for (size_t i = 0; i < frames.size(); ++i) {
        acc += frames[i].duration;
        if (t < acc) return static_cast<int>(i);
    }
    return static_cast<int>(frames.size()) - 1;
}

const AnimClip* SpritePack::FindClip(const wchar_t* animName) const {
    for (const auto& c : clips) {
        if (c.name == animName) return &c;
    }
    // 退化：没有目标动画就用 idle，再没有就第一段
    for (const auto& c : clips) {
        if (c.name == L"idle") return &c;
    }
    return clips.empty() ? nullptr : &clips.front();
}

std::wstring SpritePack::FramePath(const AnimFrame& f) const {
    return PathJoin(dirPath, f.file);
}

bool LoadSpritePack(const std::wstring& dir, const std::wstring& category,
                    SpritePack& out, std::wstring& error) {
    error.clear();
    const std::wstring jsonPath = PathJoin(dir, L"sprite.json");

    std::string bytes;
    if (!ReadWholeFileBytes(jsonPath, bytes)) {
        error = L"读取失败：" + jsonPath;
        return false;
    }

    cJSON* root = cJSON_Parse(bytes.c_str());
    if (!root) {
        error = L"JSON 解析失败：" + jsonPath;
        return false;
    }

    out = SpritePack{};
    out.category = category;
    out.dirPath  = dir;
    if (!out.dirPath.empty()) {
        wchar_t last = out.dirPath.back();
        if (last != L'\\' && last != L'/') out.dirPath.push_back(L'\\');
    }
    out.name        = json::GetStrW(root, "name", FileNameOf(dir));
    out.displayName = json::GetStrW(root, "displayName", out.name);
    out.frameWidth  = json::GetInt(root, "frameWidth", 0);
    out.frameHeight = json::GetInt(root, "frameHeight", 0);
    out.renderScale = json::GetFloat(root, "renderScale", 1.0f);
    if (out.renderScale <= 0.0f) out.renderScale = 1.0f;

    const float defFrameDur = json::GetFloat(root, "frameDuration", 0.12f);
    out.hasHitbox = json::GetRect(root, "hitbox", out.hitbox);

    const cJSON* anims = json::Child(root, "animations");
    if (!anims || !cJSON_IsObject(anims)) {
        cJSON_Delete(root);
        error = L"缺少 animations 节点：" + jsonPath;
        return false;
    }

    static const wchar_t* kKnown[] = { L"idle", L"feed", L"tease", L"fat", L"weak", L"dead" };

    // 先按固定顺序解析已知动画，保证 FishAnim 枚举与数组下标一致
    for (const wchar_t* key : kKnown) {
        std::string keyU8 = WideToUtf8(key);
        const cJSON* a = json::Child(anims, keyU8.c_str());
        if (!a || !cJSON_IsObject(a)) continue;

        AnimClip clip;
        clip.name = key;
        clip.loop = json::GetBool(a, "loop", (0 == wcscmp(key, L"idle") || 0 == wcscmp(key, L"fat") || 0 == wcscmp(key, L"weak")));
        clip.hasHitbox = json::GetRect(a, "hitbox", clip.hitbox);

        const float clipDur = json::GetFloat(a, "frameDuration", defFrameDur);

        const cJSON* frames = json::Child(a, "frames");
        if (frames && cJSON_IsArray(frames)) {
            const int n = json::Count(frames);
            for (int i = 0; i < n; ++i) {
                const cJSON* it = json::At(frames, i);
                if (!it) continue;
                AnimFrame frame;
                if (cJSON_IsString(it) && it->valuestring) {
                    frame.file = Utf8ToWide(std::string(it->valuestring));
                    frame.duration = clipDur;
                } else if (cJSON_IsObject(it)) {
                    frame.file = json::GetStrW(it, "file", L"");
                    frame.duration = json::GetFloat(it, "duration", clipDur);
                }
                if (!frame.file.empty()) clip.frames.push_back(frame);
            }
        }

        // 支持 "count" + "pattern" 的简写：pattern 里 %02d 会替换为帧号
        if (clip.frames.empty()) {
            const int count = json::GetInt(a, "count", 0);
            const std::wstring pattern = json::GetStrW(a, "pattern", L"");
            if (count > 0 && !pattern.empty()) {
                wchar_t buf[512];
                for (int i = 0; i < count; ++i) {
                    ::swprintf_s(buf, pattern.c_str(), i);
                    AnimFrame frame;
                    frame.file = buf;
                    frame.duration = clipDur;
                    clip.frames.push_back(frame);
                }
            }
        }

        clip.totalDuration = 0.0f;
        for (const auto& f : clip.frames) clip.totalDuration += (f.duration > 0.0f ? f.duration : defFrameDur);
        if (!clip.frames.empty()) out.clips.push_back(std::move(clip));
    }

    if (out.clips.empty()) {
        cJSON_Delete(root);
        error = L"animations 里没有任何可用帧：" + jsonPath;
        return false;
    }
    if (out.frameWidth <= 0 || out.frameHeight <= 0) {
        cJSON_Delete(root);
        error = L"frameWidth/frameHeight 必须为正整数：" + jsonPath;
        return false;
    }
    if (!out.hasHitbox) {
        // 兜底碰撞盒：帧中间的 70%，避免"看得见点不到"
        const float w = static_cast<float>(out.frameWidth);
        const float h = static_cast<float>(out.frameHeight);
        out.hitbox = Rect::FromXYWH(w * 0.15f, h * 0.2f, w * 0.7f, h * 0.6f);
        out.hasHitbox = true;
    }

    cJSON_Delete(root);
    return true;
}

} // namespace ft
