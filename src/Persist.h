// =============================================================================
//  Persist.h  --  状态持久化（state.json）+ 开机自启注册表
//
//  退出时自动保存、启动时自动加载；写入位置在 exe 同级目录，
//  若该目录不可写（例如放在 Program Files），自动退到 %LOCALAPPDATA%。
// =============================================================================
#pragma once

#include "Common.h"
#include "Model.h"
#include "ResourceManager.h"

namespace ft {

// state.json 的实际路径（首次调用时决定并缓存）
std::wstring StateFilePath();

struct SavedWindow {
    int  x = 0, y = 0, w = 0, h = 0;
    bool valid = false;
};

bool SaveState(const World& w);
// window 可为 nullptr；返回 false 表示没有可用存档
bool LoadState(World& w, const ResourceManager& res, SavedWindow* window);

// 开机自启：HKCU\Software\Microsoft\Windows\CurrentVersion\Run
bool IsAutostartEnabled();
bool ApplyAutostart(bool enable);

} // namespace ft
