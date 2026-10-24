// =============================================================================
// history.cpp - temp 记录实现
// =============================================================================
#include "history.h"
#include "utils.h"

#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cwctype>
#include <fstream>
#include <mutex>
#include <set>
#include <sstream>

namespace history {

// ---------------------------------------------------------------------------
// 目录解析
// ---------------------------------------------------------------------------
static std::wstring ResolveTempDir() {
    std::wstring dir = utils::GetExeDir() + L"temp";
    if (utils::EnsureDirectory(dir)) return dir;

    // exe 位于 Program Files 等只读位置时回退到用户目录
    wchar_t buf[MAX_PATH] = {0};
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n > 0 && n < MAX_PATH) {
        std::wstring base = std::wstring(buf, n) + L"\\Countdown";
        utils::EnsureDirectory(base); // 先建父目录（CreateDirectory 不递归）
        std::wstring fallback = base + L"\\temp";
        if (utils::EnsureDirectory(fallback)) return fallback;
    }
    return dir; // 都失败时仍返回默认路径，写入会静默降级为仅内存去重
}

const std::wstring& TempDir() {
    // 函数内静态：C++11 起初始化线程安全（网络线程也会首次调用）
    static const std::wstring dir = ResolveTempDir();
    return dir;
}

static const wchar_t* FileName(Kind kind) {
    switch (kind) {
        case Kind::LocalHitokoto:   return L"hitokoto_local.txt";
        case Kind::NetworkHitokoto: return L"hitokoto_network.txt";
        default:                    return L"article.txt";
    }
}

static std::wstring PathFor(Kind kind) {
    return TempDir() + L"\\" + FileName(kind);
}

// ---------------------------------------------------------------------------
// 内容哈希（FNV-1a 64 位，按 UTF-16 码元）；先去除首尾空白
// ---------------------------------------------------------------------------
static std::string KeyOf(const std::wstring& text) {
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
    for (const wchar_t* p = hex; *p; ++p) out.push_back(static_cast<char>(*p));
    return out;
}

// ---------------------------------------------------------------------------
// 每类一份缓存，避免每次随机都读文件
// ---------------------------------------------------------------------------
struct Store {
    std::mutex mu;
    bool loaded = false;
    std::set<std::string> keys;
};

static Store& GetStore(Kind kind) {
    static Store stores[3];
    return stores[static_cast<int>(kind)];
}

static void EnsureLoaded(Store& s, Kind kind) {
    if (s.loaded) return;
    s.loaded = true;

    std::wstring content;
    if (!utils::ReadTextFile(PathFor(kind), content, 4 * 1024 * 1024)) return;

    std::wistringstream ss(content);
    std::wstring line;
    while (std::getline(ss, line)) {
        while (!line.empty() && (line.back() == L'\r' || std::iswspace(line.back())))
            line.pop_back();
        while (!line.empty() && std::iswspace(line.front()))
            line.erase(line.begin());
        if (!line.empty()) s.keys.insert(utils::WideToUtf8(line));
    }
}

// ---------------------------------------------------------------------------
bool IsSeen(Kind kind, const std::wstring& text) {
    if (text.empty()) return false;
    std::string key = KeyOf(text);
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
        std::string key = KeyOf(items[i]);
        if (key.empty()) continue;
        if (s.keys.find(key) == s.keys.end()) out.push_back(i);
    }
}

void MarkSeen(Kind kind, const std::wstring& text) {
    if (text.empty()) return;
    std::string key = KeyOf(text);
    if (key.empty()) return;

    Store& s = GetStore(kind);
    std::lock_guard<std::mutex> lock(s.mu);
    EnsureLoaded(s, kind);
    if (!s.keys.insert(key).second) return; // 已记录过

    std::ofstream ofs(PathFor(kind).c_str(), std::ios::binary | std::ios::app);
    if (ofs) {
        ofs << key << "\r\n";
        ofs.flush();
    }
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

} // namespace history
