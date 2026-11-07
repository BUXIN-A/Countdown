// =============================================================================
// hitokoto.h - 一言数据获取与切换动画
//
// 职责：
//  1. 按配置来源（本地/网络/本地和网络）周期获取一句一言；
//  2. 维护当前文本与"渐隐 -> 换文本 -> 渐显"的切换动画状态机；
//  3. 本地语句存放在 exe 同目录 hitokoto\ 文件夹（读取所有 .txt，每行一句，
//     UTF-8）；文件夹为空时自动生成 default.txt；
//  4. 已显示过的语句记录在 temp\hitokoto_local.txt / hitokoto_network.txt，
//     随机时优先跳过已记录内容；一轮全部看过后清空对应记录。
//
// 网络请求在后台线程执行，完成后经 WM_APP_HITOKOTO 通知主窗口（线程安全：
// 后台线程不直接触碰成员，仅 PostMessage 传递堆分配的字符串）。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <atomic>
#include "config.h"

class HitokotoManager {
public:
    // 初始化：绑定窗口句柄、加载本地语句列表
    void Init(HWND hwnd, const Config& cfg);

    // 配置变化（来源/字体等）后立即按新配置获取一句
    void ApplyConfig(const Config& cfg);

    // 每帧调用：只推进渐隐/渐显动画（刷新由主窗口计划表驱动）
    void Tick(DWORD now);

    // 立即获取下一句（首次启动、配置变更后使用）
    void FetchNext();

    // 网络请求完成（主窗口从 WM_APP_HITOKOTO 转发；s 为空表示失败）
    void OnNetworkResult(const std::wstring& s);

    // 当前显示文本
    const std::wstring& Text() const { return text_; }

    // 一言区域当前透明度（0~255，用于渐隐渐显）
    int FadeAlpha() const { return static_cast<int>(fade_); }

    // 是否正在动画中（主窗口据此加快定时器刷新率）
    bool IsAnimating() const { return phase_ != FadePhase::Steady; }

private:
    enum class FadePhase { Steady, FadingOut, FadingIn };

    void Animate(DWORD now);
    void StartFadeOut();
    void LoadLocal();                    // 读取 hitokoto\ 内所有 .txt（为空则生成 default.txt）
    void CreateDefaultLocalFile();       // 写入默认语句列表
    void TakeLocal();                    // 从本地列表随机取一句并开始切换
    void StartNetwork();                 // 启动后台线程请求网络一言
    static std::wstring FetchNetwork(const std::string& category); // 阻塞请求（后台线程）

    HWND  hwnd_ = nullptr;
    Config cfg_;
    std::vector<std::wstring> local_;   // 本地语句列表

    std::wstring text_;                 // 当前显示
    std::wstring pending_;              // 待切换文本
    bool  hasPending_ = false;

    FadePhase phase_ = FadePhase::Steady;
    float fade_ = 255.0f;               // 当前透明度 0~255
    DWORD lastAnim_ = 0;

    std::atomic<bool> netPending_{false}; // 网络请求是否在途
};
