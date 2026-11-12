// =============================================================================
// normal_module.cpp - 默认一文显示方式
//
// 行为：随机选择一篇未显示过的文章；全部看过后清空记录重新开始；
// 定时刷新时避免与当前文章连续重复。
// 刷新间隔由本模块自己声明（refreshSec，0 = 不自动刷新）。
// =============================================================================
#include "../article_module_api.h"
#include "../json.h"

#include <windows.h>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

const int kDefaultRefreshSec = 300;

struct NormalModule {
    const ArticleModuleHostApi* host = nullptr;
    int refreshSec = kDefaultRefreshSec;
};

const wchar_t kId[]          = L"normal";
const wchar_t kName[]        = L"普通";
const wchar_t kDescription[] = L"\u968f\u673a\u5207\u6362\u4e00\u6587\uff0c\u5c3d\u91cf\u4e0d\u91cd\u590d";
const char    kSchema[] =
    "{\"items\":["
    "{\"key\":\"refreshSec\",\"type\":\"int\","
    "\"label\":\"\u5237\u65b0\u65f6\u95f4\uff08\u79d2\uff09\","
    "\"hint\":\"\u9694\u591a\u5c11\u79d2\u81ea\u52a8\u6362\u4e00\u7bc7\uff1b0 = \u4e0d\u81ea\u52a8\u5237\u65b0\","
    "\"min\":0,\"max\":86400,\"step\":30,\"default\":300}"
    "]}";

int Pick(NormalModule* m, int currentIndex) {
    const ArticleModuleHostApi* h = m->host;
    int count = h->article_count(h->host);
    if (count <= 0) return -1;

    std::vector<int> candidates;
    candidates.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        if (i == currentIndex && count > 1) continue;
        const char* key = h->article_key(h->host, i);
        if (key && key[0] && h->is_seen(h->host, key)) continue;
        candidates.push_back(i);
    }

    if (candidates.empty()) {
        // 一轮全部看过：清空记录后重新开始
        h->clear_seen(h->host);
        for (int i = 0; i < count; ++i) {
            if (i == currentIndex && count > 1) continue;
            candidates.push_back(i);
        }
    }
    if (candidates.empty()) candidates.push_back(0);

    unsigned int r = h->random_u32(h->host);
    return candidates[r % candidates.size()];
}

} // namespace

ARTICLE_MODULE_EXPORT const ArticleModuleInfo* ArticleModule_GetInfo(void) {
    static const ArticleModuleInfo info = {
        ARTICLE_MODULE_ABI_VERSION, kId, kName, kDescription
    };
    return &info;
}

ARTICLE_MODULE_EXPORT const char* ArticleModule_GetConfigSchema(void) {
    return kSchema;
}

ARTICLE_MODULE_EXPORT void* ArticleModule_Create(const ArticleModuleHostApi* host) {
    if (!host || host->abi_version != ARTICLE_MODULE_ABI_VERSION) return nullptr;
    if (!host->article_count || !host->article_key || !host->is_seen ||
        !host->clear_seen || !host->random_u32) {
        return nullptr;
    }
    NormalModule* m = new (std::nothrow) NormalModule();
    if (!m) return nullptr;
    m->host = host;
    return m;
}

ARTICLE_MODULE_EXPORT void ArticleModule_Destroy(void* module) {
    delete static_cast<NormalModule*>(module);
}

ARTICLE_MODULE_EXPORT void ArticleModule_ApplyConfig(void* module, const char* json) {
    NormalModule* m = static_cast<NormalModule*>(module);
    if (!m) return;
    m->refreshSec = kDefaultRefreshSec;
    if (!json || !json[0]) return;
    try {
        mini_json::Value root = mini_json::Value::parse(json);
        if (const mini_json::Value* v = root.find("refreshSec")) {
            int sec = static_cast<int>(v->as_number(kDefaultRefreshSec));
            if (sec < 0) sec = 0;
            if (sec > 86400) sec = 86400;
            m->refreshSec = sec;
        }
    } catch (...) {
        // 配置异常时退回默认值，避免影响显示
        m->refreshSec = kDefaultRefreshSec;
    }
}

// 可选导出：宿主据此决定定时刷新间隔
ARTICLE_MODULE_EXPORT int ArticleModule_GetRefreshSeconds(void* module) {
    NormalModule* m = static_cast<NormalModule*>(module);
    return m ? m->refreshSec : 0;
}

ARTICLE_MODULE_EXPORT int ArticleModule_Select(void* module, int currentIndex,
                                               int reason) {
    if (!module) return -1;
    NormalModule* m = static_cast<NormalModule*>(module);

    // 启动/保存设置时保持当前文章，避免界面突然换文
    if (reason != ARTICLE_SELECT_REFRESH && currentIndex >= 0 &&
        currentIndex < m->host->article_count(m->host->host)) {
        return currentIndex;
    }
    return Pick(m, currentIndex);
}
