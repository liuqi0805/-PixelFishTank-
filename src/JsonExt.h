// =============================================================================
//  JsonExt.h  --  对 cJSON 的薄封装（读工具 + 写工具）
//  说明：直接用官方 cJSON 的 C API，这里只做类型安全与默认值处理，
//        不引入任何额外依赖。
// =============================================================================
#pragma once

#include "Common.h"
#include "Util.h"
#include "third_party/cJSON.h"

namespace ft {
namespace json {

inline const cJSON* Child(const cJSON* obj, const char* key) {
    if (!obj) return nullptr;
    return cJSON_GetObjectItemCaseSensitive(obj, key);
}

inline const cJSON* At(const cJSON* arr, int index) {
    if (!arr) return nullptr;
    return cJSON_GetArrayItem(arr, index);
}

inline int Count(const cJSON* arr) {
    return arr ? cJSON_GetArraySize(arr) : 0;
}

inline double GetNum(const cJSON* obj, const char* key, double def) {
    const cJSON* it = Child(obj, key);
    if (!it || !cJSON_IsNumber(it)) return def;
    return it->valuedouble;
}

inline float GetFloat(const cJSON* obj, const char* key, float def) {
    return static_cast<float>(GetNum(obj, key, static_cast<double>(def)));
}

inline int GetInt(const cJSON* obj, const char* key, int def) {
    return static_cast<int>(GetNum(obj, key, static_cast<double>(def)));
}

inline bool GetBool(const cJSON* obj, const char* key, bool def) {
    const cJSON* it = Child(obj, key);
    if (!it) return def;
    if (cJSON_IsBool(it)) return cJSON_IsTrue(it) != 0;
    if (cJSON_IsNumber(it)) return it->valuedouble != 0.0;
    return def;
}

inline std::wstring GetStrW(const cJSON* obj, const char* key, const std::wstring& def) {
    const cJSON* it = Child(obj, key);
    if (!it || !cJSON_IsString(it) || !it->valuestring) return def;
    return Utf8ToWide(std::string(it->valuestring));
}

inline std::string GetStr(const cJSON* obj, const char* key, const std::string& def) {
    const cJSON* it = Child(obj, key);
    if (!it || !cJSON_IsString(it) || !it->valuestring) return def;
    return std::string(it->valuestring);
}

inline bool GetNum2(const cJSON* obj, const char* key, float& a, float& b) {
    const cJSON* it = Child(obj, key);
    if (!it || !cJSON_IsArray(it) || cJSON_GetArraySize(it) < 2) return false;
    const cJSON* x = cJSON_GetArrayItem(it, 0);
    const cJSON* y = cJSON_GetArrayItem(it, 1);
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y)) return false;
    a = static_cast<float>(x->valuedouble);
    b = static_cast<float>(y->valuedouble);
    return true;
}

// object 形式的 {x,y,w,h} -> Rect（单位：源像素）
inline bool GetRect(const cJSON* obj, const char* key, Rect& out) {
    const cJSON* it = Child(obj, key);
    if (!it || !cJSON_IsObject(it)) return false;
    const cJSON* x = Child(it, "x");
    const cJSON* y = Child(it, "y");
    const cJSON* w = Child(it, "w");
    const cJSON* h = Child(it, "h");
    if (!cJSON_IsNumber(x) || !cJSON_IsNumber(y) || !cJSON_IsNumber(w) || !cJSON_IsNumber(h)) {
        return false;
    }
    out = Rect::FromXYWH(static_cast<float>(x->valuedouble),
                         static_cast<float>(y->valuedouble),
                         static_cast<float>(w->valuedouble),
                         static_cast<float>(h->valuedouble));
    return true;
}

// ---- 构建辅助（写 state.json / 生成资源时使用）------------------------------
struct Doc {
    cJSON* root = nullptr;
    Doc() : root(cJSON_CreateObject()) {}
    ~Doc() { if (root) cJSON_Delete(root); }
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;

    cJSON* Obj(const char* key) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddItemToObject(root, key, o);
        return o;
    }
    cJSON* Arr(const char* key) {
        cJSON* a = cJSON_CreateArray();
        cJSON_AddItemToObject(root, key, a);
        return a;
    }
    std::string ToString(bool formatted) {
        char* s = formatted ? cJSON_Print(root) : cJSON_PrintUnformatted(root);
        if (!s) return std::string();
        std::string out(s);
        cJSON_free(s);
        return out;
    }
};

inline void PutNum(cJSON* obj, const char* key, double v) { cJSON_AddNumberToObject(obj, key, v); }
inline void PutInt(cJSON* obj, const char* key, int v) { cJSON_AddNumberToObject(obj, key, static_cast<double>(v)); }
inline void PutBool(cJSON* obj, const char* key, bool v) { cJSON_AddBoolToObject(obj, key, v); }
inline void PutStrW(cJSON* obj, const char* key, const std::wstring& v) {
    cJSON_AddStringToObject(obj, key, WideToUtf8(v).c_str());
}
inline void PutStr(cJSON* obj, const char* key, const std::string& v) {
    cJSON_AddStringToObject(obj, key, v.c_str());
}

} // namespace json
} // namespace ft
