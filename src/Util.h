// =============================================================================
//  Util.h  --  文件 / 编码 / 路径等小工具
// =============================================================================
#pragma once

#include "Common.h"

namespace ft {

std::wstring Utf8ToWide(const std::string& s);
std::string  WideToUtf8(const std::wstring& s);

// exe 所在目录（带结尾反斜杠），如 "C:\Games\PixelFishTank\"
std::wstring ExeDirectory();

std::wstring PathJoin(const std::wstring& a, const std::wstring& b);
std::wstring FileNameOf(const std::wstring& path);
std::wstring DirectoryOf(const std::wstring& path);

bool FileExists(const std::wstring& path);
bool DirectoryExists(const std::wstring& path);
bool EnsureDirectory(const std::wstring& path);

bool ReadWholeFileBytes(const std::wstring& path, std::string& out);
bool WriteWholeFileBytes(const std::wstring& path, const std::string& data);

// 列出 dir 下的所有子目录（返回完整路径）
bool ListSubDirectories(const std::wstring& dir, std::vector<std::wstring>& out);

// 当前时间戳（秒，单调时钟仅在进程内有效，这里给 wall clock，用于日志/天气节流）
double WallClockSeconds();

// 提供 0..1 的随机数
class Rng {
public:
    Rng();
    float Next01();
    float Range(float lo, float hi);
private:
    std::mt19937 gen_;
};

} // namespace ft
