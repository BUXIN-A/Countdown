// =============================================================================
// article_window.h - 一文栏目（屏幕右侧长文卡片）
//
// 从 exe 同目录 article\ 文件夹加载文章（每个文件 / /*/ 分隔段落视为一篇）。
// 显示哪一篇由 lib\article 下的 DLL 模块决定（normal/week ...）；
// 模块缺失或加载失败时回退到内置随机逻辑。
// 刷新时刻由主窗口计划表驱动，窗口本身不保存计时器。
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include "article_module_registry.h"
#include "config.h"

class ArticleWindow {
public:
    static const wchar_t* ClassName() { return L"CountdownArticleWindow"; }
    static bool RegisterClass(HINSTANCE hInst);

    bool Create(HINSTANCE hInst, HWND main);
    void Destroy();

    // 应用配置：重新加载文章、模块配置、显示/隐藏、定位并重绘
    void ApplyConfig(const Config& cfg);

    // 计划表到期：按模块逻辑刷新文章
    void RefreshNow();
    // 本地零点：week 模块需要按新星期重新评估
    void RefreshForDayChange();
    // 自动刷新间隔（秒），由当前模块声明；0 = 不自动刷新
    int  RefreshSeconds() const;

    // 重新定位（配置/DPI/倒计时位置变化后调用）
    void Reposition();

    HWND hwnd() const { return hwnd_; }

    // ---- 一文模块宿主回调（public 供 C 回调函数访问）----
    int ModuleArticleCount() const;
    const char* ModuleArticleKey(int index) const;
    bool ModuleIsSeen(const char* key) const;
    void ModuleMarkSeen(const char* key);
    void ModuleClearSeen();
    const wchar_t* ModuleTempDir() const;
    void ModuleLocalTime(int* year, int* month, int* day, int* weekday,
                         int* hour, int* minute, int* second);
    unsigned int ModuleRandomU32();

private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    LRESULT Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    void LoadArticles();
    void CreateDefaultFile();
    size_t PickRandomUnseen();       // 内置回退：随机选未显示过的
    size_t AdvanceRandom();          // 内置回退：避免与当前重复

    // 用当前模块/内置逻辑选择文章；reason 见 ARTICLE_SELECT_*
    // 返回索引或可见状态是否发生变化
    bool SelectArticle(int reason);
    void MarkCurrentSeen();

    void ApplyRoundedRegion();
    void Render();

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
    std::vector<std::string>  articleKeys_;
    size_t index_ = 0;
    bool   hasArticle_ = false; // 当前是否有可显示文章（week 未覆盖日为 false）

    // 当前模块
    ArticleModuleInstance module_;
    ArticleModuleHostApi  moduleHost_{};
    std::wstring          moduleId_;

    int layoutFontPx_ = 0;

    // 正文渲染字体缓存（动画每帧重绘时避免反复 CreateFontW）
    HFONT textFont_ = nullptr;
    int   textFontPx_ = 0;
    std::wstring textFontFamily_;
    void DestroyTextFont();

    HDC      mem_dc_   = nullptr;
    HBITMAP  dib_      = nullptr;
    HGDIOBJ  old_dib_  = nullptr;
    void*    dib_bits_ = nullptr;
    int      dib_w_    = 0;
    int      dib_h_    = 0;
};
