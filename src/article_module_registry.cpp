// =============================================================================
// article_module_registry.cpp - 一文模块加载实现
// =============================================================================
#include "article_module_registry.h"
#include "utils.h"

#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace {

std::vector<std::wstring> ListDllFiles(const std::wstring& dir) {
    std::vector<std::wstring> files;
    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') {
        base += L'\\';
    }
    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW((base + L"*.dll").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(base + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    std::sort(files.begin(), files.end());
    return files;
}

template <typename Fn>
Fn GetExport(HMODULE lib, const char* name) {
    return reinterpret_cast<Fn>(GetProcAddress(lib, name));
}

} // namespace

// ---------------------------------------------------------------------------
ArticleModuleRegistry& ArticleModuleRegistry::Instance() {
    static ArticleModuleRegistry registry;
    return registry;
}

void ArticleModuleRegistry::LoadAll() {
    if (loaded_) return;
    loaded_ = true;

    std::wstring exeDir = utils::GetExeDir();
    utils::EnsureDirectory(exeDir + L"lib"); // CreateDirectory 不递归
    std::wstring dir = exeDir + L"lib\\article";
    utils::EnsureDirectory(dir);

    for (const std::wstring& path : ListDllFiles(dir)) {
        HMODULE lib = LoadLibraryExW(path.c_str(), nullptr,
                                     LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!lib) continue;

        ArticleModuleEntry entry;
        entry.path = path;
        entry.library = lib;
        entry.getInfo = GetExport<ArticleModuleGetInfoFn>(lib, "ArticleModule_GetInfo");
        entry.getSchema = GetExport<ArticleModuleGetSchemaFn>(lib, "ArticleModule_GetConfigSchema");
        entry.create = GetExport<ArticleModuleCreateFn>(lib, "ArticleModule_Create");
        entry.destroy = GetExport<ArticleModuleDestroyFn>(lib, "ArticleModule_Destroy");
        entry.applyConfig = GetExport<ArticleModuleApplyConfigFn>(lib, "ArticleModule_ApplyConfig");
        entry.select = GetExport<ArticleModuleSelectFn>(lib, "ArticleModule_Select");
        entry.getRefreshSeconds = GetExport<ArticleModuleGetRefreshSecondsFn>(
            lib, "ArticleModule_GetRefreshSeconds");

        const ArticleModuleInfo* info =
            entry.getInfo ? entry.getInfo() : nullptr;
        bool ok = info && info->abi_version == ARTICLE_MODULE_ABI_VERSION &&
                  info->id && info->id[0] && entry.create && entry.destroy &&
                  entry.applyConfig && entry.select;
        if (!ok) {
            FreeLibrary(lib);
            continue;
        }

        entry.id = info->id;
        entry.name = info->name ? info->name : info->id;
        entry.description = info->description ? info->description : L"";
        const char* schema = entry.getSchema ? entry.getSchema() : nullptr;
        entry.schemaJson = schema ? schema : "{\"items\":[]}";

        // 同 id 只保留第一个，避免配置指向不确定的实现
        bool duplicate = false;
        for (const auto& existing : entries_) {
            if (existing.id == entry.id) { duplicate = true; break; }
        }
        if (duplicate) {
            FreeLibrary(lib);
            continue;
        }

        entries_.push_back(std::move(entry));
    }
}

const ArticleModuleEntry* ArticleModuleRegistry::Find(
    const std::wstring& id) const {
    for (const auto& entry : entries_) {
        if (entry.id == id) return &entry;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
bool ArticleModuleInstance::Create(const ArticleModuleEntry& entry,
                                   const ArticleModuleHostApi* host) {
    Destroy();
    if (!entry.Valid() || !host) return false;
    try {
        handle_ = entry.create(host);
    } catch (...) {
        handle_ = nullptr;
    }
    if (!handle_) return false;
    entry_ = &entry;
    return true;
}

void ArticleModuleInstance::Destroy() {
    if (entry_ && handle_ && entry_->destroy) {
        try {
            entry_->destroy(handle_);
        } catch (...) {
            // 模块异常不应影响宿主退出
        }
    }
    handle_ = nullptr;
    entry_ = nullptr;
}

void ArticleModuleInstance::ApplyConfig(const std::string& json) {
    if (!Valid()) return;
    try {
        entry_->applyConfig(handle_, json.c_str());
    } catch (...) {
        // 模块异常不应影响宿主
    }
}

int ArticleModuleInstance::Select(int currentIndex, int reason) {
    if (!Valid()) return -1;
    try {
        return entry_->select(handle_, currentIndex, reason);
    } catch (...) {
        return -1;
    }
}

int ArticleModuleInstance::RefreshSeconds() const {
    if (!Valid() || !entry_->getRefreshSeconds) return 0;
    try {
        return std::max(0, entry_->getRefreshSeconds(handle_));
    } catch (...) {
        return 0;
    }
}
