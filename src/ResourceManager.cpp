#include "ResourceManager.h"
#include "Util.h"

namespace ft {

void ResourceManager::ScanCategory(const std::wstring& categoryDir, const std::wstring& category,
                                   std::vector<SpritePack>& out) {
    if (!DirectoryExists(categoryDir)) {
        errors_.push_back(L"缺少资源目录：" + categoryDir);
        return;
    }

    std::vector<std::wstring> dirs;
    ListSubDirectories(categoryDir, dirs);
    // 稳定排序，保证每次运行加载顺序一致（第 0 个就是默认物种）
    std::sort(dirs.begin(), dirs.end());

    for (const auto& dir : dirs) {
        if (!FileExists(PathJoin(dir, L"sprite.json"))) continue;

        SpritePack pack;
        std::wstring err;
        if (!LoadSpritePack(dir, category, pack, err)) {
            errors_.push_back(err);
            continue;
        }
        out.push_back(std::move(pack));
    }
}

bool ResourceManager::Scan(const std::wstring& assetsDir) {
    assetsDir_ = assetsDir;
    if (!assetsDir_.empty() && assetsDir_.back() != L'\\' && assetsDir_.back() != L'/') {
        assetsDir_.push_back(L'\\');
    }

    fishPacks_.clear();
    plantPacks_.clear();
    uiPacks_.clear();
    errors_.clear();

    ScanCategory(PathJoin(assetsDir_, L"fish"), L"fish", fishPacks_);
    ScanCategory(PathJoin(assetsDir_, L"plant"), L"plant", plantPacks_);
    ScanCategory(PathJoin(assetsDir_, L"ui"), L"ui", uiPacks_);

    if (fishPacks_.empty())  errors_.push_back(L"assets\\fish 下没有任何有效精灵包");
    if (plantPacks_.empty()) errors_.push_back(L"assets\\plant 下没有任何有效精灵包（水草将不显示）");

    return !fishPacks_.empty();
}

const SpritePack* ResourceManager::FindFish(const std::wstring& name) const {
    for (const auto& p : fishPacks_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const SpritePack* ResourceManager::FindPlant(const std::wstring& name) const {
    for (const auto& p : plantPacks_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const SpritePack* ResourceManager::FindUi(const std::wstring& name) const {
    for (const auto& p : uiPacks_) {
        if (p.name == name) return &p;
    }
    return nullptr;
}

const SpritePack* ResourceManager::FishAt(size_t index) const {
    if (fishPacks_.empty()) return nullptr;
    return &fishPacks_[index % fishPacks_.size()];
}

} // namespace ft
