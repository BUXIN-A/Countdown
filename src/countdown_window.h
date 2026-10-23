// =============================================================================
// countdown_window.h - 主倒计时窗口
//
// 无标题栏、置顶、点击穿透、半透明圆角胶囊窗口。
// 显示三行内容：标题 / 倒计时内容（大字）/ 目标时间（小字）。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include "config.h"
#include "tray.h"
#include "hitokoto.h"
#include "article_window.h"

class CountdownWindow {
public:
    static const wchar_t* ClassName() { return L"CountdownMainWindow"; }

    // 注册窗口类
    static bool RegisterClass(HINSTANCE hInst);

    // 创建窗口
    bool Create(HINSTANCE hInst);

    HWND hwnd() const { return hwnd_; }

    // 应用最新配置（重绘、重定位、更新托盘提示）
    void ApplyConfig(const Config& cfg);

    // 显示设置窗口
    void OpenSettings();

    // 退出
    void Quit();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    // 处理托盘回调消息（由窗口过程 Handle 转发）
    void HandleTrayMessage(WPARAM wp, LPARAM lp);

    void OnCreate();
    void OnTimer();
    void OnDestroy();
    void OnDpiChanged();
    void Reposition();
    void ApplyRoundedRegion();

    // 逻辑像素 -> 当前 DPI 物理像素
    int S(int value) const;

    // 供布局/绘制共用的字号（物理像素）
    int TitleFontPx() const;
    int ContentFontPx() const;
    int TargetFontPx() const;
    int HkBaseFontPx() const;

    // 复用整窗 DIB（尺寸变化时才重建），避免每帧分配
    bool EnsureDib(int w, int h);
    void ReleaseDib();

    // 使用 UpdateLayeredWindow 重绘整个窗口（支持一言区域局部 alpha 渐变）
    void Render();
    // 重建字体（配置变化后调用）
    void RebuildFonts();
    void DestroyFonts();
    // 一言区域矩形（窗口客户区坐标）
    RECT HitokotoRect(int w, int h) const;
    // 绘制一言文本（字号随内容缩放，最多 2 行）。
    // base/stride 为主 DIB 像素与行跨距；fade 为动画透明度：fade<255 时
    // 通过独立文本层合成，仅文本渐隐渐显，背景保持不透明
    void DrawHitokoto(HDC mem, int w, int h, BYTE* base, int stride, int fade);

    // 计算当前剩余秒数
    long long ComputeRemainingSeconds() const;
    // 重绘（使无效）
    void RequestRedraw();

    HWND  hwnd_   = nullptr;
    HICON icon_   = nullptr;
    Tray  tray_;
    Config config_;
    ArticleWindow article_;
    UINT_PTR timer_id_ = 0;
    UINT timer_interval_ = 250; // 当前定时器间隔（毫秒），用于避免重复 SetTimer
    UINT dpi_ = 96;             // 窗口所在显示器 DPI

    // 复用的整窗 DIB（UpdateLayeredWindow 源）
    HDC      mem_dc_    = nullptr;
    HBITMAP  dib_       = nullptr;
    HGDIOBJ  old_dib_   = nullptr;
    void*    dib_bits_  = nullptr;
    int      dib_w_     = 0;
    int      dib_h_     = 0;

    // 渲染节流
    bool      dirty_          = true;
    bool      hasRendered_    = false;
    bool      animWasActive_  = false;
    long long lastRenderedRem_ = 0;

    // 缓存的字体（避免每帧创建）
    HFONT fTitle_   = nullptr;
    HFONT fContent_ = nullptr;
    HFONT fTarget_  = nullptr;
    // 一言自适应字号字体缓存（布局稳定时复用，避免每帧 CreateFontW）
    HFONT fHk_      = nullptr;
    int   fHkSize_  = 0;
    std::wstring lastHkText_; // 与该字体匹配的文本，用于判断缓存是否仍适用

    // 倒计时内容缓存：同一秒内多次重绘（如一言动画 30ms/帧）不再重复计算
    long long lastRemSec_ = 0;
    bool contentCacheValid_ = false;
    std::wstring lastContent_;
    std::wstring lastTargetText_;

    HitokotoManager hitokoto_;
};
