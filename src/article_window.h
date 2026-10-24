// =============================================================================
// article_window.h - 一文栏目（屏幕右侧长文卡片）
//
// 独立的分层窗口，从 exe 同目录 article\ 文件夹内所有 .txt 加载文章
// （每个文件视为一篇），按配置的刷新时间随机切换，并通过 temp\article.txt
// 记录已显示过的文章，尽量不重复；全部看过后清空记录重新开始。
// 位置使用九宫格锚点 + 偏移，并在与倒计时窗口相交时自动避让。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "config.h"

class ArticleWindow {
public:
    static const wchar_t* ClassName() { return L"CountdownArticleWindow"; }
    static bool RegisterClass(HINSTANCE hInst);

    // 创建（初始隐藏），main 用于避让倒计时窗口
    bool Create(HINSTANCE hInst, HWND main);

    // 销毁窗口与 GDI 资源
    void Destroy();

    // 应用配置：重新加载文章、显示/隐藏、定位并重绘
    void ApplyConfig(const Config& cfg);

    // 主窗口定时器驱动：检查刷新周期
    void Tick();

    // 重新定位（配置/DPI/倒计时位置变化后调用）
    void Reposition();

    HWND hwnd() const { return hwnd_; }

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void LoadArticles();
    void CreateDefaultFile();
    size_t PickRandomUnseen();   // 随机选一篇未显示过的（全部看过则清空后重选）
    void AdvanceRandom();        // 随机切换到下一篇（避免与当前重复）
    void ApplyRoundedRegion();
    void Render();

    // 尺寸/位置切换动画（切换文章或改尺寸时平滑过渡）
    void ApplyTargetRect(int x, int y, int w, int h, bool topmost, bool animate);
    void StepAnimation();
    void StopAnimation();

    static const UINT_PTR kAnimTimerId = 1;

    bool  animating_ = false;
    DWORD animStart_ = 0;
    RECT  animFrom_{};
    RECT  animTo_{};
    bool  animTopmost_ = false;

    bool EnsureDib(int w, int h);
    void ReleaseDib();

    int S(int value) const;

    HWND   hwnd_ = nullptr;
    HWND   main_ = nullptr;
    Config cfg_;
    UINT   dpi_ = 96;

    std::vector<std::wstring> articles_;
    size_t index_ = 0;
    bool   firstPick_ = true;   // 首次显示时随机选一篇
    DWORD  lastSwitch_ = 0;

    // 由 Reposition 决定、Render 复用的字号
    int layoutFontPx_ = 0;

    // 复用的整窗 DIB
    HDC      mem_dc_   = nullptr;
    HBITMAP  dib_      = nullptr;
    HGDIOBJ  old_dib_  = nullptr;
    void*    dib_bits_ = nullptr;
    int      dib_w_    = 0;
    int      dib_h_    = 0;
};
