#include "Util.h"

namespace ft {

std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int len = ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), &out[0], len);
    return out;
}

std::string WideToUtf8(const std::wstring& s) {
    if (s.empty()) return std::string();
    int len = ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                                    nullptr, 0, nullptr, nullptr);
    if (len <= 0) return std::string();
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()),
                          &out[0], len, nullptr, nullptr);
    return out;
}

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH * 4] = { 0 };
    DWORD n = ::GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0) return std::wstring(L".\\");
    std::wstring full(buf, n);
    size_t pos = full.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring(L".\\");
    return full.substr(0, pos + 1);
}

std::wstring PathJoin(const std::wstring& a, const std::wstring& b) {
    if (a.empty()) return b;
    wchar_t last = a.back();
    if (last == L'\\' || last == L'/') return a + b;
    return a + L"\\" + b;
}

std::wstring FileNameOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

std::wstring DirectoryOf(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return std::wstring(L".");
    return path.substr(0, pos);
}

bool FileExists(const std::wstring& path) {
    DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

bool DirectoryExists(const std::wstring& path) {
    DWORD attr = ::GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureDirectory(const std::wstring& path) {
    if (DirectoryExists(path)) return true;
    return ::CreateDirectoryW(path.c_str(), nullptr) != FALSE;
}

bool ReadWholeFileBytes(const std::wstring& path, std::string& out) {
    out.clear();
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;

    LARGE_INTEGER size{};
    if (!::GetFileSizeEx(h, &size) || size.QuadPart <= 0) {
        ::CloseHandle(h);
        return false;
    }
    out.resize(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    BOOL ok = ::ReadFile(h, &out[0], static_cast<DWORD>(out.size()), &read, nullptr);
    ::CloseHandle(h);
    if (!ok) { out.clear(); return false; }
    out.resize(read);
    // 去掉 UTF-8 BOM
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

bool WriteWholeFileBytes(const std::wstring& path, const std::string& data) {
    HANDLE h = ::CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    BOOL ok = TRUE;
    if (!data.empty()) {
        ok = ::WriteFile(h, data.data(), static_cast<DWORD>(data.size()), &written, nullptr);
    }
    ::CloseHandle(h);
    return ok != FALSE && written == data.size();
}

bool ListSubDirectories(const std::wstring& dir, std::vector<std::wstring>& out) {
    out.clear();
    WIN32_FIND_DATAW fd{};
    std::wstring pattern = PathJoin(dir, L"*");
    HANDLE h = ::FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return false;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0) continue;
            out.push_back(PathJoin(dir, fd.cFileName));
        }
    } while (::FindNextFileW(h, &fd));

    ::FindClose(h);
    return true;
}

double WallClockSeconds() {
    FILETIME ft{};
    ::GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u{};
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    // 100ns -> s
    return static_cast<double>(u.QuadPart) / 10000000.0;
}

Rng::Rng() {
    std::random_device rd;
    std::seed_seq seq{ rd(), rd(), rd(), static_cast<unsigned>(::GetTickCount()) };
    gen_.seed(seq);
}

float Rng::Next01() {
    std::uniform_real_distribution<float> d(0.0f, 1.0f);
    return d(gen_);
}

float Rng::Range(float lo, float hi) {
    return lo + (hi - lo) * Next01();
}

} // namespace ft
