// =============================================================================
// hitokoto.cpp - 一言实现
// =============================================================================
#include "hitokoto.h"
#include "../resource.h"
#include "history.h"
#include "json.h"
#include "utils.h"
#include "version.h"

#include <winhttp.h>
#include <thread>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <new>
#include <random>
#include <ctime>
#include <utility>

// 渐隐/渐显时长（毫秒）
static const float kFadeOutMs = 150.0f;
static const float kFadeInMs  = 200.0f;

// 网络返回已看过的句子时，最多连续重新请求的次数
static const int kMaxFetchRetries = 6;

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
}

void HitokotoManager::ApplyConfig(const Config& cfg) {
    cfg_ = cfg;
    LoadLocal();  // 本地文件可能在设置期间新增
    FetchNext();  // 立即按新配置获取一句
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
// 每帧：只推进动画；刷新时刻由主窗口计划表核对
// ---------------------------------------------------------------------------
void HitokotoManager::Tick(DWORD now) {
    Animate(now);
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
// 本地语句：hitokoto 文件夹内所有 .txt，每行一句
// ---------------------------------------------------------------------------
static bool WriteDefaultLocalFile(const std::wstring& path) {
    const wchar_t* lines[] = {
        L"\u7eb5\u6709\u75be\u98ce\u8d77\uff0c\u4eba\u751f\u4e0d\u8a00\u5f03\u3002",           // 纵有疾风起，人生不言弃。
        L"\u4fdd\u6301\u70ed\u7231\uff0c\u5954\u8d74\u5c71\u6d77\u3002",                       // 保持热爱，奔赴山海。
        L"\u661f\u5149\u4e0d\u95ee\u8d76\u8def\u4eba\uff0c\u65f6\u5149\u4e0d\u8d1f\u6709\u5fc3\u4eba\u3002", // 星光不问赶路人，时光不负有心人。
        L"\u8def\u867d\u8fdc\uff0c\u884c\u5219\u5c06\u81f3\uff1b\u4e8b\u96be\uff0c\u505a\u5219\u5fc5\u6210\u3002", // 路虽远，行则将至；事虽难，做则必成。
        L"\u957f\u98ce\u7834\u6d6a\u4f1a\u6709\u65f6\uff0c\u76f4\u6302\u4e91\u5e06\u6d4e\u6ca7\u6d77\u3002",     // 长风破浪会有时，直挂云帆济沧海。
    };
    std::wstring text;
    for (auto* s : lines) {
        text += s;
        text += L'\n';
    }
    return utils::WriteTextFile(path, text);
}

void HitokotoManager::LoadLocal() {
    local_.clear();
    std::wstring dir = utils::GetExeDir() + L"hitokoto";
    utils::EnsureDirectory(dir);

    std::vector<std::wstring> files = utils::ListTxtFiles(dir);
    if (files.empty()) {
        // 文件夹为空：生成 default.txt 后使用
        CreateDefaultLocalFile();
        files = utils::ListTxtFiles(dir);
    }

    for (const auto& file : files) {
        std::wstring content;
        // 单个文件读取上限 512KB，防御异常超大文件
        if (!utils::ReadTextFile(file, content, 512 * 1024)) continue;

        std::wistringstream ss(content);
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
}

void HitokotoManager::CreateDefaultLocalFile() {
    std::wstring dir = utils::GetExeDir() + L"hitokoto";
    utils::EnsureDirectory(dir);
    WriteDefaultLocalFile(dir + L"\\default.txt");
}

void HitokotoManager::TakeLocal() {
    if (local_.empty()) return;
    const size_t n = local_.size();

    // 优先在“未显示过”的语句中随机；同时避免与当前屏幕上这句重复
    std::vector<size_t> unseen;
    history::CollectUnseen(history::Kind::LocalHitokoto, local_, unseen);

    std::vector<size_t> candidates;
    for (size_t idx : unseen) {
        if (n > 1 && local_[idx] == text_) continue;
        candidates.push_back(idx);
    }

    if (candidates.empty()) {
        // 所有本地语句都显示过：清空记录，开始新一轮
        history::Clear(history::Kind::LocalHitokoto);
        for (size_t i = 0; i < n; ++i) {
            if (n > 1 && local_[i] == text_) continue;
            candidates.push_back(i);
        }
        if (candidates.empty()) candidates.push_back(0);
    }

    size_t idx = candidates[Rng()() % candidates.size()];
    pending_ = local_[idx];
    hasPending_ = true;
    history::MarkSeen(history::Kind::LocalHitokoto, pending_);
    StartFadeOut();
}

// ---------------------------------------------------------------------------
// 网络一言（后台线程）
// ---------------------------------------------------------------------------
std::wstring HitokotoManager::FetchNetwork(const std::string& category) {
    std::wstring result;
    HINTERNET hSession = WinHttpOpen(L"Countdown/" APP_VERSION_STRING,
                                     WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME,
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;
    // 连接/发送/接收均限时，避免后台线程长期挂起
    WinHttpSetTimeouts(hSession, 5000, 5000, 5000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, L"v1.hitokoto.cn",
                                        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (hConnect) {
        // 句子类型：category 为空表示全部，不带 c 参数
        std::wstring path = L"/?charset=utf-8&encode=json";
        if (!category.empty()) {
            path += L"&c=";
            path += utils::Utf8ToWide(category);
        }

        HINTERNET hReq = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
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
    std::string category = cfg_.hitokotoCategory;

    std::thread([hwnd, category]() {
        std::wstring result;
        bool duplicate = false;
        for (int attempt = 0; attempt < kMaxFetchRetries; ++attempt) {
            result = FetchNetwork(category);
            if (result.empty()) {
                duplicate = false;
                break; // 请求失败：交给主线程决定是否兜底
            }
            duplicate = history::IsSeen(history::Kind::NetworkHitokoto, result);
            if (!duplicate) break; // 取到新句子
            // 已显示过：重新获取
        }
        if (!result.empty()) {
            // 连续多次都取到看过的句子，视为该类型已看完一轮：清空后重新开始
            if (duplicate) history::Clear(history::Kind::NetworkHitokoto);
            history::MarkSeen(history::Kind::NetworkHitokoto, result);
        }

        std::wstring* payload = new (std::nothrow) std::wstring(std::move(result));
        if (!payload) return;
        // 窗口可能已在请求期间销毁：先检查句柄有效，再投递；
        // 失败时手动释放，避免堆分配字符串泄漏
        if (!IsWindow(hwnd) ||
            !PostMessageW(hwnd, WM_APP_HITOKOTO, 0,
                          reinterpret_cast<LPARAM>(payload))) {
            delete payload;
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
