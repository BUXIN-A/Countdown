// =============================================================================
// history.cpp - temp 记录实现
// =============================================================================
#include "history.h"
#include "utils.h"

#include <windows.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace history {
namespace {

// ---------------------------------------------------------------------------
// 目录解析
// ---------------------------------------------------------------------------
std::wstring ResolveTempDir() {
    std::wstring dir = utils::GetExeDir() + L"temp";
    if (utils::EnsureDirectory(dir)) return dir;

    // exe 位于 Program Files 等只读位置时回退到用户目录
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring base = std::wstring(buf, n) + L"\\Countdown";
        utils::EnsureDirectory(base);
        std::wstring fallback = base + L"\\temp";
        if (utils::EnsureDirectory(fallback)) return fallback;
    }
    return dir;
}

const wchar_t* FileName(Kind kind) {
    switch (kind) {
        case Kind::LocalHitokoto:   return L"hitokoto_local.txt";
        case Kind::NetworkHitokoto: return L"hitokoto_network.txt";
        default:                    return L"article.txt";
    }
}

std::wstring PathFor(Kind kind) {
    return TempDir() + L"\\" + FileName(kind);
}

// ---------------------------------------------------------------------------
// 每类一份缓存，避免每次随机都读文件
// ---------------------------------------------------------------------------
struct Store {
    std::mutex mu;
    bool loaded = false;
    std::set<std::string> keys;
};

Store& GetStore(Kind kind) {
    static Store stores[3];
    return stores[static_cast<int>(kind)];
}

// ---------------------------------------------------------------------------
// 清理策略
// ---------------------------------------------------------------------------
struct Policy {
    TempCleanupMode mode = TempCleanupMode::Count;
    TempTrimMode trim = TempTrimMode::ClearAll;
    int trimCount = 100;
    long long articleLimit = 5000;
    long long hitokotoLimit = 5000;
};

std::mutex g_policyMu;
Policy g_policy;

Policy SnapshotPolicy() {
    std::lock_guard<std::mutex> lock(g_policyMu);
    return g_policy;
}

Policy MakePolicy(const Config& cfg) {
    Policy p;
    p.mode = cfg.tempCleanupMode;
    p.trim = cfg.tempTrimMode;
    p.trimCount = std::max(1, cfg.tempTrimCount);
    if (cfg.tempCleanupMode == TempCleanupMode::Count) {
        p.articleLimit = std::max(1, cfg.tempArticleLimit);
        p.hitokotoLimit = std::max(1, cfg.tempHitokotoLimit);
    } else {
        p.articleLimit = cfg.TempArticleLimitBytes();
        p.hitokotoLimit = cfg.TempHitokotoLimitBytes();
    }
    return p;
}

long long LimitFor(const Policy& p, Kind kind) {
    return kind == Kind::Article ? p.articleLimit : p.hitokotoLimit;
}

// ---------------------------------------------------------------------------
// 文件辅助
// ---------------------------------------------------------------------------
bool FileSizeBytes(const std::wstring& path, unsigned long long& out) {
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        out = 0;
        return false;
    }
    ULARGE_INTEGER li;
    li.LowPart = fad.nFileSizeLow;
    li.HighPart = fad.nFileSizeHigh;
    out = li.QuadPart;
    return true;
}

bool ReadLines(const std::wstring& path, std::vector<std::string>& lines) {
    lines.clear();
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;
    std::string line;
    while (std::getline(ifs, line)) {
        while (!line.empty() &&
               (line.back() == '\r' || line.back() == ' ' || line.back() == '\t')) {
            line.pop_back();
        }
        while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
            line.erase(line.begin());
        }
        if (!line.empty()) lines.push_back(line);
        if (lines.size() > 1000000) break; // 防御异常文件
    }
    return true;
}

bool WriteLines(const std::wstring& path, const std::vector<std::string>& lines) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        for (const auto& line : lines) {
            ofs << line << "\r\n";
        }
        ofs.flush();
        if (!ofs) {
            ofs.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }
    return MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

void EnsureLoaded(Store& s, Kind kind) {
    if (s.loaded) return;
    s.loaded = true;

    std::wstring content;
    if (!utils::ReadTextFile(PathFor(kind), content, 64 * 1024 * 1024)) return;

    std::wistringstream ss(content);
    std::wstring line;
    while (std::getline(ss, line)) {
        while (!line.empty() &&
               (line.back() == L'\r' || std::iswspace(line.back()))) {
            line.pop_back();
        }
        while (!line.empty() && std::iswspace(line.front())) {
            line.erase(line.begin());
        }
        if (!line.empty()) s.keys.insert(utils::WideToUtf8(line));
    }
}

// 达到上限后的处理：清空全部 / 删除最早 N 项（调用方已持有锁）
void TrimLocked(Store& s, Kind kind, const Policy& p) {
    long long limit = LimitFor(p, kind);
    if (limit <= 0) return;

    bool over = false;
    if (p.mode == TempCleanupMode::Count) {
        over = static_cast<long long>(s.keys.size()) >= limit;
    } else {
        unsigned long long size = 0;
        FileSizeBytes(PathFor(kind), size);
        over = static_cast<long long>(size) >= limit;
    }
    if (!over) return;

    if (p.trim == TempTrimMode::ClearAll) {
        s.keys.clear();
        DeleteFileW(PathFor(kind).c_str());
        return;
    }

    // DropOldest：删除文件开头若干行，保证处理后的记录数/大小低于上限
    std::vector<std::string> lines;
    if (!ReadLines(PathFor(kind), lines) || lines.empty()) {
        s.keys.clear();
        DeleteFileW(PathFor(kind).c_str());
        return;
    }

    size_t drop = std::min(static_cast<size_t>(std::max(1, p.trimCount)),
                           lines.size());
    if (p.mode == TempCleanupMode::Count) {
        size_t total = lines.size();
        size_t target = static_cast<size_t>(std::max<long long>(1, limit));
        if (total >= target) {
            size_t need = total - target + 1;
            drop = std::max(drop, need);
        }
    } else {
        size_t fileBytes = 0;
        for (const auto& line : lines) fileBytes += line.size() + 2;
        size_t i = 0;
        while (i < lines.size() &&
               (i < drop || fileBytes >= static_cast<size_t>(limit))) {
            fileBytes -= std::min<size_t>(fileBytes, lines[i].size() + 2);
            ++i;
        }
        drop = i;
    }
    drop = std::min(drop, lines.size());
    lines.erase(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(drop));
    WriteLines(PathFor(kind), lines);

    s.keys.clear();
    for (const auto& line : lines) s.keys.insert(line);
}

} // anonymous namespace

// ---------------------------------------------------------------------------
const std::wstring& TempDir() {
    static const std::wstring dir = ResolveTempDir();
    return dir;
}

// ---------------------------------------------------------------------------
// 内容哈希（FNV-1a 64 位，按 UTF-16 码元）；先去除首尾空白
// ---------------------------------------------------------------------------
std::string KeyOfText(const std::wstring& text) {
    size_t begin = 0;
    while (begin < text.size() && std::iswspace(text[begin])) ++begin;
    size_t end = text.size();
    while (end > begin && std::iswspace(text[end - 1])) --end;

    uint64_t h = 1469598103934665603ull;
    for (size_t i = begin; i < end; ++i) {
        h ^= static_cast<uint64_t>(static_cast<unsigned short>(text[i]));
        h *= 1099511628211ull;
    }
    wchar_t hex[32];
    swprintf_s(hex, L"%016llx", static_cast<unsigned long long>(h));

    std::string out;
    out.reserve(16);
    for (const wchar_t* p = hex; *p; ++p) {
        out.push_back(static_cast<char>(*p));
    }
    return out;
}

// ---------------------------------------------------------------------------
bool IsSeen(Kind kind, const std::wstring& text) {
    if (text.empty()) return false;
    std::string key = KeyOfText(text);
    if (key.empty()) return false;

    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    return s.keys.find(key) != s.keys.end();
}

void CollectUnseen(Kind kind, const std::vector<std::wstring>& items,
                   std::vector<size_t>& out) {
    out.clear();
    if (items.empty()) return;

    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    for (size_t i = 0; i < items.size(); ++i) {
        std::string key = KeyOfText(items[i]);
        if (key.empty()) continue;
        if (s.keys.find(key) == s.keys.end()) out.push_back(i);
    }
}

bool IsSeenKey(Kind kind, const std::string& key) {
    if (key.empty()) return false;
    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    return s.keys.find(key) != s.keys.end();
}

void MarkSeen(Kind kind, const std::wstring& text) {
    if (text.empty()) return;
    std::string key = KeyOfText(text);
    if (key.empty()) return;
    MarkSeenKey(kind, key);
}

void MarkSeenKey(Kind kind, const std::string& key) {
    if (key.empty()) return;

    Policy policy = SnapshotPolicy();

    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    bool inserted = s.keys.insert(key).second;
    if (inserted) {
        std::ofstream ofs(PathFor(kind).c_str(), std::ios::binary | std::ios::app);
        if (ofs) {
            ofs << key << "\r\n";
            ofs.flush();
        }
    }
    TrimLocked(s, kind, policy);
}

int SeenCount(Kind kind) {
    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    return static_cast<int>(s.keys.size());
}

void Clear(Kind kind) {
    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    s.keys.clear();
    s.loaded = true;
    DeleteFileW(PathFor(kind).c_str());
}

void ApplyConfig(const Config& cfg) {
    Policy p = MakePolicy(cfg);
    {
        std::lock_guard<std::mutex> lock(g_policyMu);
        g_policy = p;
    }

    for (int i = 0; i < 3; ++i) {
        Kind kind = static_cast<Kind>(i);
        Store& s = GetStore(kind);
        std::lock_guard<std::mutex> lock(s.mu);
        EnsureLoaded(s, kind);
        TrimLocked(s, kind, p);
    }
}

void ClearAll() {
    for (int i = 0; i < 3; ++i) {
        Clear(static_cast<Kind>(i));
    }
}

} // namespace history
