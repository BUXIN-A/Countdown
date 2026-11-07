// =============================================================================
// week_module.cpp - 按星期时间段显示一文
//
// 可配置项（由本 DLL 的 schema 声明）：
//   ranges: 多个不重叠的星期范围，1=周一 ... 7=周日。
// 同一范围覆盖的几天显示同一篇文章；每周一重新抽取分配。
// 当天不属于任何范围时返回 -1，宿主隐藏一文窗口。
// 分配结果保存在 temp\week_state.json（每周一自动失效）。
// =============================================================================
#include "../article_module_api.h"
#include "../json.h"

#include <windows.h>
#include <algorithm>
#include <ctime>
#include <map>
#include <new>
#include <set>
#include <string>
#include <vector>

namespace {

struct DayRange {
    int start = 1;
    int end = 1;
};

struct WeekModule {
    const ArticleModuleHostApi* host = nullptr;
    std::vector<DayRange> ranges;
    std::wstring statePath;
};

const wchar_t kId[]          = L"week";
const wchar_t kName[]        = L"\u661f\u671f\u65f6\u6bb5";
const wchar_t kDescription[] = L"\u540c\u4e00\u661f\u671f\u8303\u56f4\u5185\u56fa\u5b9a\u663e\u793a\u540c\u4e00\u7bc7\u4e00\u6587";
const char    kSchema[] =
    "{\"items\":[{"
    "\"key\":\"ranges\","
    "\"type\":\"dayRangeList\","
    "\"label\":\"时间段\","
    "\"hint\":\"1=\u5468\u4e00 ... 7=\u5468\u65e5\uff0c"
    "\u540c\u4e00\u8303\u56f4\u5185\u663e\u793a\u540c\u4e00\u7bc7\uff0c"
    "\u8303\u56f4\u4e0d\u80fd\u91cd\u53e0\","
    "\"max\":7,"
    "\"default\":[{\"start\":1,\"end\":7}]"
    "}]}";

// ---------------------------------------------------------------------------
// 文件读写（UTF-8，状态文件很小，1MB 上限足够）
// ---------------------------------------------------------------------------
bool ReadUtf8File(const std::wstring& path, std::string& out) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD size = GetFileSize(h, nullptr);
    if (size == INVALID_FILE_SIZE || size > 1024 * 1024) {
        CloseHandle(h);
        return false;
    }
    out.resize(size);
    DWORD read = 0;
    bool ok = size == 0 ||
              (ReadFile(h, out.data(), size, &read, nullptr) && read == size);
    CloseHandle(h);
    if (!ok) out.clear();
    return ok;
}

bool WriteUtf8File(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = true;
    if (!content.empty()) {
        DWORD written = 0;
        ok = WriteFile(h, content.data(), static_cast<DWORD>(content.size()),
                       &written, nullptr) && written == content.size();
    }
    CloseHandle(h);
    if (!ok) {
        DeleteFileW(tmp.c_str());
        return false;
    }
    return MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---------------------------------------------------------------------------
// 本周周一日期（YYYY-MM-DD），用 12:00 做基准抵消夏令时偏移
// ---------------------------------------------------------------------------
std::string MondayKey(int year, int month, int day, int weekday) {
    if (weekday < 1) weekday = 1;
    if (weekday > 7) weekday = 7;
    std::tm t{};
    t.tm_year = year - 1900;
    t.tm_mon  = month - 1;
    t.tm_mday = day;
    t.tm_hour = 12;
    t.tm_isdst = -1;
    std::time_t tt = std::mktime(&t);
    if (tt == static_cast<std::time_t>(-1)) return std::string();
    tt -= static_cast<std::time_t>((weekday - 1) * 86400);
    std::tm out{};
    localtime_s(&out, &tt);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d",
             out.tm_year + 1900, out.tm_mon + 1, out.tm_mday);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// 配置解析
// ---------------------------------------------------------------------------
// 返回 false 表示“没有配置过 ranges 字段”，此时使用默认整周；
// 返回 true 且 out 为空表示用户明确配置了空列表（不显示一文）。
bool ParseRanges(const char* json, std::vector<DayRange>& out) {
    out.clear();
    if (!json || !json[0]) return false;
    try {
        mini_json::Value root = mini_json::Value::parse(json);
        const mini_json::Value* arr = root.find("ranges");
        if (!arr || !arr->is_array()) return false;
        for (const auto& item : arr->as_array()) {
            const mini_json::Value* s = item.find("start");
            const mini_json::Value* e = item.find("end");
            if (!s || !e) continue;
            int start = static_cast<int>(s->as_number(0));
            int end   = static_cast<int>(e->as_number(0));
            if (start < 1 || start > 7 || end < 1 || end > 7 || start > end) {
                continue;
            }
            bool overlap = false;
            for (const auto& r : out) {
                if (start <= r.end && end >= r.start) { overlap = true; break; }
            }
            if (!overlap) out.push_back(DayRange{start, end});
        }
        return true;
    } catch (...) {
        out.clear();
        return false;
    }
}

// ---------------------------------------------------------------------------
// 状态读写
// ---------------------------------------------------------------------------
void LoadState(const WeekModule& m, const std::string& currentWeek,
               std::map<std::string, std::string>& assignments) {
    assignments.clear();
    std::string text;
    if (!ReadUtf8File(m.statePath, text) || text.empty()) return;
    try {
        mini_json::Value root = mini_json::Value::parse(text);
        const mini_json::Value* ws = root.find("weekStart");
        if (!ws || ws->as_string() != currentWeek) return; // 跨周自动失效
        const mini_json::Value* a = root.find("assignments");
        if (!a || !a->is_object()) return;
        for (const auto& kv : a->as_object()) {
            if (kv.second.is_string()) {
                assignments[kv.first] = kv.second.as_string();
            }
        }
    } catch (...) {
        assignments.clear();
    }
}

void SaveState(const WeekModule& m, const std::string& currentWeek,
               const std::map<std::string, std::string>& assignments) {
    using J = mini_json::Value;
    mini_json::Object root;
    root.emplace_back("weekStart", J(currentWeek));
    mini_json::Object assign;
    for (const auto& kv : assignments) {
        assign.emplace_back(kv.first, J(kv.second));
    }
    root.emplace_back("assignments", J(std::move(assign)));
    std::string text = J::serialize(J(std::move(root)), true);
    WriteUtf8File(m.statePath, text);
}

int FindByKey(const ArticleModuleHostApi* h, const std::string& key) {
    int count = h->article_count(h->host);
    for (int i = 0; i < count; ++i) {
        const char* k = h->article_key(h->host, i);
        if (k && key == k) return i;
    }
    return -1;
}

int PickUnseen(WeekModule* m, int currentIndex,
               const std::set<std::string>& reserved) {
    const ArticleModuleHostApi* h = m->host;
    int count = h->article_count(h->host);
    if (count <= 0) return -1;

    auto collect = [&](bool ignoreReserved) {
        std::vector<int> out;
        for (int i = 0; i < count; ++i) {
            if (i == currentIndex && count > 1) continue;
            const char* key = h->article_key(h->host, i);
            if (!key || !key[0]) continue;
            if (h->is_seen(h->host, key)) continue;
            if (!ignoreReserved && reserved.count(key)) continue;
            out.push_back(i);
        }
        return out;
    };

    std::vector<int> candidates = collect(false);
    if (candidates.empty()) {
        // 未看过的文章不足：清空记录后重试；仍不足时允许与其他时间段重复
        h->clear_seen(h->host);
        candidates = collect(false);
        if (candidates.empty()) candidates = collect(true);
    }
    if (candidates.empty()) candidates.push_back(0);
    return candidates[h->random_u32(h->host) % candidates.size()];
}

int SelectForToday(WeekModule* m, int currentIndex) {
    const ArticleModuleHostApi* h = m->host;
    int year = 0, month = 0, day = 0, weekday = 0, hour = 0, minute = 0, second = 0;
    h->local_time(h->host, &year, &month, &day, &weekday,
                  &hour, &minute, &second);

    int rangeIdx = -1;
    for (size_t i = 0; i < m->ranges.size(); ++i) {
        if (weekday >= m->ranges[i].start && weekday <= m->ranges[i].end) {
            rangeIdx = static_cast<int>(i);
            break;
        }
    }
    if (rangeIdx < 0) return -1; // 今天不属于任何范围：隐藏一文

    std::string week = MondayKey(year, month, day, weekday);
    if (week.empty()) return -1;

    std::map<std::string, std::string> assignments;
    LoadState(*m, week, assignments);

    std::string rangeKey = std::to_string(m->ranges[rangeIdx].start) + "-" +
                           std::to_string(m->ranges[rangeIdx].end);

    auto it = assignments.find(rangeKey);
    if (it != assignments.end() && !it->second.empty()) {
        int idx = FindByKey(h, it->second);
        if (idx >= 0) return idx; // 本周该范围已分配，保持同一篇
    }

    // 尽量不与其他时间段已分配的文章重复
    std::set<std::string> reserved;
    for (const auto& kv : assignments) {
        if (kv.first != rangeKey && !kv.second.empty()) {
            reserved.insert(kv.second);
        }
    }

    int idx = PickUnseen(m, currentIndex, reserved);
    if (idx < 0) return -1;

    const char* key = h->article_key(h->host, idx);
    if (key && key[0]) {
        assignments[rangeKey] = key;
        SaveState(*m, week, assignments);
    }
    return idx;
}

} // namespace

// ---------------------------------------------------------------------------
// 导出接口
// ---------------------------------------------------------------------------
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
        !host->clear_seen || !host->temp_dir || !host->local_time ||
        !host->random_u32) {
        return nullptr;
    }
    WeekModule* m = new (std::nothrow) WeekModule();
    if (!m) return nullptr;
    m->host = host;
    m->ranges.push_back(DayRange{1, 7}); // 未配置时默认整周
    m->statePath = std::wstring(host->temp_dir(host->host)) + L"\\week_state.json";
    return m;
}

ARTICLE_MODULE_EXPORT void ArticleModule_Destroy(void* module) {
    delete static_cast<WeekModule*>(module);
}

ARTICLE_MODULE_EXPORT void ArticleModule_ApplyConfig(void* module, const char* json) {
    if (!module) return;
    WeekModule* m = static_cast<WeekModule*>(module);
    std::vector<DayRange> ranges;
    if (!ParseRanges(json, ranges)) {
        ranges.push_back(DayRange{1, 7}); // 首次无配置：默认整周
    }
    m->ranges.swap(ranges);
}

ARTICLE_MODULE_EXPORT int ArticleModule_Select(void* module, int currentIndex,
                                               int /*reason*/) {
    if (!module) return -1;
    try {
        return SelectForToday(static_cast<WeekModule*>(module), currentIndex);
    } catch (...) {
        return -1;
    }
}
