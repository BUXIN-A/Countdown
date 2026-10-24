// =============================================================================
// utils.cpp - 工具实现
// =============================================================================
#include "utils.h"

#include <windows.h>
#include <string>
#include <algorithm>
#include <cstdio>
#include <set>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <ctime>

namespace utils {

// ---------------------------------------------------------------------------
// 字符串编码转换
// ---------------------------------------------------------------------------
std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                                  static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(),
                        static_cast<int>(utf8.size()), &wide[0], len);
    return wide;
}

std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                  static_cast<int>(wide.size()),
                                  nullptr, 0, nullptr, nullptr);
    std::string utf8(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                        static_cast<int>(wide.size()),
                        &utf8[0], len, nullptr, nullptr);
    return utf8;
}

// ---------------------------------------------------------------------------
// 系统默认字体
// ---------------------------------------------------------------------------
std::wstring GetSystemDefaultFontName() {
    NONCLIENTMETRICSW ncm{};
    ncm.cbSize = sizeof(ncm);
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        return ncm.lfMessageFont.lfFaceName; // 通常为 "Segoe UI"
    }
    return L"Segoe UI";
}

// ---------------------------------------------------------------------------
// 字体枚举：使用 EnumFontFamiliesEx 收集所有字体族
// ---------------------------------------------------------------------------
struct EnumContext {
    std::set<std::wstring> names; // 去重
};

static BOOL CALLBACK EnumFontFamProc(const LOGFONTW* lplf,
                                     const TEXTMETRICW* /*lptm*/,
                                     DWORD /*dwType*/,
                                     LPARAM lParam) {
    EnumContext* ctx = reinterpret_cast<EnumContext*>(lParam);
    std::wstring face = lplf->lfFaceName;
    // 跳过以 @ 开头的竖排字体
    if (!face.empty() && face[0] != L'@') {
        ctx->names.insert(face);
    }
    return TRUE;
}

std::vector<std::wstring> EnumerateSystemFonts() {
    EnumContext ctx;
    HDC hdc = GetDC(nullptr);
    if (hdc) {
        LOGFONTW lf{};
        lf.lfCharSet = DEFAULT_CHARSET;
        lf.lfFaceName[0] = L'\0';
        EnumFontFamiliesExW(hdc, &lf, EnumFontFamProc,
                            reinterpret_cast<LPARAM>(&ctx), 0);
        ReleaseDC(nullptr, hdc);
    }
    std::vector<std::wstring> result(ctx.names.begin(), ctx.names.end());
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// 本地字体（exe\fonts）：AddFontResourceEx 仅对当前进程生效，不装进系统
// ---------------------------------------------------------------------------
static std::wstring LowerPath(std::wstring path) {
    if (!path.empty()) CharLowerW(&path[0]);
    return path;
}

static std::vector<std::wstring> ListFontFiles(const std::wstring& dir) {
    std::vector<std::wstring> files;
    static const wchar_t* kPatterns[] = {
        L"*.ttf", L"*.otf", L"*.ttc", L"*.otc",
    };

    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') {
        base += L'\\';
    }
    for (const wchar_t* pattern : kPatterns) {
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW((base + pattern).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            files.push_back(base + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return files;
}

std::vector<std::wstring> LoadLocalFonts() {
    static std::set<std::wstring> loadedFiles;   // 已加载过的文件（小写路径）
    static std::set<std::wstring> localFamilies; // 本进程加载出的字体族
    static std::set<std::wstring> systemFamilies;
    static bool baselineCaptured = false;

    // 首次调用先记录系统原有字体，之后新增的字体族即视为本地字体
    if (!baselineCaptured) {
        auto sys = EnumerateSystemFonts();
        systemFamilies.insert(sys.begin(), sys.end());
        baselineCaptured = true;
    }

    std::wstring dir = GetExeDir() + L"fonts";
    EnsureDirectory(dir);
    std::vector<std::wstring> files = ListFontFiles(dir);

    bool added = false;
    std::set<std::wstring> present;
    for (const auto& file : files) {
        std::wstring key = LowerPath(file);
        present.insert(key);
        if (loadedFiles.count(key)) continue;
        if (AddFontResourceExW(file.c_str(), FR_PRIVATE, 0) != 0) {
            loadedFiles.insert(key);
            added = true;
        }
    }
    // 文件被移走后从“已加载”集合里剔除，下次重新出现时会再次加载；
    // 加载失败的文件不加入 loadedFiles，下次打开设置会重试
    for (auto it = loadedFiles.begin(); it != loadedFiles.end(); ) {
        if (!present.count(*it)) it = loadedFiles.erase(it);
        else ++it;
    }

    if (added) {
        auto after = EnumerateSystemFonts();
        for (const auto& name : after) {
            if (!systemFamilies.count(name)) localFamilies.insert(name);
        }
    }

    std::vector<std::wstring> result(localFamilies.begin(), localFamilies.end());
    std::sort(result.begin(), result.end());
    return result;
}

// ---------------------------------------------------------------------------
// 颜色工具
// ---------------------------------------------------------------------------
std::string ColorToHex(COLORREF color) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  static_cast<int>(GetRValue(color)),
                  static_cast<int>(GetGValue(color)),
                  static_cast<int>(GetBValue(color)));
    return std::string(buf);
}

static int HexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool ParseHexColor(const std::string& text, COLORREF& out) {
    std::string s = text;
    size_t begin = 0;
    while (begin < s.size() && (s[begin] == ' ' || s[begin] == '\t')) ++begin;
    s = s.substr(begin);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    if (!s.empty() && s[0] == '#') s.erase(0, 1);
    if (s.size() != 6) return false;

    int v[6];
    for (int i = 0; i < 6; ++i) {
        v[i] = HexDigit(s[i]);
        if (v[i] < 0) return false;
    }
    out = RGB(v[0] * 16 + v[1], v[2] * 16 + v[3], v[4] * 16 + v[5]);
    return true;
}

static int ClampChannel(int v) {
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

COLORREF ScaleColor(COLORREF color, float factor) {
    return RGB(ClampChannel(static_cast<int>(GetRValue(color) * factor + 0.5f)),
               ClampChannel(static_cast<int>(GetGValue(color) * factor + 0.5f)),
               ClampChannel(static_cast<int>(GetBValue(color) * factor + 0.5f)));
}

COLORREF BlendColor(COLORREF a, COLORREF b, float t) {
    auto mix = [t](int from, int to) {
        return ClampChannel(static_cast<int>(from + (to - from) * t + 0.5f));
    };
    return RGB(mix(GetRValue(a), GetRValue(b)),
               mix(GetGValue(a), GetGValue(b)),
               mix(GetBValue(a), GetBValue(b)));
}

// ---------------------------------------------------------------------------
// 剩余时长格式化
// ---------------------------------------------------------------------------
std::wstring FormatRemainingTime(long long remaining_seconds, DisplayFormat fmt,
                                 bool includeStartDay) {
    if (remaining_seconds <= 0) {
        return L"\u5df2\u7ed3\u675f"; // "已结束"
    }
    // 起始日计入：统一在剩余时间上加一天（已结束状态不追加）
    if (includeStartDay) remaining_seconds += 86400;

    if (fmt == DisplayFormat::HMS) {
        // HH:MM:SS，总小时可超 24（不补齐宽度上限）
        long long hours   = remaining_seconds / 3600;
        long long minutes = (remaining_seconds / 60) % 60;
        long long seconds = remaining_seconds % 60;
        std::wostringstream os;
        os << hours << L":"
           << std::setw(2) << std::setfill(L'0') << minutes << L":"
           << std::setw(2) << std::setfill(L'0') << seconds;
        return os.str();
    }

    if (fmt == DisplayFormat::Days) {
        // DDDD HH:MM:SS：总天数(可 >99) + 空格 + 时:分:秒（时分秒补零）
        long long days   = remaining_seconds / 86400;
        long long hour   = (remaining_seconds / 3600) % 24;
        long long minute = (remaining_seconds / 60) % 60;
        long long second = remaining_seconds % 60;
        std::wostringstream os;
        os << days << L"\u5929 "; // "天 "
        os << std::setw(2) << std::setfill(L'0') << hour << L":"
           << std::setw(2) << std::setfill(L'0') << minute << L":"
           << std::setw(2) << std::setfill(L'0') << second;
        return os.str();
    }

    // Full：N年M月D日 H时M分S秒
    // 用真实日历拆分剩余天数：从当前日期起逐月累加（mktime 自动处理
    // 跨年、闰月），而不是用"1 年=365 天、1 月=30 天"的近似算法。
    long long days   = remaining_seconds / 86400;
    long long hour   = (remaining_seconds / 3600) % 24;
    long long minute = (remaining_seconds / 60) % 60;
    long long second = remaining_seconds % 60;

    std::time_t now = std::time(nullptr);
    std::tm cur{};
    localtime_s(&cur, &now);
    cur.tm_isdst = -1;

    long long months_total = 0;
    long long days_left = days;
    // 循环上限：最多推进 2400 个月（200 年）。防御极端目标时间
    // （如 config 手写 9999 年）导致每秒一次的逐月循环过长造成瞬时卡顿
    const long long kMaxMonths = 2400;
    while (months_total < kMaxMonths) {
        std::tm next = cur;
        next.tm_mon += 1;
        std::mktime(&next); // 规范化：跨年/闰月/月末自动进位
        std::time_t t_cur  = std::mktime(&cur);
        std::time_t t_next = std::mktime(&next);
        // +3600 抵消 DST 切换造成的 1 小时偏移
        long long month_days = (t_next - t_cur + 3600) / 86400;
        if (days_left >= month_days) {
            days_left -= month_days;
            ++months_total;
            cur = next;
        } else {
            break;
        }
    }
    long long years  = months_total / 12;
    long long months = months_total % 12;

    // 输出：数值为 0 的分量连单位一起省略（如 0年/0月 不显示），
    // 时分秒始终显示并补零，保证倒计时秒位连续变化
    std::wostringstream os;
    if (years > 0)  os << years << L"\u5e74";
    if (months > 0) os << months << L"\u6708";
    if (days_left > 0) os << days_left << L"\u65e5";
    os << std::setw(2) << std::setfill(L'0') << hour << L"\u65f6"
       << std::setw(2) << std::setfill(L'0') << minute << L"\u5206"
       << std::setw(2) << std::setfill(L'0') << second << L"\u79d2";
    return os.str();
}

// ---------------------------------------------------------------------------
// 目标时间格式化 / 解析
// ---------------------------------------------------------------------------
std::wstring FormatTargetTime(const std::tm& t) {
    std::wostringstream os;
    os << std::setw(4) << std::setfill(L'0') << (t.tm_year + 1900) << L"-"
       << std::setw(2) << std::setfill(L'0') << (t.tm_mon + 1)     << L"-"
       << std::setw(2) << std::setfill(L'0') << t.tm_mday          << L" "
       << std::setw(2) << std::setfill(L'0') << t.tm_hour          << L":"
       << std::setw(2) << std::setfill(L'0') << t.tm_min           << L":"
       << std::setw(2) << std::setfill(L'0') << t.tm_sec;
    return os.str();
}

bool ParseTargetTime(const std::wstring& s, std::tm& out) {
    std::wistringstream is(s);
    is >> std::get_time(&out, L"%Y-%m-%d %H:%M:%S");
    if (is.fail()) return false;
    // get_time 对越界字段（如 13 月、40 日、25 时）不报错，需额外校验：
    // 拒绝尾部多余字符；并通过"规范化前后字段一致"识别非法日期。
    is >> std::ws;
    if (!is.eof()) return false;
    std::tm chk = out;
    if (std::mktime(&chk) == (std::time_t)-1) return false;
    if (chk.tm_year != out.tm_year || chk.tm_mon != out.tm_mon ||
        chk.tm_mday != out.tm_mday || chk.tm_hour != out.tm_hour ||
        chk.tm_min != out.tm_min || chk.tm_sec != out.tm_sec) {
        return false; // 输入为非法日期，被 mktime 规范化后字段发生变化
    }
    // 关闭 DST 自动推算，避免 1 小时偏差
    out.tm_isdst = 0;
    return true;
}

// ---------------------------------------------------------------------------
// 获取 exe 目录
// ---------------------------------------------------------------------------
std::wstring GetExeDir() {
    wchar_t path[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return L".\\";
    return p.substr(0, pos + 1);
}

// ---------------------------------------------------------------------------
// 文本文件 / 目录
// ---------------------------------------------------------------------------
bool EnsureDirectory(const std::wstring& path) {
    if (CreateDirectoryW(path.c_str(), nullptr)) return true;
    return GetLastError() == ERROR_ALREADY_EXISTS;
}

std::vector<std::wstring> ListTxtFiles(const std::wstring& dir) {
    std::vector<std::wstring> files;
    std::wstring pattern = dir;
    if (!pattern.empty() && pattern.back() != L'\\' && pattern.back() != L'/') {
        pattern += L'\\';
    }
    pattern += L"*.txt";

    WIN32_FIND_DATAW fd{};
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;

    std::wstring base = dir;
    if (!base.empty() && base.back() != L'\\' && base.back() != L'/') {
        base += L'\\';
    }
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        files.push_back(base + fd.cFileName);
    } while (FindNextFileW(h, &fd));

    FindClose(h);
    std::sort(files.begin(), files.end());
    return files;
}

bool ReadTextFile(const std::wstring& path, std::wstring& out,
                  size_t maxBytes) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;

    std::string content;
    content.resize(maxBytes);
    std::streamsize got = ifs.read(&content[0],
        static_cast<std::streamsize>(maxBytes)).gcount();
    content.resize(static_cast<size_t>(got));

    // 容错 UTF-8 BOM
    if (content.size() >= 3 &&
        static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
        content.erase(0, 3);
    }
    out = Utf8ToWide(content);
    return true;
}

bool WriteTextFile(const std::wstring& path, const std::wstring& text) {
    std::ofstream ofs(path.c_str(), std::ios::binary | std::ios::trunc);
    if (!ofs) return false;
    std::string utf8 = WideToUtf8(text);
    ofs.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(ofs);
}

// ---------------------------------------------------------------------------
// DPI
// ---------------------------------------------------------------------------
int GetWindowDpi(HWND hwnd) {
    // 动态解析 GetDpiForWindow：避免在不支持该 API 的系统上加载失败
    using GetDpiForWindowFn = UINT(WINAPI*)(HWND);
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow"));
    if (hwnd && fn) {
        UINT dpi = fn(hwnd);
        if (dpi != 0) return static_cast<int>(dpi);
    }
    HDC dc = GetDC(nullptr);
    if (dc) {
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(nullptr, dc);
        if (dpi > 0) return dpi;
    }
    return 96;
}

int ScaleDpi(int value, int dpi) {
    return MulDiv(value, dpi > 0 ? dpi : 96, 96);
}

} // namespace utils
