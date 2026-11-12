// =============================================================================
// update_dialog.cpp - 获取更新窗口实现
//
// 流程：
//   Checking      请求 GitHub Release API 获取最新版本
//   HasUpdate     显示新版号与更新详情，让用户选择是否更新
//   Assets        展示附件列表：下载到下载目录 / 手动下载（浏览器）
//   Downloading   进度百分比 + 取消
//   DownloadFailed / Failed  回退手动下载；检查失败直接打开发布页
//   Done          下载完成，可定位到文件
// =============================================================================
#include "update_dialog.h"
#include "json.h"
#include "utils.h"
#include "version.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// ---------------------------------------------------------------------------
// 配色（与设置窗口保持一致）
// ---------------------------------------------------------------------------
static const COLORREF kBg         = RGB(24, 24, 30);
static const COLORREF kDivider    = RGB(44, 44, 54);
static const COLORREF kLabel      = RGB(212, 212, 221);
static const COLORREF kMuted      = RGB(138, 138, 150);
static const COLORREF kChipBg     = RGB(36, 36, 45);
static const COLORREF kChipBorder = RGB(56, 56, 69);
static const COLORREF kValueText  = RGB(233, 233, 240);
static const COLORREF kAccent     = RGB(62, 94, 240);
static const COLORREF kAccentHi   = RGB(92, 124, 255);
static const COLORREF kDanger     = RGB(214, 92, 92);
static const COLORREF kOnBg       = RGB(38, 48, 88);
static const COLORREF kOnBorder   = RGB(80, 110, 255);

// ---------------------------------------------------------------------------
// 布局常量（96 DPI 逻辑像素）
// ---------------------------------------------------------------------------
static const int kWinW      = 560;
static const int kWinH      = 470;
static const int kHeaderH   = 44;
static const int kPad       = 20;
static const int kBtnH      = 30;
static const int kAssetRowH = 52;
static const int kAssetGap  = 8;

static const wchar_t* kClass = L"CountdownUpdateClass";

// ---------------------------------------------------------------------------
// 后台线程 -> UI 线程消息
// ---------------------------------------------------------------------------
enum : UINT {
    WM_UPDATE_CHECK_DONE = WM_APP + 201,
    WM_UPDATE_PROGRESS   = WM_APP + 202,
    WM_UPDATE_DL_DONE    = WM_APP + 203,
};

enum UpdateButtonId {
    BtnNone = 0,
    BtnClose,
    BtnYes,
    BtnNo,
    BtnManual,
    BtnBack,
    BtnCancelDownload,
    BtnRetry,
    BtnOpenFolder,
    BtnOpenRelease,
    BtnLineDirect,
    BtnLineMirror,
    BtnRetryMirror,
    BtnRetryDirect,
    BtnAssetBase = 1000,
};

struct UiButton {
    int          id = 0;
    RECT         rc{};
    std::wstring text;
    bool         primary = false;
    bool         small = false;
    bool         toggle = false;   // 分段选择（直连/镜像）
    bool         selected = false;
};

struct ReleaseAsset {
    std::wstring name;
    std::wstring url;
    long long    size = 0;
};

struct ReleaseInfo {
    std::wstring tag;     // v2.3.0
    std::wstring version; // 2.3.0
    std::wstring name;
    std::wstring body;
    std::wstring htmlUrl;
    std::vector<ReleaseAsset> assets;
};

// 后台线程与 UI 线程共享的数据：worker 只写这里，不直接触碰窗口状态
struct SharedState {
    std::mutex               mtx;
    std::atomic<bool>        cancel{false};
    std::atomic<HWND>        hwnd{nullptr};

    bool                     checkOk = false;
    std::shared_ptr<ReleaseInfo> info;
    std::wstring             checkError;

    bool                     dlOk = false;
    bool                     dlCancelled = false;
    std::wstring             dlPath;
    std::wstring             dlPartialPath; // 正在写入的临时文件，取消/失败时删除
    std::wstring             dlError;
    int                      progress = 0;
    long long                downloaded = 0; // 已下载字节，供无 Content-Length 时展示
};

enum class UpdateStage {
    Checking,
    UpToDate,
    HasUpdate,
    Assets,
    Downloading,
    DownloadFailed,
    Done,
    Failed,
};

struct UpdateState {
    HWND        hwnd = nullptr;
    HWND        owner = nullptr;
    UINT        dpi = 96;
    UpdateStage stage = UpdateStage::Checking;

    HFONT  font = nullptr;
    HFONT  boldFont = nullptr;
    HFONT  smallFont = nullptr;
    HFONT  titleFont = nullptr;
    HBRUSH brBg = nullptr;

    HDC     memDC = nullptr;
    HBITMAP memBmp = nullptr;
    HGDIOBJ memOld = nullptr;
    int     memW = 0;
    int     memH = 0;

    std::shared_ptr<SharedState> shared;
    std::shared_ptr<ReleaseInfo> info;

    std::wstring errorText;
    std::wstring downloadName;
    std::wstring savedPath;
    int  progress = 0;
    bool indeterminate = false; // 服务器未返回 Content-Length 时用流动进度条
    int  detailScroll = 0;
    int  assetScroll = 0;
    int  detailContentH = 0;
    int  detailViewH = 0;
    int  spinPhase = 0;

    // 下载线路（手动选择）
    bool useMirror = false;
    int  lastAssetIndex = -1;
    bool lastUsedMirror = false;

    std::vector<UiButton> buttons;
    int  hoverBtn = BtnNone;
    int  pressedBtn = BtnNone;
    bool trackingLeave = false;

    bool running = true;

    int S(int v) const { return utils::ScaleDpi(v, static_cast<int>(dpi)); }
};

// ---------------------------------------------------------------------------
// 基础绘制辅助
// ---------------------------------------------------------------------------
static void FillRound(HDC dc, const RECT& rc, int radius, COLORREF fill,
                      COLORREF border) {
    int h = rc.bottom - rc.top;
    int w = rc.right - rc.left;
    if (w <= 0 || h <= 0) return;
    int r = std::max(0, std::min(radius, std::min(w, h) / 2));
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBrush = SelectObject(dc, brush);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, r * 2, r * 2);
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
}

static void DrawTextIn(HDC dc, const std::wstring& text, RECT rc, HFONT font,
                       COLORREF color, UINT format) {
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    SetTextColor(dc, color);
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc,
              format | DT_NOPREFIX);
    SelectObject(dc, old);
}

static void DrawCentered(HDC dc, const RECT& rc, const std::wstring& text,
                         HFONT font, COLORREF color) {
    DrawTextIn(dc, text, rc, font, color,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static std::wstring FormatBytes(long long bytes) {
    if (bytes <= 0) return L"";
    if (bytes < 1024) return std::to_wstring(bytes) + L" B";
    const wchar_t* units[] = {L"KB", L"MB", L"GB"};
    double v = static_cast<double>(bytes) / 1024.0;
    int u = 0;
    while (v >= 1024.0 && u < 2) {
        v /= 1024.0;
        ++u;
    }
    wchar_t num[32];
    swprintf_s(num, L"%.1f", v);
    return std::wstring(num) + L" " + units[u];
}

static std::wstring TrimTag(const std::wstring& tag) {
    std::wstring s = tag;
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.erase(0, 1);
    if (!s.empty() && (s[0] == L'v' || s[0] == L'V')) s.erase(0, 1);
    return s;
}

// 解析 "2.3.0" / "v2.3.0" / "2.3.0-beta" 形式的主版本号
static void ParseVersion(const std::wstring& text, int out[3]) {
    out[0] = out[1] = out[2] = 0;
    std::wstring s = TrimTag(text);
    size_t pos = 0;
    for (int i = 0; i < 3; ++i) {
        size_t dot = s.find(L'.', pos);
        std::wstring part =
            s.substr(pos, dot == std::wstring::npos ? std::wstring::npos : dot - pos);
        out[i] = static_cast<int>(wcstol(part.c_str(), nullptr, 10));
        if (dot == std::wstring::npos) break;
        pos = dot + 1;
    }
}

// latest > current 返回正数
static int CompareVersion(const std::wstring& latest, const std::wstring& current) {
    int a[3], b[3];
    ParseVersion(latest, a);
    ParseVersion(current, b);
    for (int i = 0; i < 3; ++i) {
        if (a[i] != b[i]) return a[i] - b[i];
    }
    return 0;
}

static void OpenUrl(const std::wstring& url) {
    if (url.empty()) return;
    ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

static std::wstring ReleasePageUrl() {
    return APP_REPO_RELEASES_URL;
}

static bool MirrorAvailable() { return APP_MIRROR_PREFIX[0] != L'\0'; }

// 镜像格式：<镜像前缀><原始 GitHub 链接>（前缀可带或不带结尾斜杠）
static std::wstring MirrorUrl(const std::wstring& url) {
    std::wstring prefix = APP_MIRROR_PREFIX;
    if (!prefix.empty() && prefix.back() != L'/') prefix.push_back(L'/');
    return prefix + url;
}

static void RevealInExplorer(const std::wstring& path) {
    if (path.empty()) return;
    std::wstring args = L"/select,\"" + path + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr,
                  SW_SHOWNORMAL);
}

// 用户下载目录（失败时回退 %USERPROFILE%\Downloads）
static std::wstring DownloadsDir() {
    // FOLDERID_Downloads 的 GUID 直接写字面量，避免依赖 knownfolders 库
    static const GUID kDownloads = {
        0x374de290, 0x123f, 0x4565,
        {0x91, 0x64, 0x39, 0xc4, 0x92, 0x5e, 0x46, 0x7b}};
    PWSTR path = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(kDownloads, 0, nullptr, &path)) && path) {
        std::wstring dir(path);
        CoTaskMemFree(path);
        if (!dir.empty()) return dir;
    }
    wchar_t buf[MAX_PATH] = {0};
    if (GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH) > 0) {
        return std::wstring(buf) + L"\\Downloads";
    }
    return utils::GetExeDir();
}

static std::wstring SanitizeFileName(const std::wstring& name) {
    std::wstring out;
    out.reserve(name.size());
    for (wchar_t c : name) {
        if (c == L'<' || c == L'>' || c == L':' || c == L'"' || c == L'/' ||
            c == L'\\' || c == L'|' || c == L'?' || c == L'*' || c < 32) {
            out.push_back(L'_');
        } else {
            out.push_back(c);
        }
    }
    if (out.empty()) out = L"update.bin";
    return out;
}

// 目标文件已存在时追加序号，避免覆盖
static std::wstring MakeUniquePath(const std::wstring& dir,
                                   const std::wstring& name) {
    std::wstring base = name;
    std::wstring ext;
    size_t dot = name.find_last_of(L'.');
    if (dot != std::wstring::npos && dot > 0) {
        base = name.substr(0, dot);
        ext = name.substr(dot);
    }
    std::wstring candidate = dir + L"\\" + name;
    int n = 1;
    while (GetFileAttributesW(candidate.c_str()) != INVALID_FILE_ATTRIBUTES) {
        candidate = dir + L"\\" + base + L" (" + std::to_wstring(n) + L")" + ext;
        ++n;
        if (n > 999) break;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
// WinHTTP 封装
// ---------------------------------------------------------------------------
struct HttpHandles {
    HINTERNET session = nullptr;
    HINTERNET connect = nullptr;
    HINTERNET request = nullptr;

    ~HttpHandles() {
        if (request) WinHttpCloseHandle(request);
        if (connect) WinHttpCloseHandle(connect);
        if (session) WinHttpCloseHandle(session);
    }
};

static bool OpenHttpGet(HttpHandles& h, const std::wstring& url,
                        std::wstring& error) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc)) {
        error = L"无效的地址";
        return false;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path;
    if (uc.lpszUrlPath) path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";

    h.session = WinHttpOpen(L"Countdown/" APP_VERSION_STRING,
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!h.session) {
        error = L"初始化网络组件失败";
        return false;
    }
    WinHttpSetTimeouts(h.session, 5000, 6000, 10000, 20000);

    h.connect = WinHttpConnect(h.session, host.c_str(), uc.nPort, 0);
    if (!h.connect) {
        error = L"无法连接服务器";
        return false;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    h.request = WinHttpOpenRequest(h.connect, L"GET", path.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   flags);
    if (!h.request) {
        error = L"创建请求失败";
        return false;
    }
    // Release 附件会 302 到 objects.githubusercontent.com，必须允许自动跳转
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(h.request, WINHTTP_OPTION_REDIRECT_POLICY, &policy,
                     sizeof(policy));

    if (!WinHttpSendRequest(h.request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        error = L"请求发送失败";
        return false;
    }
    if (!WinHttpReceiveResponse(h.request, nullptr)) {
        error = L"服务器无响应";
        return false;
    }

    DWORD status = 0;
    DWORD len = sizeof(status);
    WinHttpQueryHeaders(h.request,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len,
                        WINHTTP_NO_HEADER_INDEX);
    if (status < 200 || status >= 300) {
        error = L"服务器返回 HTTP " + std::to_wstring(status);
        return false;
    }
    return true;
}

static bool ReadResponseText(HINTERNET request, std::string& out,
                             std::wstring& error) {
    out.clear();
    DWORD available = 0;
    while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, &chunk[0], available, &read)) {
            error = L"读取响应失败";
            return false;
        }
        if (read == 0) break;
        out.append(chunk.data(), read);
        if (out.size() > 4 * 1024 * 1024) break; // 防止异常超大响应
    }
    return true;
}

static std::shared_ptr<ReleaseInfo> ParseRelease(const std::string& text,
                                                 std::wstring& error) {
    mini_json::Value root;
    try {
        root = mini_json::Value::parse(text);
    } catch (...) {
        error = L"更新信息解析失败";
        return nullptr;
    }
    if (!root.is_object()) {
        error = L"更新信息格式异常";
        return nullptr;
    }
    auto info = std::make_shared<ReleaseInfo>();
    if (const mini_json::Value* v = root.find("tag_name")) {
        info->tag = utils::Utf8ToWide(v->as_string());
    }
    info->version = TrimTag(info->tag);
    if (const mini_json::Value* v = root.find("name")) {
        info->name = utils::Utf8ToWide(v->as_string());
    }
    if (const mini_json::Value* v = root.find("body")) {
        info->body = utils::Utf8ToWide(v->as_string());
    }
    if (const mini_json::Value* v = root.find("html_url")) {
        info->htmlUrl = utils::Utf8ToWide(v->as_string());
    }
    if (info->htmlUrl.empty()) info->htmlUrl = ReleasePageUrl();
    if (const mini_json::Value* assets = root.find("assets")) {
        if (assets->is_array()) {
            for (const mini_json::Value& item : assets->as_array()) {
                if (!item.is_object()) continue;
                ReleaseAsset asset;
                if (const mini_json::Value* n = item.find("name")) {
                    asset.name = utils::Utf8ToWide(n->as_string());
                }
                if (const mini_json::Value* u = item.find("browser_download_url")) {
                    asset.url = utils::Utf8ToWide(u->as_string());
                }
                if (const mini_json::Value* s = item.find("size")) {
                    asset.size = static_cast<long long>(s->as_number(0));
                }
                if (asset.url.empty()) continue;
                if (asset.name.empty()) asset.name = L"update.bin";
                info->assets.push_back(asset);
            }
        }
    }
    if (info->version.empty()) {
        error = L"未获取到版本号";
        return nullptr;
    }
    return info;
}

// 后台：查询最新 Release
static void DoCheckUpdates(std::shared_ptr<SharedState> shared) {
    std::wstring error;
    std::string body;
    bool ok = false;

    HttpHandles h;
    if (OpenHttpGet(h, APP_REPO_LATEST_API,
                    error)) {
        ok = ReadResponseText(h.request, body, error);
    }

    std::shared_ptr<ReleaseInfo> info;
    if (ok) {
        if (shared->cancel.load()) ok = false;
        else {
            info = ParseRelease(body, error);
            if (!info) ok = false;
        }
    }

    {
        std::lock_guard<std::mutex> lock(shared->mtx);
        shared->checkOk = ok;
        shared->info = info;
        shared->checkError = ok ? std::wstring() : error;
    }
    HWND hwnd = shared->hwnd.load();
    if (hwnd) PostMessageW(hwnd, WM_UPDATE_CHECK_DONE, 0, 0);
}

// 删除未完成的下载文件：句柄刚关闭时可能被杀软/索引服务短暂占用，做有限重试
static void RemoveFileBestEffort(const std::wstring& path) {
    if (path.empty()) return;
    for (int attempt = 0; attempt < 6; ++attempt) {
        if (DeleteFileW(path.c_str())) return;
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) return;
        if (err != ERROR_ACCESS_DENIED && err != ERROR_SHARING_VIOLATION &&
            err != ERROR_LOCK_VIOLATION) {
            return;
        }
        Sleep(60);
    }
}

// 后台：下载附件到指定路径
static void DoDownloadAsset(std::shared_ptr<SharedState> shared,
                            std::wstring url, std::wstring path) {
    std::wstring error;
    bool ok = false;
    bool cancelled = false;
    HANDLE file = INVALID_HANDLE_VALUE;

    do {
        HttpHandles h;
        if (!OpenHttpGet(h, url, error)) break;

        DWORD total = 0;
        DWORD len = sizeof(total);
        WinHttpQueryHeaders(h.request,
                            WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &total, &len,
                            WINHTTP_NO_HEADER_INDEX);

        file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            error = L"无法写入下载目录";
            break;
        }
        {
            std::lock_guard<std::mutex> lock(shared->mtx);
            shared->dlPartialPath = path;
        }

        long long written = 0;
        int lastPct = -1;
        DWORD lastPost = 0;
        while (true) {
            if (shared->cancel.load()) {
                cancelled = true;
                break;
            }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(h.request, &available)) {
                error = L"网络中断，下载失败";
                break;
            }
            if (available == 0) {
                ok = true;
                break;
            }
            std::vector<char> buf(available);
            DWORD read = 0;
            if (!WinHttpReadData(h.request, buf.data(), available, &read)) {
                error = L"读取数据失败";
                break;
            }
            if (read == 0) {
                ok = true;
                break;
            }
            DWORD wrote = 0;
            if (!WriteFile(file, buf.data(), read, &wrote, nullptr) || wrote != read) {
                error = L"写入文件失败";
                break;
            }
            written += read;
            {
                std::lock_guard<std::mutex> lock(shared->mtx);
                shared->downloaded = written;
            }
            // total <= 0 表示服务器未给 Content-Length（镜像分块传输常见），
            // 此时上报 -1，由界面显示“已下载”字节数与流动进度条
            int pct = (total > 0)
                          ? static_cast<int>(
                                std::min<long long>(100, written * 100 / total))
                          : -1;
            DWORD now = GetTickCount();
            if (pct != lastPct || now - lastPost >= 150) {
                lastPct = pct;
                lastPost = now;
                if (pct >= 0) {
                    std::lock_guard<std::mutex> lock(shared->mtx);
                    shared->progress = pct;
                }
                HWND hwnd = shared->hwnd.load();
                if (hwnd) {
                    PostMessageW(hwnd, WM_UPDATE_PROGRESS,
                                 static_cast<WPARAM>(pct < 0 ? -1 : pct), 0);
                }
            }
        }

        if (ok && total > 0 && written < static_cast<long long>(total)) {
            ok = false;
            error = L"下载不完整";
        }
    } while (false);

    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
        file = INVALID_HANDLE_VALUE;
    }
    if (ok) {
        std::lock_guard<std::mutex> lock(shared->mtx);
        shared->dlPartialPath.clear();
    } else {
        // 取消或失败：删除未下载完成的文件
        RemoveFileBestEffort(path);
        std::lock_guard<std::mutex> lock(shared->mtx);
        shared->dlPartialPath.clear();
    }

    {
        std::lock_guard<std::mutex> lock(shared->mtx);
        shared->dlOk = ok;
        shared->dlCancelled = cancelled;
        shared->dlPath = ok ? path : std::wstring();
        shared->dlError = ok ? std::wstring() : error;
    }
    HWND hwnd = shared->hwnd.load();
    if (hwnd) PostMessageW(hwnd, WM_UPDATE_DL_DONE, 0, 0);
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
static int AssetListTop(UpdateState* st) { return st->S(kHeaderH + 52); }

static RECT AssetRowRect(UpdateState* st, int w, int index) {
    int top = AssetListTop(st) +
              index * st->S(kAssetRowH + kAssetGap) - st->assetScroll;
    return RECT{st->S(kPad), top, w - st->S(kPad), top + st->S(kAssetRowH)};
}

static RECT AssetDownloadBtnRect(UpdateState* st, const RECT& row) {
    RECT rc{};
    rc.right = row.right - st->S(12);
    rc.left = rc.right - st->S(66);
    int h = st->S(24);
    rc.top = (row.top + row.bottom - h) / 2;
    rc.bottom = rc.top + h;
    return rc;
}

static int AssetMaxScroll(UpdateState* st, int h) {
    if (!st->info) return 0;
    int count = static_cast<int>(st->info->assets.size());
    int contentH = count * st->S(kAssetRowH + kAssetGap) - st->S(kAssetGap);
    int viewH = (h - st->S(kPad) - st->S(kBtnH) - st->S(12)) - AssetListTop(st);
    return std::max(0, contentH - viewH);
}

static int DetailMaxScroll(UpdateState* st, int h);

static void ClampScroll(UpdateState* st, int w, int h) {
    if (st->stage == UpdateStage::HasUpdate) {
        st->detailScroll = std::max(0, std::min(st->detailScroll,
                                                DetailMaxScroll(st, h)));
    } else if (st->stage == UpdateStage::Assets) {
        st->assetScroll = std::max(0, std::min(st->assetScroll,
                                               AssetMaxScroll(st, h)));
    }
    (void)w;
}

static void AddButton(UpdateState* st, int id, RECT rc, const wchar_t* text,
                      bool primary, bool small = false) {
    UiButton b;
    b.id = id;
    b.rc = rc;
    b.text = text;
    b.primary = primary;
    b.small = small;
    st->buttons.push_back(b);
}

static void AddToggle(UpdateState* st, int id, RECT rc, const wchar_t* text,
                      bool selected) {
    UiButton b;
    b.id = id;
    b.rc = rc;
    b.text = text;
    b.toggle = true;
    b.selected = selected;
    st->buttons.push_back(b);
}

static void BuildButtons(UpdateState* st, int w, int h) {
    st->buttons.clear();
    int pad = st->S(kPad);
    int btnH = st->S(kBtnH);

    // 右上角关闭
    RECT closeRc{w - pad - st->S(26), st->S(8), w - pad, st->S(8) + st->S(26)};
    AddButton(st, BtnClose, closeRc, L"", false);

    int bottomY = h - pad - btnH;
    auto rightBtn = [&](int id, int widthPx, const wchar_t* text, bool primary,
                        int& cursor) {
        int bw = st->S(widthPx);
        RECT rc{cursor - bw, bottomY, cursor, bottomY + btnH};
        AddButton(st, id, rc, text, primary);
        cursor = rc.left - st->S(10);
    };

    int cursor = w - pad;
    switch (st->stage) {
        case UpdateStage::UpToDate:
        case UpdateStage::Done:
        case UpdateStage::DownloadFailed:
            if (st->stage == UpdateStage::Done) {
                rightBtn(BtnClose, 88, L"关闭", false, cursor);
                rightBtn(BtnOpenFolder, 116, L"打开文件夹", true, cursor);
            } else if (st->stage == UpdateStage::DownloadFailed) {
                rightBtn(BtnClose, 88, L"关闭", false, cursor);
                rightBtn(BtnManual, 112, L"手动下载", true, cursor);
                if (MirrorAvailable()) {
                    if (st->lastUsedMirror) {
                        rightBtn(BtnRetryDirect, 112, L"直连重试", false, cursor);
                    } else {
                        rightBtn(BtnRetryMirror, 112, L"镜像重试", false, cursor);
                    }
                }
            } else {
                rightBtn(BtnClose, 88, L"关闭", true, cursor);
            }
            break;
        case UpdateStage::HasUpdate:
            rightBtn(BtnYes, 104, L"是，更新", true, cursor);
            rightBtn(BtnNo, 80, L"否", false, cursor);
            break;
        case UpdateStage::Assets:
            if (MirrorAvailable()) {
                // 标题右侧的线路切换：直连 / 镜像
                int top = st->S(kHeaderH + 6);
                int chipH = st->S(24);
                int right = w - st->S(kPad);
                int mirrorW = st->S(60);
                int directW = st->S(56);
                RECT mirrorRc{right - mirrorW, top, right, top + chipH};
                RECT directRc{mirrorRc.left - st->S(6) - directW, top,
                              mirrorRc.left - st->S(6), top + chipH};
                AddToggle(st, BtnLineDirect, directRc, L"直连", !st->useMirror);
                AddToggle(st, BtnLineMirror, mirrorRc, L"镜像", st->useMirror);
            }
            rightBtn(BtnManual, 104, L"手动下载", false, cursor);
            rightBtn(BtnBack, 80, L"返回", false, cursor);
            if (st->info) {
                int count = static_cast<int>(st->info->assets.size());
                for (int i = 0; i < count; ++i) {
                    RECT row = AssetRowRect(st, w, i);
                    RECT bar = AssetDownloadBtnRect(st, row);
                    // 超出可视区域的按钮不下发，避免点击到不可见区域
                    if (row.bottom < AssetListTop(st) || row.top > bottomY) continue;
                    AddButton(st, BtnAssetBase + i, bar, L"下载", true, true);
                }
            }
            break;
        case UpdateStage::Downloading:
            rightBtn(BtnCancelDownload, 96, L"取消", false, cursor);
            break;
        case UpdateStage::Failed:
            rightBtn(BtnClose, 88, L"关闭", false, cursor);
            rightBtn(BtnRetry, 96, L"重新获取", false, cursor);
            rightBtn(BtnOpenRelease, 116, L"打开发布页", true, cursor);
            break;
        case UpdateStage::Checking:
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 内容绘制
// ---------------------------------------------------------------------------
static RECT DetailViewRect(UpdateState* st, int w, int h) {
    int top = st->S(kHeaderH + 66);
    int bottom = h - st->S(kPad) - st->S(kBtnH) - st->S(12);
    if (bottom < top + st->S(40)) bottom = top + st->S(40);
    return RECT{st->S(kPad), top, w - st->S(kPad), bottom};
}

static int MeasureTextHeight(UpdateState* st, HDC dc, const std::wstring& text,
                             HFONT font, int width) {
    RECT rc{0, 0, width, 0};
    HFONT old = static_cast<HFONT>(SelectObject(dc, font));
    DrawTextW(dc, text.c_str(), static_cast<int>(text.size()), &rc,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    return rc.bottom;
}

static int DetailMaxScroll(UpdateState* st, int h) {
    if (!st->info) return 0;
    (void)h;
    // 内容高度在绘制时测量并缓存，滚动范围据此计算
    return std::max(0, st->detailContentH - st->detailViewH);
}

static void DrawScrollThumb(HDC dc, UpdateState* st, const RECT& view,
                            int contentH) {
    if (contentH <= 0) return;
    int viewH = view.bottom - view.top;
    if (viewH <= 0 || contentH <= viewH) return;
    int trackH = viewH;
    int thumbH = std::max(st->S(24), trackH * viewH / contentH);
    int maxScroll = contentH - viewH;
    int offset = maxScroll > 0 ? (trackH - thumbH) * st->detailScroll / maxScroll : 0;
    RECT thumb{view.right + st->S(2), view.top + offset,
               view.right + st->S(6), view.top + offset + thumbH};
    FillRound(dc, thumb, st->S(2), RGB(74, 74, 92), RGB(74, 74, 92));
}

static void DrawSpinner(HDC dc, int cx, int cy, int radius, int phase,
                        int dotRadius) {
    static const double kPi = 3.14159265358979323846;
    for (int i = 0; i < 8; ++i) {
        double ang = i * kPi / 4.0;
        int x = cx + static_cast<int>(radius * std::cos(ang));
        int y = cy + static_cast<int>(radius * std::sin(ang));
        int step = (i - phase + 8) % 8;
        int lum = 48 + step * 24;
        if (lum > 220) lum = 220;
        HBRUSH br = CreateSolidBrush(RGB(lum / 2, lum * 3 / 4, lum));
        HGDIOBJ oldBrush = SelectObject(dc, br);
        HGDIOBJ oldPen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x - dotRadius, y - dotRadius, x + dotRadius, y + dotRadius);
        SelectObject(dc, oldPen);
        SelectObject(dc, oldBrush);
        DeleteObject(br);
    }
}

static void DrawButton(HDC dc, UpdateState* st, const UiButton& b) {
    bool hover = (st->hoverBtn == b.id);
    bool pressed = (st->pressedBtn == b.id);

    if (b.id == BtnClose) {
        if (hover) {
            FillRound(dc, b.rc, st->S(6), RGB(44, 44, 56), RGB(44, 44, 56));
        }
        int cx = (b.rc.left + b.rc.right) / 2;
        int cy = (b.rc.top + b.rc.bottom) / 2;
        int arm = st->S(5);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, st->S(1)),
                             hover ? kValueText : kMuted);
        HGDIOBJ oldPen = SelectObject(dc, pen);
        MoveToEx(dc, cx - arm, cy - arm, nullptr);
        LineTo(dc, cx + arm + 1, cy + arm + 1);
        MoveToEx(dc, cx + arm, cy - arm, nullptr);
        LineTo(dc, cx - arm - 1, cy + arm + 1);
        SelectObject(dc, oldPen);
        DeleteObject(pen);
        return;
    }

    int radius = std::min(st->S(8), static_cast<int>(b.rc.bottom - b.rc.top) / 2);
    if (b.toggle) {
        COLORREF fill = b.selected
                            ? (pressed ? RGB(48, 60, 110) : kOnBg)
                            : (pressed ? RGB(46, 46, 58)
                                       : (hover ? RGB(42, 42, 54) : kChipBg));
        COLORREF border = b.selected
                              ? kOnBorder
                              : (hover ? kAccent : kChipBorder);
        FillRound(dc, b.rc, radius, fill, border);
        DrawCentered(dc, b.rc, b.text, st->smallFont,
                     b.selected ? kValueText : kLabel);
        return;
    }
    if (b.primary) {
        COLORREF fill = pressed ? RGB(48, 74, 196) : (hover ? kAccentHi : kAccent);
        FillRound(dc, b.rc, radius, fill, kAccentHi);
        DrawCentered(dc, b.rc, b.text, b.small ? st->font : st->boldFont,
                     RGB(255, 255, 255));
    } else {
        COLORREF fill = pressed ? RGB(46, 46, 58) : (hover ? RGB(42, 42, 54) : kChipBg);
        FillRound(dc, b.rc, radius, fill, hover ? kAccent : kChipBorder);
        DrawCentered(dc, b.rc, b.text, st->font, kLabel);
    }
}

static void DrawChecking(UpdateState* st, HDC dc, int w, int h) {
    int cx = w / 2;
    int cy = h / 2 - st->S(24);
    DrawSpinner(dc, cx, cy, st->S(18), st->spinPhase, st->S(3));

    RECT rc{st->S(kPad), cy + st->S(34), w - st->S(kPad), cy + st->S(62)};
    DrawTextIn(dc, L"正在检查更新…", rc, st->font, kLabel,
               DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

static void DrawHasUpdate(UpdateState* st, HDC dc, int w, int h) {
    if (!st->info) return;
    std::wstring title = L"发现新版本 " + st->info->tag;
    RECT titleRc{st->S(kPad), st->S(kHeaderH + 8), w - st->S(kPad),
                 st->S(kHeaderH + 38)};
    DrawTextIn(dc, title, titleRc, st->titleFont, kValueText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT curRc{st->S(kPad), st->S(kHeaderH + 38), w - st->S(kPad),
               st->S(kHeaderH + 56)};
    DrawTextIn(dc, std::wstring(L"当前版本 ") + APP_VERSION_STRING, curRc,
               st->smallFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, st->S(kPad), st->S(kHeaderH + 60), nullptr);
    LineTo(dc, w - st->S(kPad), st->S(kHeaderH + 60));
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    RECT view = DetailViewRect(st, w, h);
    std::wstring body = st->info->body;
    if (body.empty()) body = L"该版本未提供更新说明。";
    body = L"更新内容\n\n" + body;

    int contentH = MeasureTextHeight(st, dc, body, st->font,
                                     view.right - view.left - st->S(12));
    int viewH = view.bottom - view.top;
    st->detailContentH = contentH;
    st->detailViewH = viewH;
    int maxScroll = std::max(0, contentH - viewH);
    if (st->detailScroll > maxScroll) st->detailScroll = maxScroll;

    int saved = SaveDC(dc);
    IntersectClipRect(dc, view.left, view.top, view.right, view.bottom);
    RECT bodyRc{view.left, view.top - st->detailScroll, view.right - st->S(10),
                view.top - st->detailScroll + contentH + st->S(4)};
    DrawTextIn(dc, body, bodyRc, st->font, kLabel, DT_LEFT | DT_WORDBREAK);
    RestoreDC(dc, saved);

    if (maxScroll > 0) {
        RECT viewForThumb = view;
        viewForThumb.right -= st->S(8);
        DrawScrollThumb(dc, st, viewForThumb, contentH);
    }
}

static void DrawAssets(UpdateState* st, HDC dc, int w, int h) {
    RECT headRc{st->S(kPad), st->S(kHeaderH + 8), w - st->S(kPad),
                st->S(kHeaderH + 34)};
    DrawTextIn(dc, L"选择要下载的附件", headRc, st->boldFont, kValueText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT hintRc{st->S(kPad), st->S(kHeaderH + 32), w - st->S(kPad),
                st->S(kHeaderH + 50)};
    std::wstring hint = L"下载后将保存到系统的「下载」文件夹";
    if (MirrorAvailable() && st->useMirror) {
        hint = std::wstring(L"当前线路：镜像 ") + APP_MIRROR_PREFIX;
    } else if (MirrorAvailable()) {
        hint += L" · 当前线路：直连 GitHub";
    }
    DrawTextIn(dc, hint, hintRc, st->smallFont, kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int listTop = AssetListTop(st);
    int listBottom = h - st->S(kPad) - st->S(kBtnH) - st->S(12);

    if (!st->info || st->info->assets.empty()) {
        RECT rc{st->S(kPad), listTop, w - st->S(kPad), listTop + st->S(60)};
        DrawTextIn(dc, L"该版本未提供附件文件，请使用「手动下载」前往发布页。", rc,
                   st->font, kMuted, DT_LEFT | DT_WORDBREAK);
        return;
    }

    int saved = SaveDC(dc);
    IntersectClipRect(dc, 0, listTop - st->S(4), w, listBottom);
    for (size_t i = 0; i < st->info->assets.size(); ++i) {
        RECT row = AssetRowRect(st, w, static_cast<int>(i));
        if (row.bottom < listTop - st->S(8) || row.top > listBottom) continue;
        FillRound(dc, row, st->S(8), kChipBg, kChipBorder);

        RECT nameRc{row.left + st->S(12), row.top + st->S(8),
                    row.right - st->S(90), row.top + st->S(30)};
        DrawTextIn(dc, st->info->assets[i].name, nameRc, st->boldFont, kValueText,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

        RECT sizeRc{row.left + st->S(12), row.top + st->S(28),
                    row.right - st->S(90), row.top + st->S(46)};
        std::wstring size = FormatBytes(st->info->assets[i].size);
        DrawTextIn(dc, size.empty() ? L"未知大小" : size, sizeRc, st->smallFont,
                   kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    }
    RestoreDC(dc, saved);
}

static void DrawDownloading(UpdateState* st, HDC dc, int w, int h) {
    RECT titleRc{st->S(kPad), st->S(kHeaderH + 16), w - st->S(kPad),
                 st->S(kHeaderH + 46)};
    DrawTextIn(dc, L"正在下载更新文件", titleRc, st->titleFont, kValueText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT nameRc{st->S(kPad), st->S(kHeaderH + 46), w - st->S(kPad),
                st->S(kHeaderH + 68)};
    DrawTextIn(dc, st->downloadName, nameRc, st->font, kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    RECT lineRc{st->S(kPad), st->S(kHeaderH + 66), w - st->S(kPad),
                st->S(kHeaderH + 86)};
    std::wstring line = st->lastUsedMirror
                            ? (std::wstring(L"线路：镜像 ") + APP_MIRROR_PREFIX)
                            : std::wstring(L"线路：直连 GitHub");
    DrawTextIn(dc, line, lineRc, st->smallFont, kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    int barY = st->S(kHeaderH + 100);
    int barH = st->S(10);
    RECT track{st->S(kPad), barY, w - st->S(kPad), barY + barH};
    FillRound(dc, track, barH / 2, kChipBg, kChipBorder);
    int pct = std::max(0, std::min(100, st->progress));
    if (st->indeterminate) {
        // 镜像常见分块传输，无 Content-Length：用流动色块表示仍在下载
        int trackW = track.right - track.left;
        int blockW = std::max(st->S(70), trackW / 5);
        int span = trackW + blockW;
        int pos = (st->spinPhase * span / 24) % span - blockW;
        int left = track.left + std::max(0, pos);
        int right = track.left + std::min(trackW, pos + blockW);
        if (right > left) {
            RECT fill{left, track.top, right, track.bottom};
            FillRound(dc, fill, barH / 2, kAccent, kAccent);
        }
    } else {
        int fillW = (track.right - track.left) * pct / 100;
        if (fillW > 0) {
            RECT fill{track.left, track.top, track.left + fillW, track.bottom};
            FillRound(dc, fill, barH / 2, kAccent, kAccent);
        }
    }

    std::wstring progressText;
    if (st->indeterminate) {
        long long downloaded = 0;
        {
            std::lock_guard<std::mutex> lock(st->shared->mtx);
            downloaded = st->shared->downloaded;
        }
        progressText = L"已下载 ";
        std::wstring size = FormatBytes(downloaded);
        progressText += size.empty() ? L"0 B" : size;
    } else {
        progressText = std::to_wstring(pct) + L"%";
    }
    RECT pctRc{st->S(kPad), barY + st->S(20), w - st->S(kPad),
               barY + st->S(44)};
    DrawTextIn(dc, progressText, pctRc, st->smallFont, kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT tipRc{st->S(kPad), barY + st->S(48), w - st->S(kPad),
               barY + st->S(72)};
    DrawTextIn(dc, L"下载过程中请勿关闭窗口，可随时点击「取消」。", tipRc,
               st->smallFont, kMuted, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

static void DrawDownloadFailed(UpdateState* st, HDC dc, int w, int h) {
    RECT titleRc{st->S(kPad), st->S(kHeaderH + 16), w - st->S(kPad),
                 st->S(kHeaderH + 46)};
    DrawTextIn(dc, L"下载失败", titleRc, st->titleFont, kDanger,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT errRc{st->S(kPad), st->S(kHeaderH + 52), w - st->S(kPad),
               st->S(kHeaderH + 142)};
    DrawTextIn(dc, st->errorText, errRc, st->font, kLabel,
               DT_LEFT | DT_WORDBREAK);

    RECT tipRc{st->S(kPad), st->S(kHeaderH + 148), w - st->S(kPad),
               st->S(kHeaderH + 176)};
    DrawTextIn(dc, L"也可以使用「手动下载」，在浏览器中直接获取文件。", tipRc,
               st->smallFont, kMuted, DT_LEFT | DT_WORDBREAK);
}

static void DrawDone(UpdateState* st, HDC dc, int w, int h) {
    RECT titleRc{st->S(kPad), st->S(kHeaderH + 16), w - st->S(kPad),
                 st->S(kHeaderH + 46)};
    DrawTextIn(dc, L"下载完成", titleRc, st->titleFont, kValueText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT tipRc{st->S(kPad), st->S(kHeaderH + 52), w - st->S(kPad),
               st->S(kHeaderH + 74)};
    DrawTextIn(dc, L"文件已保存到下载文件夹：", tipRc, st->font, kMuted,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT pathRc{st->S(kPad), st->S(kHeaderH + 78), w - st->S(kPad),
                st->S(kHeaderH + 150)};
    DrawTextIn(dc, st->savedPath, pathRc, st->font, kLabel, DT_LEFT | DT_WORDBREAK);

    if (st->lastUsedMirror) {
        RECT lineRc{st->S(kPad), st->S(kHeaderH + 156), w - st->S(kPad),
                    st->S(kHeaderH + 180)};
        DrawTextIn(dc, std::wstring(L"已通过镜像下载：") + APP_MIRROR_PREFIX,
                   lineRc, st->smallFont, kMuted,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
}

static void DrawFailed(UpdateState* st, HDC dc, int w, int h) {
    RECT titleRc{st->S(kPad), st->S(kHeaderH + 16), w - st->S(kPad),
                 st->S(kHeaderH + 46)};
    DrawTextIn(dc, L"获取更新失败", titleRc, st->titleFont, kDanger,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    RECT errRc{st->S(kPad), st->S(kHeaderH + 52), w - st->S(kPad),
               st->S(kHeaderH + 118)};
    DrawTextIn(dc, st->errorText, errRc, st->font, kLabel,
               DT_LEFT | DT_WORDBREAK);

    RECT tipRc{st->S(kPad), st->S(kHeaderH + 126), w - st->S(kPad),
               st->S(kHeaderH + 172)};
    DrawTextIn(dc, L"已尝试为你打开项目发布页，可在浏览器中查看最新版本。", tipRc,
               st->font, kMuted, DT_LEFT | DT_WORDBREAK);
}

static void PaintWindow(UpdateState* st, HDC dc, int w, int h) {
    RECT full{0, 0, w, h};
    FillRect(dc, &full, st->brBg);
    SetBkMode(dc, TRANSPARENT);

    // 标题
    const wchar_t* title = L"检查更新";
    switch (st->stage) {
        case UpdateStage::UpToDate: title = L"检查更新"; break;
        case UpdateStage::HasUpdate:
        case UpdateStage::Assets: title = L"发现新版本"; break;
        case UpdateStage::Downloading: title = L"下载更新"; break;
        case UpdateStage::DownloadFailed: title = L"下载更新"; break;
        case UpdateStage::Done: title = L"下载更新"; break;
        case UpdateStage::Failed: title = L"检查更新"; break;
        default: title = L"检查更新"; break;
    }
    RECT titleRc{st->S(kPad), 0, w - st->S(kPad) - st->S(36), st->S(kHeaderH)};
    DrawTextIn(dc, title, titleRc, st->titleFont, kValueText,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ oldPen = SelectObject(dc, pen);
    MoveToEx(dc, st->S(kPad), st->S(kHeaderH) - 1, nullptr);
    LineTo(dc, w - st->S(kPad), st->S(kHeaderH) - 1);
    SelectObject(dc, oldPen);
    DeleteObject(pen);

    BuildButtons(st, w, h);

    switch (st->stage) {
        case UpdateStage::Checking:       DrawChecking(st, dc, w, h); break;
        case UpdateStage::UpToDate: {
            RECT rc{st->S(kPad), st->S(kHeaderH + 20), w - st->S(kPad),
                    st->S(kHeaderH + 52)};
            DrawTextIn(dc, L"当前已是最新版本", rc, st->titleFont, kValueText,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            RECT sub{st->S(kPad), st->S(kHeaderH + 56), w - st->S(kPad),
                     st->S(kHeaderH + 78)};
            std::wstring v = std::wstring(L"版本 ") + APP_VERSION_STRING;
            DrawTextIn(dc, v, sub, st->font, kMuted,
                       DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            break;
        }
        case UpdateStage::HasUpdate:      DrawHasUpdate(st, dc, w, h); break;
        case UpdateStage::Assets:         DrawAssets(st, dc, w, h); break;
        case UpdateStage::Downloading:    DrawDownloading(st, dc, w, h); break;
        case UpdateStage::DownloadFailed: DrawDownloadFailed(st, dc, w, h); break;
        case UpdateStage::Done:           DrawDone(st, dc, w, h); break;
        case UpdateStage::Failed:         DrawFailed(st, dc, w, h); break;
    }

    for (const UiButton& b : st->buttons) DrawButton(dc, st, b);
}

// ---------------------------------------------------------------------------
// 行为
// ---------------------------------------------------------------------------
static void ResetShared(UpdateState* st) {
    std::lock_guard<std::mutex> lock(st->shared->mtx);
    st->shared->checkOk = false;
    st->shared->info.reset();
    st->shared->checkError.clear();
    st->shared->dlOk = false;
    st->shared->dlCancelled = false;
    st->shared->dlPath.clear();
    st->shared->dlPartialPath.clear();
    st->shared->dlError.clear();
    st->shared->progress = 0;
    st->shared->downloaded = 0;
}

static void StartCheck(UpdateState* st) {
    st->stage = UpdateStage::Checking;
    st->info.reset();
    st->errorText.clear();
    st->progress = 0;
    st->detailScroll = 0;
    st->assetScroll = 0;
    ResetShared(st);
    st->shared->cancel.store(false);
    SetTimer(st->hwnd, 1, 60, nullptr);

    std::shared_ptr<SharedState> shared = st->shared;
    std::thread([shared]() { DoCheckUpdates(shared); }).detach();

    InvalidateRect(st->hwnd, nullptr, TRUE);
}

static void StartDownload(UpdateState* st, int index) {
    if (!st->info || index < 0 ||
        index >= static_cast<int>(st->info->assets.size())) {
        return;
    }
    const ReleaseAsset& asset = st->info->assets[index];
    std::wstring name = SanitizeFileName(asset.name);
    std::wstring path = MakeUniquePath(DownloadsDir(), name);

    bool useMirror = st->useMirror && MirrorAvailable();
    std::wstring url = useMirror ? MirrorUrl(asset.url) : asset.url;

    st->downloadName = asset.name;
    st->progress = 0;
    st->indeterminate = false;
    st->lastAssetIndex = index;
    st->lastUsedMirror = useMirror;
    st->stage = UpdateStage::Downloading;
    st->shared->cancel.store(false);
    {
        std::lock_guard<std::mutex> lock(st->shared->mtx);
        st->shared->progress = 0;
        st->shared->downloaded = 0;
    }
    // 下载期间用定时器驱动无 Content-Length 时的流动进度条
    SetTimer(st->hwnd, 1, 60, nullptr);

    std::shared_ptr<SharedState> shared = st->shared;
    std::thread([shared, url, path]() {
        DoDownloadAsset(shared, url, path);
    }).detach();

    InvalidateRect(st->hwnd, nullptr, TRUE);
}

static void DoButtonAction(UpdateState* st, int id) {
    if (id == BtnNone) return;
    if (id >= BtnAssetBase) {
        StartDownload(st, id - BtnAssetBase);
        return;
    }
    switch (id) {
        case BtnClose:
            DestroyWindow(st->hwnd);
            break;
        case BtnYes:
            st->stage = UpdateStage::Assets;
            st->assetScroll = 0;
            InvalidateRect(st->hwnd, nullptr, TRUE);
            break;
        case BtnNo:
            DestroyWindow(st->hwnd);
            break;
        case BtnBack:
            st->stage = UpdateStage::HasUpdate;
            InvalidateRect(st->hwnd, nullptr, TRUE);
            break;
        case BtnManual:
            OpenUrl(st->info ? st->info->htmlUrl : ReleasePageUrl());
            break;
        case BtnCancelDownload:
            st->shared->cancel.store(true);
            break;
        case BtnLineDirect:
            if (st->useMirror) {
                st->useMirror = false;
                InvalidateRect(st->hwnd, nullptr, FALSE);
            }
            break;
        case BtnLineMirror:
            if (!st->useMirror) {
                st->useMirror = true;
                InvalidateRect(st->hwnd, nullptr, FALSE);
            }
            break;
        case BtnRetryMirror:
            if (st->lastAssetIndex >= 0 && MirrorAvailable()) {
                st->useMirror = true;
                StartDownload(st, st->lastAssetIndex);
            }
            break;
        case BtnRetryDirect:
            if (st->lastAssetIndex >= 0) {
                st->useMirror = false;
                StartDownload(st, st->lastAssetIndex);
            }
            break;
        case BtnRetry:
            StartCheck(st);
            break;
        case BtnOpenFolder:
            RevealInExplorer(st->savedPath);
            break;
        case BtnOpenRelease:
            OpenUrl(ReleasePageUrl());
            break;
        default:
            break;
    }
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK UpdateWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    UpdateState* st = reinterpret_cast<UpdateState*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));

    switch (msg) {
        case WM_NCCREATE: {
            CREATESTRUCTW* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            UpdateState* state = static_cast<UpdateState*>(cs->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }
        case WM_CREATE: {
            if (!st) return -1;
            st->hwnd = hwnd;
            st->dpi = static_cast<UINT>(utils::GetWindowDpi(hwnd));
            st->brBg = CreateSolidBrush(kBg);

            std::wstring face = utils::GetSystemDefaultFontName();
            st->font = CreateFontW(-st->S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                   FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                   CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                   DEFAULT_PITCH | FF_DONTCARE, face.c_str());
            st->boldFont = CreateFontW(-st->S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE,
                                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       DEFAULT_PITCH | FF_DONTCARE, face.c_str());
            st->smallFont = CreateFontW(-st->S(11), 0, 0, 0, FW_NORMAL, FALSE,
                                        FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, face.c_str());
            st->titleFont = CreateFontW(-st->S(16), 0, 0, 0, FW_BOLD, FALSE,
                                        FALSE, FALSE, DEFAULT_CHARSET,
                                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                        CLEARTYPE_QUALITY,
                                        DEFAULT_PITCH | FF_DONTCARE, face.c_str());

            st->shared->hwnd.store(hwnd);
            StartCheck(st);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            if (!dc || !st) {
                if (dc) EndPaint(hwnd, &ps);
                return 0;
            }
            RECT cr{};
            GetClientRect(hwnd, &cr);
            int w = cr.right;
            int h = cr.bottom;
            if (w <= 0 || h <= 0) {
                EndPaint(hwnd, &ps);
                return 0;
            }
            if (!st->memDC) st->memDC = CreateCompatibleDC(dc);
            if (st->memDC &&
                (!st->memBmp || st->memW != w || st->memH != h)) {
                if (st->memBmp) {
                    SelectObject(st->memDC, st->memOld);
                    DeleteObject(st->memBmp);
                    st->memBmp = nullptr;
                }
                st->memBmp = CreateCompatibleBitmap(dc, w, h);
                if (st->memBmp) {
                    st->memOld = SelectObject(st->memDC, st->memBmp);
                    st->memW = w;
                    st->memH = h;
                }
            }
            HDC target = (st->memDC && st->memBmp) ? st->memDC : dc;
            ClampScroll(st, w, h);
            PaintWindow(st, target, w, h);
            if (target != dc) BitBlt(dc, 0, 0, w, h, target, 0, 0, SRCCOPY);
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER:
            if (st && wp == 1) {
                if (st->stage == UpdateStage::Checking) {
                    st->spinPhase = (st->spinPhase + 1) % 8;
                    InvalidateRect(hwnd, nullptr, FALSE);
                } else if (st->stage == UpdateStage::Downloading) {
                    // 有 Content-Length 时不需要动画，但仍保留定时器，
                    // 以便后续切换到无长度（流动条）模式时立即生效
                    if (st->indeterminate) {
                        st->spinPhase = (st->spinPhase + 1) % 24;
                        InvalidateRect(hwnd, nullptr, FALSE);
                    }
                } else {
                    KillTimer(hwnd, 1);
                }
            }
            return 0;
        case WM_NCHITTEST: {
            LRESULT hit = DefWindowProcW(hwnd, msg, wp, lp);
            if (!st || hit != HTCLIENT) return hit;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y < st->S(kHeaderH)) {
                for (const UiButton& b : st->buttons) {
                    if (b.id == BtnClose && PtInRect(&b.rc, pt)) return HTCLIENT;
                }
                return HTCAPTION;
            }
            return HTCLIENT;
        }
        case WM_MOUSEMOVE: {
            if (!st) return 0;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hover = BtnNone;
            for (const UiButton& b : st->buttons) {
                if (PtInRect(&b.rc, pt)) {
                    hover = b.id;
                    break;
                }
            }
            if (hover != st->hoverBtn) {
                st->hoverBtn = hover;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (!st->trackingLeave) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                st->trackingLeave = true;
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            if (st) {
                st->trackingLeave = false;
                if (st->hoverBtn != BtnNone) {
                    st->hoverBtn = BtnNone;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        case WM_SETCURSOR: {
            if (st && LOWORD(lp) == HTCLIENT) {
                POINT pt{};
                GetCursorPos(&pt);
                ScreenToClient(hwnd, &pt);
                for (const UiButton& b : st->buttons) {
                    if (PtInRect(&b.rc, pt)) {
                        SetCursor(LoadCursor(nullptr, IDC_HAND));
                        return TRUE;
                    }
                }
            }
            break;
        }
        case WM_LBUTTONDOWN: {
            if (!st) return 0;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hit = BtnNone;
            for (const UiButton& b : st->buttons) {
                if (PtInRect(&b.rc, pt)) {
                    hit = b.id;
                    break;
                }
            }
            if (hit != BtnNone) {
                st->pressedBtn = hit;
                SetCapture(hwnd);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_LBUTTONUP: {
            if (!st) return 0;
            if (st->pressedBtn == BtnNone) return 0;
            int pressed = st->pressedBtn;
            st->pressedBtn = BtnNone;
            if (GetCapture() == hwnd) ReleaseCapture();
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT cr{};
            GetClientRect(hwnd, &cr);
            bool inside = PtInRect(&cr, pt) != FALSE;
            if (inside) {
                for (const UiButton& b : st->buttons) {
                    if (b.id == pressed && PtInRect(&b.rc, pt)) {
                        InvalidateRect(hwnd, nullptr, FALSE);
                        DoButtonAction(st, pressed);
                        return 0;
                    }
                }
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            if (!st) return 0;
            int delta = GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA;
            RECT cr{};
            GetClientRect(hwnd, &cr);
            if (st->stage == UpdateStage::HasUpdate) {
                st->detailScroll -= delta * st->S(48);
                ClampScroll(st, cr.right, cr.bottom);
                InvalidateRect(hwnd, nullptr, FALSE);
            } else if (st->stage == UpdateStage::Assets) {
                st->assetScroll -= delta * st->S(48);
                ClampScroll(st, cr.right, cr.bottom);
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            return 0;
        }
        case WM_UPDATE_CHECK_DONE: {
            if (!st) return 0;
            bool ok = false;
            std::shared_ptr<ReleaseInfo> info;
            std::wstring error;
            {
                std::lock_guard<std::mutex> lock(st->shared->mtx);
                ok = st->shared->checkOk;
                info = st->shared->info;
                error = st->shared->checkError;
            }
            KillTimer(hwnd, 1);
            if (!ok) {
                st->errorText = error.empty() ? L"网络请求失败" : error;
                st->stage = UpdateStage::Failed;
                // 需求：获取失败跳转到项目发布页
                OpenUrl(ReleasePageUrl());
            } else if (info &&
                       CompareVersion(info->version, APP_VERSION_STRING) > 0) {
                st->info = info;
                st->stage = UpdateStage::HasUpdate;
                st->detailScroll = 0;
            } else {
                st->info = info;
                st->stage = UpdateStage::UpToDate;
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_UPDATE_PROGRESS: {
            if (st) {
                if (wp == static_cast<WPARAM>(-1)) {
                    st->indeterminate = true;
                } else {
                    st->indeterminate = false;
                    st->progress = static_cast<int>(wp);
                }
                if (st->stage == UpdateStage::Downloading) {
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_UPDATE_DL_DONE: {
            if (!st) return 0;
            bool ok = false;
            bool cancelled = false;
            std::wstring path;
            std::wstring partial;
            std::wstring error;
            {
                std::lock_guard<std::mutex> lock(st->shared->mtx);
                ok = st->shared->dlOk;
                cancelled = st->shared->dlCancelled;
                path = st->shared->dlPath;
                partial = st->shared->dlPartialPath;
                st->shared->dlPartialPath.clear();
                error = st->shared->dlError;
            }
            // worker 未能删除时（偶发占用）在这里再兜底删除一次
            if (!ok && !partial.empty()) RemoveFileBestEffort(partial);
            KillTimer(hwnd, 1);
            if (cancelled) {
                st->stage = UpdateStage::Assets;
            } else if (ok) {
                st->savedPath = path;
                st->progress = 100;
                st->indeterminate = false;
                st->stage = UpdateStage::Done;
            } else {
                st->errorText = error.empty() ? L"下载失败" : error;
                if (MirrorAvailable() && !st->lastUsedMirror) {
                    st->errorText += L"\n可点击「镜像重试」改用镜像线路。";
                } else if (MirrorAvailable() && st->lastUsedMirror) {
                    st->errorText += L"\n可切换回「直连」线路再次尝试。";
                }
                st->stage = UpdateStage::DownloadFailed;
            }
            InvalidateRect(hwnd, nullptr, TRUE);
            return 0;
        }
        case WM_KEYDOWN:
            if (wp == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY: {
            KillTimer(hwnd, 1);
            if (st) {
                st->running = false;
                st->shared->hwnd.store(nullptr);
                st->shared->cancel.store(true);
                if (st->memDC) {
                    if (st->memBmp) SelectObject(st->memDC, st->memOld);
                    DeleteDC(st->memDC);
                }
                if (st->memBmp) DeleteObject(st->memBmp);
                st->memDC = nullptr;
                st->memBmp = nullptr;
                if (st->font) DeleteObject(st->font);
                if (st->boldFont) DeleteObject(st->boldFont);
                if (st->smallFont) DeleteObject(st->smallFont);
                if (st->titleFont) DeleteObject(st->titleFont);
                if (st->brBg) DeleteObject(st->brBg);
                st->font = nullptr;
                st->boldFont = nullptr;
                st->smallFont = nullptr;
                st->titleFont = nullptr;
                st->brBg = nullptr;
                st->hwnd = nullptr;
            }
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            return 0;
        }
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static bool RegisterUpdateClass(HINSTANCE inst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(inst, kClass, &exist)) return true;
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_DROPSHADOW;
    wc.lpfnWndProc = UpdateWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

// ---------------------------------------------------------------------------
void ShowUpdateWindow(HWND owner) {
    HWND existing = FindWindowW(kClass, nullptr);
    if (existing) {
        SetForegroundWindow(existing);
        return;
    }

    HINSTANCE inst = owner
        ? reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE))
        : GetModuleHandleW(nullptr);
    if (!RegisterUpdateClass(inst)) {
        MessageBoxW(owner, L"无法创建更新窗口", L"错误", MB_ICONERROR);
        return;
    }

    UpdateState st;
    st.owner = owner;
    st.dpi = static_cast<UINT>(utils::GetWindowDpi(owner ? owner : GetDesktopWindow()));
    st.shared = std::make_shared<SharedState>();

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(owner ? owner : GetDesktopWindow(),
                                     MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int winW = std::min(st.S(kWinW), workW - st.S(16));
    int winH = std::min(st.S(kWinH), workH - st.S(16));
    int x = mi.rcWork.left + (workW - winW) / 2;
    int y = mi.rcWork.top + (workH - winH) / 2;

    st.hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass,
                              L"检查更新", WS_POPUP | WS_CLIPCHILDREN, x, y,
                              winW, winH, owner, nullptr, inst, &st);
    if (!st.hwnd) {
        MessageBoxW(owner, L"无法创建更新窗口", L"错误", MB_ICONERROR);
        return;
    }

    SetWindowRgn(st.hwnd, CreateRoundRectRgn(0, 0, winW + 1, winH + 1,
                                             st.S(16), st.S(16)), TRUE);

    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(st.hwnd, SW_SHOW);
    UpdateWindow(st.hwnd);
    SetForegroundWindow(st.hwnd);

    MSG msg;
    while (st.running) {
        if (owner && !IsWindow(owner)) break;
        BOOL got = GetMessageW(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (st.hwnd) {
        DestroyWindow(st.hwnd);
        st.hwnd = nullptr;
    }
    st.shared->cancel.store(true);
    st.shared->hwnd.store(nullptr);

    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
    }
}
