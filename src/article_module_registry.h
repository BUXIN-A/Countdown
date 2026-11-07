// =============================================================================
// article_module_registry.h - 一文 DLL 模块发现与实例管理
//
// 从 <exe 目录>\lib\article\*.dll 加载模块；每个 DLL 通过固定导出函数
// 声明自身标识、配置 schema 与选择逻辑。加载失败或没有可用模块时，
// 一文窗口会回退到内置随机逻辑。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "article_module_api.h"

struct ArticleModuleEntry {
    std::wstring path;
    std::wstring id;
    std::wstring name;
    std::wstring description;
    std::string  schemaJson;
    HMODULE library = nullptr;

    ArticleModuleGetInfoFn       getInfo = nullptr;
    ArticleModuleGetSchemaFn     getSchema = nullptr;
    ArticleModuleCreateFn        create = nullptr;
    ArticleModuleDestroyFn       destroy = nullptr;
    ArticleModuleApplyConfigFn   applyConfig = nullptr;
    ArticleModuleSelectFn        select = nullptr;

    bool Valid() const {
        return library && !id.empty() && create && destroy &&
               applyConfig && select;
    }
};

class ArticleModuleRegistry {
public:
    static ArticleModuleRegistry& Instance();

    // 扫描并加载（重复调用只扫描一次）
    void LoadAll();

    const std::vector<ArticleModuleEntry>& Entries() const { return entries_; }
    const ArticleModuleEntry* Find(const std::wstring& id) const;

private:
    ArticleModuleRegistry() = default;
    bool loaded_ = false;
    std::vector<ArticleModuleEntry> entries_;
};

// 一个模块实例（切换模块/退出时销毁）
class ArticleModuleInstance {
public:
    ArticleModuleInstance() = default;
    ~ArticleModuleInstance() { Destroy(); }

    ArticleModuleInstance(const ArticleModuleInstance&) = delete;
    ArticleModuleInstance& operator=(const ArticleModuleInstance&) = delete;

    bool Create(const ArticleModuleEntry& entry,
                const ArticleModuleHostApi* host);
    void Destroy();
    void ApplyConfig(const std::string& json);
    int Select(int currentIndex, int reason);
    bool Valid() const { return entry_ != nullptr && handle_ != nullptr; }
    const std::wstring& Id() const {
        static const std::wstring kEmpty;
        return entry_ ? entry_->id : kEmpty;
    }

private:
    const ArticleModuleEntry* entry_ = nullptr;
    void* handle_ = nullptr;
};
