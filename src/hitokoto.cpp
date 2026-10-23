// =============================================================================
// hitokoto.cpp - 一言实现
// =============================================================================
#include "hitokoto.h"
#include "../resource.h"
#include "json.h"
#include "utils.h"

#include <winhttp.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <random>
#include <ctime>

// 渐隐/渐显时长（毫秒）
static const float kFadeOutMs = 150.0f;
static const float kFadeInMs  = 200.0f;

// 随机数生成器：以时间+启动时刻播种，避免 std::rand() 未播种时
// 每次启动随机序列固定（本地语句/来源选择总是一样）
static std::mt19937& Rng() {
    static std::mt19937 gen(
        static_cast<unsigned>(std::time(nullptr)) ^
        static_cast<unsigned>(GetTickCount()));
    return gen;
}

// ---------------------------------------------------------------------------
void HitokotoManager::Init(HWND hwnd, const Config& cfg) {
    hwnd_ = hwnd;
    cfg_ = cfg;
    LoadLocal();
    lastFetch_ = GetTickCount();
}

void HitokotoManager::ApplyConfig(const Config& cfg) {
    cfg_ = cfg;
    lastFetch_ = GetTickCount();
    FetchNext(); // 立即按新配置获取一句
}

// ---------------------------------------------------------------------------
// 动画：渐隐 -> （切换文本）-> 渐显
// ---------------------------------------------------------------------------
void HitokotoManager::StartFadeOut() {
    phase_ = FadePhase::FadingOut;
    fade_ = 255.0f;
    lastAnim_ = GetTickCount();
}

void HitokotoManager::Animate(DWORD now) {
    float dt = static_cast<float>(now - lastAnim_);
    lastAnim_ = now;

    switch (phase_) {
        case FadePhase::Steady:
            break;
        case FadePhase::FadingOut:
            fade_ -= 255.0f * dt / kFadeOutMs;
            if (fade_ <= 0.0f) {
                fade_ = 0.0f;
                if (hasPending_) {
                    text_ = pending_;
                    hasPending_ = false;
                }
                phase_ = FadePhase::FadingIn;
            }
            break;
        case FadePhase::FadingIn:
            fade_ += 255.0f * dt / kFadeInMs;
            if (fade_ >= 255.0f) {
                fade_ = 255.0f;
                phase_ = FadePhase::Steady;
            }
            break;
    }
}

// ---------------------------------------------------------------------------
// 每帧：动画推进 + 刷新周期检查
// ---------------------------------------------------------------------------
void HitokotoManager::Tick() {
    DWORD now = GetTickCount();
    Animate(now);

    if (cfg_.hitokotoRefreshSec > 0 &&
        now - lastFetch_ >= static_cast<DWORD>(cfg_.hitokotoRefreshSec) * 1000) {
        lastFetch_ = now;
        FetchNext();
    }
}

// ---------------------------------------------------------------------------
void HitokotoManager::FetchNext() {
    switch (cfg_.hitokotoSource) {
        case HitokotoSource::Local:
            TakeLocal();
            break;
        case HitokotoSource::Network:
            StartNetwork();
            break;
        case HitokotoSource::Both: {
            // 本地/网络随机；网络失败时在 OnNetworkResult 兜底本地
            if (Rng()() & 1) TakeLocal();
            else StartNetwork();
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// 本地语句
// ---------------------------------------------------------------------------
static void WriteDefaultLocalFile(const std::wstring& path) {
    const wchar_t* lines[] = {
        L"\u7eb5\u6709\u75be\u98ce\u8d77\uff0c\u4eba\u751f\u4e0d\u8a00\u5f03\u3002",           // 纵有疾风起，人生不言弃。
        L"\u4fdd\u6301\u70ed\u7231\uff0c\u5954\u8d74\u5c71\u6d77\u3002",                       // 保持热爱，奔赴山海。
        L"\u661f\u5149\u4e0d\u95ee\u8d76\u8def\u4eba\uff0c\u65f6\u5149\u4e0d\u8d1f\u6709\u5fc3\u4eba\u3002", // 星光不问赶路人，时光不负有心人。
        L"\u8def\u867d\u8fdc\uff0c\u884c\u5219\u5c06\u81f3\uff1b\u4e8b\u96be\uff0c\u505a\u5219\u5fc5\u6210\u3002", // 路虽远，行则将至；事虽难，做则必成。
        L"\u957f\u98ce\u7834\u6d6a\u4f1a\u6709\u65f6\uff0c\u76f4\u6302\u4e91\u5e06\u6d4e\u6ca7\u6d77\u3002",     // 长风破浪会有时，直挂云帆济沧海。
    };
    std::string out;
    for (auto* s : lines) {
        out += utils::WideToUtf8(s);
        out += '\n';
    }
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (ofs) ofs.write(out.data(), static_cast<std::streamsize>(out.size()));
}

void HitokotoManager::LoadLocal() {
    local_.clear();
    std::wstring path = utils::GetExeDir() + L"hitokoto_local.txt";

    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        // 文件不存在：生成默认语句文件后重读
        CreateDefaultLocalFile();
        ifs.open(path, std::ios::binary);
        if (!ifs) return;
    }
    // 读取大小上限：正常语句文件仅数 KB；超过 512KB 视为异常，
    // 只读前 512KB，防御意外超大文件一次性占满内存
    const std::streamsize kMaxLocalBytes = 512 * 1024;
    std::string content;
    content.resize(kMaxLocalBytes);
    std::streamsize got = ifs.read(&content[0], kMaxLocalBytes).gcount();
    content.resize(static_cast<size_t>(got));

    std::wistringstream ss(utils::Utf8ToWide(content));
    std::wstring line;
    while (std::getline(ss, line)) {
        // 去除行尾 \r 与首尾空白
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' ' || line.back() == L'\t'))
            line.pop_back();
        while (!line.empty() && (line.front() == L' ' || line.front() == L'\t'))
            line.erase(line.begin());
        if (!line.empty()) local_.push_back(line);
    }
}

void HitokotoManager::CreateDefaultLocalFile() {
    WriteDefaultLocalFile(utils::GetExeDir() + L"hitokoto_local.txt");
}

void HitokotoManager::TakeLocal() {
    if (local_.empty()) return;
    size_t n = local_.size();
    size_t idx = Rng()() % n;
    // 避免与当前显示重复（仅当列表多于一句时）
    if (n > 1 && local_[idx] == text_) idx = (idx + 1) % n;
    pending_ = local_[idx];
    hasPending_ = true;
    StartFadeOut();
}

// ---------------------------------------------------------------------------
// 网络一言（后台线程）
// ---------------------------------------------------------------------------
std::wstring HitokotoManager::FetchNetwork() {
    std::wstring result;
    HINTERNET hSession = WinHttpOpen(L"Countdown/1.0",
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;
    // 连接/发送/接收均限时，避免后台线程长期挂起
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"v1.hitokoto.cn",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET",
                                            L"/?charset=utf-8&encode=json",
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
        if (hReq) {
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                std::string body;
                char buf[4096];
                // 响应体上限：正常一言 JSON 仅数百字节，超过 64KB 视为异常，
                // 丢弃以防御响应无限增长导致的内存膨胀
                const size_t kMaxBody = 64 * 1024;
                DWORD available = 0;
                do {
                    if (!WinHttpQueryDataAvailable(hReq, &available)) break;
                    if (available == 0) break;
                    DWORD read = 0;
                    DWORD want = (std::min)(available, (DWORD)sizeof(buf));
                    if (!WinHttpReadData(hReq, buf, want, &read)) break;
                    size_t remaining = kMaxBody - body.size(); // body.size() <= kMaxBody
                    if (read >= remaining) {
                        body.append(buf, remaining);
                        break;
                    }
                    body.append(buf, read);
                } while (available > 0);

                // 解析 JSON，取 hitokoto 字段
                try {
                    mini_json::Value root = mini_json::Value::parse(body);
                    if (const auto* v = root.find("hitokoto")) {
                        result = utils::Utf8ToWide(v->as_string());
                    }
                } catch (...) {
                    result.clear();
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return result;
}

void HitokotoManager::StartNetwork() {
    if (netPending_.exchange(true)) return; // 已有请求在途
    HWND hwnd = hwnd_;
    std::thread([hwnd]() {
        std::wstring* result = new std::wstring(FetchNetwork());
        // 窗口可能已在请求期间销毁：PostMessage 失败时手动释放，
        // 避免堆分配字符串泄漏（成功时由主窗口 WM_APP_HITOKOTO 处理释放）
        if (!PostMessageW(hwnd, WM_APP_HITOKOTO, 0,
                          reinterpret_cast<LPARAM>(result))) {
            delete result;
        }
    }).detach();
}

void HitokotoManager::OnNetworkResult(const std::wstring& s) {
    netPending_ = false;
    if (!s.empty()) {
        pending_ = s;
        hasPending_ = true;
        StartFadeOut();
    } else if (cfg_.hitokotoSource == HitokotoSource::Both) {
        // 网络失败：兜底使用本地
        TakeLocal();
    }
    // 纯网络模式且失败：保留当前语句，等待下一刷新周期
}
