// =============================================================================
//  ResourceManager.h  --  扫描 assets 目录，装载全部精灵包
//  规则：资源全部外部化（exe 同级 assets\），程序内不内置任何美术；
//        只读 PNG + JSON，不解析 .blend / glTF。
// =============================================================================
#pragma once

#include "Common.h"
#include "Sprite.h"

namespace ft {

class ResourceManager {
public:
    // assetsDir 一般形如 "<exeDir>\assets"
    bool Scan(const std::wstring& assetsDir);

    const std::vector<SpritePack>& FishPacks()  const { return fishPacks_; }
    const std::vector<SpritePack>& PlantPacks() const { return plantPacks_; }
    const std::vector<SpritePack>& UiPacks()    const { return uiPacks_; }

    const SpritePack* FindFish(const std::wstring& name) const;
    const SpritePack* FindPlant(const std::wstring& name) const;
    const SpritePack* FindUi(const std::wstring& name) const;

    // 按枚举取"任意一条鱼"的包（用于添加鱼时轮换物种）
    const SpritePack* FishAt(size_t index) const;

    const std::wstring& AssetsDir() const { return assetsDir_; }
    const std::vector<std::wstring>& Errors() const { return errors_; }

private:
    void ScanCategory(const std::wstring& categoryDir, const std::wstring& category,
                      std::vector<SpritePack>& out);

    std::wstring assetsDir_;
    std::vector<SpritePack> fishPacks_;
    std::vector<SpritePack> plantPacks_;
    std::vector<SpritePack> uiPacks_;
    std::vector<std::wstring> errors_;
};

} // namespace ft
