// =============================================================================
// settings_window.cpp - 深色无边框设置窗口（四标签 + 可滚动内容）
//
// 标签：基础设置 / 倒计时 / 一文 / 关于
// 每页内容放在独立的子窗口中，超出高度时可用滚轮/右侧滑块滚动，
// 子窗口自带裁剪，避免控件覆盖标签栏与底部按钮。
// 值控件沿用深色胶囊、下拉浮层、数字步进器与月历浮层。
// =============================================================================
#include "settings_window.h"
#include "../resource.h"
#include "article_module_registry.h"
#include "autostart.h"
#include "history.h"
#include "hitokoto_category.h"
#include "json.h"
#include "settings_module_config.h"
#include "utils.h"
#include "version.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <initializer_list>
#include <new>
#include <string>
#include <utility>
#include <vector>

// ---------------------------------------------------------------------------
// 配色
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
static const COLORREF kAccentDim  = RGB(38, 56, 142);
static const COLORREF kOnBg       = RGB(38, 48, 88);
static const COLORREF kOnBorder   = RGB(80, 110, 255);

// ---------------------------------------------------------------------------
// 布局常量（96 DPI 逻辑像素）
// ---------------------------------------------------------------------------
static const int kPad        = 24;
static const int kWinW       = 640;
static const int kWinH       = 620;
static const int kTabTop     = 12;
static const int kTabH       = 30;
static const int kTabDivider = 54;
static const int kContentTop = 70;
static const int kRowH       = 32;
static const int kRowStep    = 38;
static const int kBtnH       = 30;
static const int kBtnW       = 88;
static const int kTabCount   = 4;

enum SettingsPage {
    PageBasic = 0,
    PageCountdown,
    PageArticle,
    PageAbout,
};

enum ColorTarget {
    ColorCountdownBg = 0,
    ColorCountdownText,
    ColorArticleBg,
    ColorArticleText,
};

struct ChipSlot {
    HWND child = nullptr;
    RECT rc{};
};

enum DropdownKind {
    DropCycle = 0,
    DropFormat,
    DropSource,
    DropHkCategory,
    DropArtAnchor,
    DropWinAnchor,
    DropArtSize,
    DropFont,
    DropHkFont,
    DropArtFont,
    DropArticleModule,
    DropTempMode,
    DropTempUnit,
    DropTempTrim,
};

struct DropdownDef {
    HWND trigger = nullptr;
    DropdownKind kind = DropCycle;
    std::vector<std::wstring> items;
    std::vector<std::wstring> values; // 为空时与 items 相同
    int current = 0;
};

struct NumberField {
    HWND minus = nullptr;
    HWND edit = nullptr;
    HWND plus = nullptr;
    int lo = 0;
    int hi = 0;
};

struct CalendarViewData {
    struct SettingsState* st = nullptr;
};

struct PageViewData {
    struct SettingsState* st = nullptr;
    int page = 0;

    // 页面双缓冲（滚动/焦点重绘复用，避免每次 WM_PAINT 新建位图）
    HDC     mem = nullptr;
    HBITMAP bmp = nullptr;
    HGDIOBJ oldBmp = nullptr;
    int     memW = 0;
    int     memH = 0;
};

struct SettingsState; // 前置声明

struct DropdownPopupData {
    SettingsState* st = nullptr;
    DropdownKind kind = DropCycle;
    HWND list = nullptr;
};

struct PageControlPos {
    HWND hwnd = nullptr;
    int x = 0;
    int baseY = 0;
    int w = 0;
    int h = 0;
};

struct SettingsState {
    Config cfg;
    HWND   parent = nullptr;
    HWND   dlg    = nullptr;
    bool   saved  = false;
    bool   running = true;
    UINT   dpi = 96;

    HFONT  font = nullptr;
    HFONT  boldFont = nullptr;
    HFONT  aboutTitleFont = nullptr;
    HBRUSH brBg = nullptr;
    HBRUSH brChip = nullptr;

    int  page = PageBasic;
    int  hoverTab = -1;
    bool trackingMouse = false;

    HWND pageViews[kTabCount] = {};
    int  pageScroll[kTabCount] = {};
    int  pageContentH[kTabCount] = {};
    int  contentW = 0;
    int  viewH = 0;

    std::vector<PageControlPos> pageCtls[kTabCount];
    std::vector<ChipSlot> chips[kTabCount];
    std::vector<int> rowDividerY[kTabCount];
    std::vector<HWND> sectionLabels;

    bool optStart = false;
    bool optArticle = false;
    bool optArtTop = false;
    bool optTopMost = false;
    bool optAutoStart = false;
    bool optHkBold = false;
    bool optDummy = false;

    std::vector<RECT> tabRects;

    HWND hEditTitle = nullptr;
    HWND hEditDate = nullptr;
    HWND hBtnCalendar = nullptr;
    HWND hEditTime = nullptr;
    HWND hBtnCycle = nullptr;
    HWND hBtnStart = nullptr;
    HWND hBtnFormat = nullptr;
    HWND hEditSize = nullptr;
    HWND hBtnBgColor = nullptr;
    HWND hBtnTextColor = nullptr;
    HWND hBtnHkSrc = nullptr;
    HWND hBtnHkCategory = nullptr;
    HWND hEditHkRefresh = nullptr;
    HWND hBtnHkBold = nullptr;
    HWND hBtnArticle = nullptr;
    HWND hBtnArtAnchor = nullptr;
    HWND hEditArtX = nullptr;
    HWND hEditArtY = nullptr;
    HWND hEditArtH = nullptr;
    HWND hEditArtM = nullptr;
    HWND hEditArtS = nullptr;
    HWND hBtnArtTop = nullptr;
    HWND hEditArtFont = nullptr;
    HWND hBtnArtBgColor = nullptr;
    HWND hBtnArtTextColor = nullptr;
    HWND hBtnArtSize = nullptr;
    HWND hEditArtWidth = nullptr;
    HWND hEditArtHeight = nullptr;
    HWND hBtnArticleModule = nullptr;
    HWND hBtnModuleConfig = nullptr;
    HWND hModuleHint = nullptr;
    HWND hBtnTop = nullptr;
    HWND hBtnAnchor = nullptr;
    HWND hEditPlaceX = nullptr;
    HWND hEditPlaceY = nullptr;
    HWND hBtnAuto = nullptr;
    HWND hEditTempArtLimit = nullptr;
    HWND hEditTempHkLimit = nullptr;
    HWND hEditTempTrimCount = nullptr;
    HWND hTempArtLabel = nullptr;
    HWND hTempHkLabel = nullptr;
    HWND hBtnTempClean = nullptr;
    HWND hBtnUpdate = nullptr;
    HWND hBtnSave = nullptr;
    HWND hBtnCancel = nullptr;

    HWND hCalPopup = nullptr;
    HWND hCalPrevYear = nullptr;
    HWND hCalNextYear = nullptr;
    HWND hDropdown = nullptr;
    HWND hDropdownTrigger = nullptr;

    // 自绘月历视图/选中日期
    int calYear = 2000;
    int calMonth = 1;
    int calSelYear = 0;
    int calSelMonth = 0;
    int calSelDay = 0;
    bool calHasSelection = false;

    HICON aboutIcon = nullptr;

    // 设置窗口双缓冲
    HDC     paintDc = nullptr;
    HBITMAP paintBmp = nullptr;
    HGDIOBJ paintOldBmp = nullptr;
    int     paintW = 0;
    int     paintH = 0;

    int tempTrimFieldIndex = -1;

    // 页面滚动条拖动状态
    int dragPage = -1;
    int dragStartY = 0;
    int dragStartScroll = 0;

    std::vector<DropdownDef> dropdowns;
    std::vector<NumberField> numbers;

    int S(int value) const {
        return utils::ScaleDpi(value, static_cast<int>(dpi));
    }
};

static const wchar_t* kClass = L"CountdownSettingsClass";
static const wchar_t* kDropdownClass = L"CountdownSettingsDropdown";
static const wchar_t* kPageClass = L"CountdownSettingsPage";
static const wchar_t* kCalendarClass = L"CountdownSettingsCalendar";
static const wchar_t* kTabNames[kTabCount] = {
    L"\u57fa\u7840\u8bbe\u7f6e", // 基础设置
    L"\u5012\u8ba1\u65f6",       // 倒计时
    L"\u4e00\u6587",             // 一文
    L"\u5173\u4e8e",             // 关于
};

// 前置声明
static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK DropdownWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK PageWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK CalendarWndProc(HWND, UINT, WPARAM, LPARAM);
static bool RegisterDropdownClass(HINSTANCE hInst);
static bool RegisterPageClass(HINSTANCE hInst);
static bool RegisterCalendarClass(HINSTANCE hInst);
static void OnCreate(HWND hwnd, SettingsState* st);
static bool CollectAndValidate(HWND hwnd, SettingsState* st);
static void ShowPage(SettingsState* st, int page);
static void LayoutTabs(SettingsState* st, HWND hwnd);
static void CloseCalendarPopup(SettingsState* st);
static void CloseDropdown(SettingsState* st);
static void OpenDropdown(SettingsState* st, DropdownDef* def);
static void ScrollPageBy(SettingsState* st, int page, int delta);
static void UpdateTempLabels(SettingsState* st);
static void UpdateTempControls(SettingsState* st);
static void UpdateModuleHint(SettingsState* st);

// ---------------------------------------------------------------------------
// 文本辅助
// ---------------------------------------------------------------------------
static std::wstring FormatDateText(const std::tm& t) {
    wchar_t buf[32];
    swprintf_s(buf, L"%04d-%02d-%02d", t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    return buf;
}

static std::wstring FormatClockText(const std::tm& t) {
    wchar_t buf[32];
    swprintf_s(buf, L"%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

static std::vector<std::wstring> CycleItems() {
    return {
        L"\u5173\u95ed", L"\u6bcf\u5e74", L"\u6bcf\u6708",
        L"\u6bcf\u5929", L"\u6bcf\u5c0f\u65f6", L"\u6bcf\u5206\u949f",
        L"\u6bcf\u79d2",
    };
}

static std::vector<std::wstring> FormatItems() {
    return {
        L"\u65f6:\u5206:\u79d2",
        L"\u5e74\u6708\u65e5 \u65f6\u5206\u79d2",
        L"\u5929\u6570 \u65f6:\u5206:\u79d2",
    };
}

static std::vector<std::wstring> SourceItems() {
    return {
        L"\u672c\u5730", L"\u7f51\u7edc", L"\u672c\u5730\u548c\u7f51\u7edc",
    };
}

static std::vector<std::wstring> AnchorItems() {
    return {
        L"\u5de6\u4e0a\u89d2", L"\u9876\u90e8\u5c45\u4e2d", L"\u53f3\u4e0a\u89d2",
        L"\u5de6\u5c45\u4e2d", L"\u5c4f\u5e55\u5c45\u4e2d", L"\u53f3\u5c45\u4e2d",
        L"\u5de6\u4e0b\u89d2", L"\u5e95\u90e8\u5c45\u4e2d", L"\u53f3\u4e0b\u89d2",
    };
}

static std::vector<std::wstring> ArticleSizeItems() {
    return {
        L"\u81ea\u9002\u5e94\uff08\u56fa\u5b9a\u5bbd\uff09",
        L"\u5168\u81ea\u9002\u5e94",
        L"\u81ea\u5b9a\u4e49",
    };
}

static std::vector<std::wstring> TempModeItems() {
    return { L"\u6309\u9879\u6570", L"\u6309\u5927\u5c0f" };
}

static std::vector<std::wstring> TempUnitItems() {
    return { L"MB", L"GB" };
}

static std::vector<std::wstring> TempTrimItems() {
    return { L"\u6e05\u7a7a\u5168\u90e8", L"\u5220\u9664\u6700\u65e9 N \u9879" };
}

static bool ParseIntField(HWND edit, int& out) {
    wchar_t buf[256] = {0};
    GetWindowTextW(edit, buf, 256);

    const wchar_t* p = buf;
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'\0') { out = 0; return true; }

    errno = 0;
    wchar_t* end = nullptr;
    long v = wcstol(p, &end, 10);
    if (end == p || errno == ERANGE) return false;
    while (*end == L' ' || *end == L'\t') ++end;
    if (*end != L'\0') return false;

    out = static_cast<int>(v);
    return true;
}

static DropdownDef* FindDropdownByKind(SettingsState* st, DropdownKind kind) {
    for (auto& d : st->dropdowns) {
        if (d.kind == kind) return &d;
    }
    return nullptr;
}

static DropdownDef* FindDropdownByTrigger(SettingsState* st, HWND trigger) {
    for (auto& d : st->dropdowns) {
        if (d.trigger == trigger) return &d;
    }
    return nullptr;
}

static NumberField* FindNumber(SettingsState* st, HWND edit) {
    for (auto& n : st->numbers) {
        if (n.edit == edit) return &n;
    }
    return nullptr;
}

// 字体列表（系统 + fonts 目录本地字体），三个字体下拉共用一次枚举结果
struct FontCatalog {
    std::vector<std::wstring> system;
    std::vector<std::wstring> locals;
};

struct FontChoices {
    std::vector<std::wstring> items;
    std::vector<std::wstring> values;
};

static FontCatalog LoadFontCatalog() {
    FontCatalog cat;
    cat.locals = utils::LoadLocalFonts();
    cat.system = utils::EnumerateSystemFonts();
    return cat;
}

static FontChoices BuildFontChoices(const FontCatalog& catalog,
                                    const std::wstring& current) {
    FontChoices out;
    for (const auto& name : catalog.system) {
        out.values.push_back(name);
        bool isLocal = std::find(catalog.locals.begin(), catalog.locals.end(),
                                 name) != catalog.locals.end();
        out.items.push_back(isLocal ? name + L"\uff08\u672c\u5730\uff09" : name);
    }
    bool found = false;
    for (size_t i = 0; i < out.values.size(); ++i) {
        if (lstrcmpiW(out.values[i].c_str(), current.c_str()) == 0) {
            found = true;
            break;
        }
    }
    if (!found && !current.empty()) {
        out.values.insert(out.values.begin(), current);
        out.items.insert(out.items.begin(), current);
    }
    if (out.items.empty()) {
        std::wstring def = utils::GetSystemDefaultFontName();
        out.values.push_back(def);
        out.items.push_back(def);
    }
    return out;
}

static int FontIndex(const FontChoices& choices, const std::wstring& current) {
    for (size_t i = 0; i < choices.values.size(); ++i) {
        if (lstrcmpiW(choices.values[i].c_str(), current.c_str()) == 0) {
            return static_cast<int>(i);
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// 选中下拉项：写入配置并更新触发按钮
// ---------------------------------------------------------------------------
static void UpdateTempLabels(SettingsState* st) {
    if (!st) return;
    bool count = st->cfg.tempCleanupMode == TempCleanupMode::Count;
    std::wstring unit = st->cfg.tempSizeUnit == TempSizeUnit::GB ? L"GB" : L"MB";
    std::wstring article = count
        ? L"\u4e00\u6587\u4e0a\u9650\uff08\u9879\uff09"
        : L"\u4e00\u6587\u4e0a\u9650\uff08" + unit + L"\uff09";
    std::wstring hitokoto = count
        ? L"\u4e00\u8a00\u4e0a\u9650\uff08\u9879\uff09"
        : L"\u4e00\u8a00\u4e0a\u9650\uff08" + unit + L"\uff09";
    if (st->hTempArtLabel) SetWindowTextW(st->hTempArtLabel, article.c_str());
    if (st->hTempHkLabel) SetWindowTextW(st->hTempHkLabel, hitokoto.c_str());
}

static void UpdateTempControls(SettingsState* st) {
    if (!st) return;
    bool canTrimCount = st->cfg.tempTrimMode == TempTrimMode::DropOldest;
    if (st->tempTrimFieldIndex >= 0 &&
        st->tempTrimFieldIndex < static_cast<int>(st->numbers.size())) {
        NumberField& f = st->numbers[st->tempTrimFieldIndex];
        EnableWindow(f.minus, canTrimCount);
        EnableWindow(f.edit, canTrimCount);
        EnableWindow(f.plus, canTrimCount);
    }
}

static void UpdateModuleHint(SettingsState* st) {
    if (!st || !st->hModuleHint) return;
    const ArticleModuleEntry* entry =
        ArticleModuleRegistry::Instance().Find(st->cfg.articleModule);
    if (entry) {
        std::wstring text = entry->description;
        if (text.empty()) text = L"\u6a21\u5757\uff1a" + entry->name;
        SetWindowTextW(st->hModuleHint, text.c_str());
        if (st->hBtnModuleConfig) EnableWindow(st->hBtnModuleConfig, TRUE);
    } else {
        SetWindowTextW(st->hModuleHint,
            L"\u672a\u627e\u5230\u8be5\u6a21\u5757\uff0c\u5c06\u4f7f\u7528\u5185\u7f6e\u666e\u901a\u6a21\u5f0f");
        if (st->hBtnModuleConfig) EnableWindow(st->hBtnModuleConfig, FALSE);
    }
}

static void ApplyDropdownSelection(SettingsState* st, DropdownDef* def, int index) {
    if (!st || !def || index < 0 || index >= static_cast<int>(def->items.size())) {
        return;
    }
    def->current = index;
    auto value = [&]() -> std::wstring {
        if (index < static_cast<int>(def->values.size())) return def->values[index];
        return def->items[index];
    };

    switch (def->kind) {
        case DropCycle:
            st->cfg.cycle = static_cast<CycleUnit>(index);
            break;
        case DropFormat:
            st->cfg.displayFormat = static_cast<utils::DisplayFormat>(index);
            break;
        case DropSource:
            st->cfg.hitokotoSource = static_cast<HitokotoSource>(index);
            break;
        case DropHkCategory:
            if (index < hitokoto_category::Count()) {
                st->cfg.hitokotoCategory = hitokoto_category::kOptions[index].code;
            }
            break;
        case DropArtAnchor:
            st->cfg.articleAnchor = static_cast<WindowAnchor>(index);
            break;
        case DropWinAnchor:
            st->cfg.anchor = static_cast<WindowAnchor>(index);
            break;
        case DropArtSize:
            st->cfg.articleSizeMode = static_cast<ArticleSizeMode>(index);
            break;
        case DropFont:
            st->cfg.font = value();
            break;
        case DropHkFont:
            st->cfg.hitokotoFont = value();
            break;
        case DropArtFont:
            st->cfg.articleFont = value();
            break;
        case DropArticleModule:
            st->cfg.articleModule = value();
            UpdateModuleHint(st);
            break;
        case DropTempMode:
            st->cfg.tempCleanupMode = index == 1
                ? TempCleanupMode::Size : TempCleanupMode::Count;
            UpdateTempLabels(st);
            UpdateTempControls(st);
            break;
        case DropTempUnit:
            st->cfg.tempSizeUnit = index == 1
                ? TempSizeUnit::GB : TempSizeUnit::MB;
            UpdateTempLabels(st);
            break;
        case DropTempTrim:
            st->cfg.tempTrimMode = index == 1
                ? TempTrimMode::DropOldest : TempTrimMode::ClearAll;
            UpdateTempControls(st);
            break;
    }
    SetWindowTextW(def->trigger, def->items[index].c_str());
    InvalidateRect(def->trigger, nullptr, TRUE);
}

static void AdjustNumber(NumberField& f, int delta) {
    int v = f.lo;
    if (!ParseIntField(f.edit, v)) v = f.lo;
    v += delta;
    if (v < f.lo) v = f.lo;
    if (v > f.hi) v = f.hi;
    wchar_t buf[32];
    swprintf_s(buf, L"%d", v);
    SetWindowTextW(f.edit, buf);
}

// ---------------------------------------------------------------------------
// 颜色选择
// ---------------------------------------------------------------------------
static COLORREF g_customColors[16] = {
    RGB(20, 20, 30),    RGB(36, 36, 45),   RGB(62, 94, 240),
    RGB(92, 124, 255),  RGB(235, 235, 245), RGB(170, 170, 190),
    RGB(255, 255, 255), RGB(0, 0, 0),      RGB(70, 40, 40),
    RGB(40, 70, 40),    RGB(40, 50, 80),   RGB(80, 60, 40),
    RGB(120, 80, 160),  RGB(200, 120, 60), RGB(60, 160, 160),
    RGB(200, 80, 120),
};

static void PickColor(HWND hwnd, SettingsState* st, ColorTarget target) {
    if (!st) return;
    COLORREF initial = 0;
    switch (target) {
        case ColorCountdownBg:   initial = st->cfg.backgroundColor; break;
        case ColorCountdownText: initial = st->cfg.textColor; break;
        case ColorArticleBg:     initial = st->cfg.articleBackgroundColor; break;
        default:                 initial = st->cfg.articleTextColor; break;
    }

    CHOOSECOLORW cc{};
    cc.lStructSize = sizeof(cc);
    cc.hwndOwner = hwnd;
    cc.rgbResult = initial;
    cc.lpCustColors = g_customColors;
    cc.Flags = CC_FULLOPEN | CC_RGBINIT | CC_ANYCOLOR;
    if (!ChooseColorW(&cc)) return;

    HWND btn = nullptr;
    switch (target) {
        case ColorCountdownBg:
            st->cfg.backgroundColor = cc.rgbResult;
            btn = st->hBtnBgColor;
            break;
        case ColorCountdownText:
            st->cfg.textColor = cc.rgbResult;
            btn = st->hBtnTextColor;
            break;
        case ColorArticleBg:
            st->cfg.articleBackgroundColor = cc.rgbResult;
            btn = st->hBtnArtBgColor;
            break;
        default:
            st->cfg.articleTextColor = cc.rgbResult;
            btn = st->hBtnArtTextColor;
            break;
    }
    if (btn) {
        std::string hex = utils::ColorToHex(cc.rgbResult);
        std::wstring whex(hex.begin(), hex.end());
        SetWindowTextW(btn, whex.c_str());
        InvalidateRect(btn, nullptr, TRUE);
    }
}

// ---------------------------------------------------------------------------
// 窗口类注册
// ---------------------------------------------------------------------------
static bool RegisterSettingsClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kClass, &exist) == 0) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.style         = 0;
        wc.lpfnWndProc   = SettingsWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc)) return false;
    }
    return RegisterDropdownClass(hInst) && RegisterPageClass(hInst) &&
           RegisterCalendarClass(hInst);
}

// ---------------------------------------------------------------------------
// 控件创建
// ---------------------------------------------------------------------------
static HINSTANCE InstOf(HWND h) {
    return (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
}

static void RegisterPageControl(SettingsState* st, int page, HWND hwnd) {
    if (!st || !hwnd || page < 0 || page >= kTabCount) return;
    HWND parent = st->pageViews[page];
    if (!parent) return;
    RECT rc{};
    GetWindowRect(hwnd, &rc);
    MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&rc), 2);
    PageControlPos pos;
    pos.hwnd = hwnd;
    pos.x = rc.left;
    pos.baseY = rc.top;
    pos.w = rc.right - rc.left;
    pos.h = rc.bottom - rc.top;
    st->pageCtls[page].push_back(pos);
    int bottom = rc.bottom + st->S(12);
    if (bottom > st->pageContentH[page]) st->pageContentH[page] = bottom;
}

static HWND CreateChild(SettingsState* st, int page, const wchar_t* cls,
                        const wchar_t* text, DWORD style, DWORD exStyle,
                        int x, int y, int w, int h, int id, HWND /*parent*/,
                        bool tabStop = true) {
    if (tabStop && (style & WS_CHILD) &&
        (lstrcmpiW(cls, L"EDIT") == 0 || lstrcmpiW(cls, L"BUTTON") == 0 ||
         lstrcmpiW(cls, L"COMBOBOX") == 0)) {
        style |= WS_TABSTOP;
    }
    HWND parent = st->pageViews[page];
    if (!parent) return nullptr;
    HWND hwnd = CreateWindowExW(exStyle, cls, text, style, x, y, w, h,
                                parent, (HMENU)(INT_PTR)id, InstOf(parent),
                                nullptr);
    if (hwnd) RegisterPageControl(st, page, hwnd);
    return hwnd;
}

static HWND CreateLabel(SettingsState* st, int page, const wchar_t* text,
                        int x, int y, int w) {
    HWND h = CreateChild(st, page, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS, 0,
        x, y, w, st->S(kRowH), 0, nullptr, false);
    return h;
}

static bool IsToggleId(int id) {
    return id == IDC_CHECK_INCLUDE_START || id == IDC_CHECK_ARTICLE ||
           id == IDC_CHECK_ARTICLE_TOP || id == IDC_CHECK_TOPMOST ||
           id == IDC_CHECK_AUTOSTART || id == IDC_BTN_HK_BOLD;
}

static bool& ToggleRef(SettingsState* st, int id) {
    switch (id) {
        case IDC_CHECK_INCLUDE_START: return st->optStart;
        case IDC_CHECK_ARTICLE:       return st->optArticle;
        case IDC_CHECK_ARTICLE_TOP:   return st->optArtTop;
        case IDC_CHECK_TOPMOST:       return st->optTopMost;
        case IDC_CHECK_AUTOSTART:     return st->optAutoStart;
        case IDC_BTN_HK_BOLD:         return st->optHkBold;
        default:                      return st->optDummy;
    }
}

// ---------------------------------------------------------------------------
// 页面滚动
// ---------------------------------------------------------------------------
static int MaxScroll(SettingsState* st, int page) {
    int content = st->pageContentH[page];
    int view = st->viewH;
    return std::max(0, content - view);
}

static void ApplyScroll(SettingsState* st, int page) {
    if (!st || page < 0 || page >= kTabCount || !st->pageViews[page]) return;
    int maxScroll = MaxScroll(st, page);
    if (st->pageScroll[page] < 0) st->pageScroll[page] = 0;
    if (st->pageScroll[page] > maxScroll) st->pageScroll[page] = maxScroll;
    int offset = st->pageScroll[page];
    for (const PageControlPos& pos : st->pageCtls[page]) {
        if (pos.hwnd) {
            SetWindowPos(pos.hwnd, nullptr, pos.x, pos.baseY - offset, 0, 0,
                         SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    InvalidateRect(st->pageViews[page], nullptr, TRUE);
}

static void ScrollPageBy(SettingsState* st, int page, int delta) {
    if (!st || page < 0 || page >= kTabCount) return;
    CloseDropdown(st);
    CloseCalendarPopup(st);
    st->pageScroll[page] += delta;
    ApplyScroll(st, page);
}

static void EnsureControlVisible(SettingsState* st, HWND ctl) {
    if (!st || !ctl || st->page < 0 || st->page >= kTabCount) return;
    int page = st->page;
    for (const PageControlPos& pos : st->pageCtls[page]) {
        if (pos.hwnd != ctl) continue;
        int offset = st->pageScroll[page];
        int top = pos.baseY - offset;
        int bottom = top + pos.h;
        bool changed = false;
        if (top < 0) {
            st->pageScroll[page] = pos.baseY;
            changed = true;
        } else if (bottom > st->viewH) {
            st->pageScroll[page] = pos.baseY + pos.h - st->viewH;
            changed = true;
        }
        if (changed) ApplyScroll(st, page);
        return;
    }
}

static void ScrollbarGeometry(SettingsState* st, int page,
                              RECT& track, RECT& thumb) {
    RECT cr{};
    GetClientRect(st->pageViews[page], &cr);
    track = RECT{cr.right - st->S(7), st->S(5),
                 cr.right - st->S(2), cr.bottom - st->S(5)};
    int trackH = track.bottom - track.top;
    int content = std::max(1, st->pageContentH[page]);
    int view = std::max(1, st->viewH);
    int thumbH = std::max(st->S(24), trackH * view / content);
    if (thumbH > trackH) thumbH = trackH;
    int maxScroll = MaxScroll(st, page);
    int y = track.top;
    if (maxScroll > 0) {
        y += (trackH - thumbH) * st->pageScroll[page] / maxScroll;
    }
    thumb = RECT{track.left, y, track.right, y + thumbH};
}

// ---------------------------------------------------------------------------
// 标签页
// ---------------------------------------------------------------------------
static void LayoutTabs(SettingsState* st, HWND hwnd) {
    st->tabRects.clear();
    HDC dc = GetDC(hwnd);
    if (!dc) return;
    HFONT old = (HFONT)SelectObject(dc, st->boldFont ? st->boldFont : st->font);
    int x = st->S(kPad);
    int y = st->S(kTabTop);
    int h = st->S(kTabH);
    for (int i = 0; i < kTabCount; ++i) {
        SIZE sz{};
        GetTextExtentPoint32W(dc, kTabNames[i],
                              static_cast<int>(wcslen(kTabNames[i])), &sz);
        int w = sz.cx + st->S(28);
        st->tabRects.push_back(RECT{x, y, x + w, y + h});
        x += w + st->S(6);
    }
    if (old) SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
}

static void ShowPage(SettingsState* st, int page) {
    if (!st || page < 0 || page >= kTabCount) return;
    CloseDropdown(st);
    CloseCalendarPopup(st);
    st->page = page;
    st->dragPage = -1;
    for (int i = 0; i < kTabCount; ++i) {
        if (st->pageViews[i]) {
            ShowWindow(st->pageViews[i], i == page ? SW_SHOW : SW_HIDE);
        }
    }
    ApplyScroll(st, page);
    if (st->dlg) InvalidateRect(st->dlg, nullptr, TRUE);
}

// 切页后把焦点落到该页首个常用控件，保证键盘操作仍然有效
static void FocusPageControl(SettingsState* st, int page) {
    if (!st) return;
    HWND target = nullptr;
    switch (page) {
        case PageBasic:     target = st->hEditTitle; break;
        case PageCountdown: target = st->hEditSize; break;
        case PageArticle:   target = st->hBtnArticle; break;
        default:            target = st->hBtnUpdate; break;
    }
    if (target && IsWindow(target) && IsWindowVisible(target)) {
        SetFocus(target);
        wchar_t cls[32] = {0};
        GetClassNameW(target, cls, 32);
        if (lstrcmpiW(cls, L"EDIT") == 0) {
            SendMessageW(target, EM_SETSEL, 0, -1);
        }
    }
}

// ---------------------------------------------------------------------------
// 自绘辅助
// ---------------------------------------------------------------------------
static void FillRound(HDC dc, RECT rc, int radius, COLORREF fill,
                      COLORREF border, int borderW = 1) {
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, borderW, border);
    HGDIOBJ ob = SelectObject(dc, br);
    HGDIOBJ op = SelectObject(dc, pen);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(dc, ob);
    SelectObject(dc, op);
    DeleteObject(br);
    DeleteObject(pen);
}

static void DrawCentered(HDC dc, RECT rc, const wchar_t* text,
                         HFONT font, COLORREF color) {
    HFONT old = font ? (HFONT)SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
}

static void PaintDialog(HWND hwnd, SettingsState* st) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    if (!dc) return;

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int w = cr.right;
    int h = cr.bottom;
    if (w <= 0 || h <= 0 || !st->brBg) { EndPaint(hwnd, &ps); return; }

    if (!st->paintDc) {
        st->paintDc = CreateCompatibleDC(dc);
        if (!st->paintDc) { EndPaint(hwnd, &ps); return; }
    }
    if (!st->paintBmp || st->paintW != w || st->paintH != h) {
        if (st->paintBmp) {
            SelectObject(st->paintDc, st->paintOldBmp);
            DeleteObject(st->paintBmp);
            st->paintBmp = nullptr;
        }
        st->paintBmp = CreateCompatibleBitmap(dc, w, h);
        if (!st->paintBmp) { EndPaint(hwnd, &ps); return; }
        st->paintOldBmp = SelectObject(st->paintDc, st->paintBmp);
        st->paintW = w;
        st->paintH = h;
    }
    HDC mem = st->paintDc;

    FillRect(mem, &cr, st->brBg);
    FillRound(mem, RECT{0, 0, w - 1, h - 1}, st->S(24), kBg, kChipBorder);

    for (int i = 0; i < kTabCount && i < static_cast<int>(st->tabRects.size()); ++i) {
        RECT r = st->tabRects[i];
        if (i == st->page) {
            RECT glow = r;
            InflateRect(&glow, st->S(2), st->S(2));
            FillRound(mem, glow, st->S(17), kAccentDim, kAccentDim);
            FillRound(mem, r, st->S(15), kAccent, kAccentHi);
            DrawCentered(mem, r, kTabNames[i], st->boldFont, RGB(255, 255, 255));
        } else if (i == st->hoverTab) {
            FillRound(mem, r, st->S(15), kChipBg, kChipBg);
            DrawCentered(mem, r, kTabNames[i], st->font, kLabel);
        } else {
            DrawCentered(mem, r, kTabNames[i], st->font, kMuted);
        }
    }

    HPEN penDiv = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ oldPen = SelectObject(mem, penDiv);
    MoveToEx(mem, st->S(kPad), st->S(kTabDivider), nullptr);
    LineTo(mem, w - st->S(kPad), st->S(kTabDivider));

    int btnY = h - st->S(kPad) - st->S(kBtnH);
    MoveToEx(mem, st->S(kPad), btnY - st->S(12), nullptr);
    LineTo(mem, w - st->S(kPad), btnY - st->S(12));
    SelectObject(mem, oldPen);
    DeleteObject(penDiv);

    SetBkMode(mem, TRANSPARENT);
    SetTextColor(mem, kMuted);
    HFONT oldFont = (HFONT)SelectObject(mem, st->font);
    RECT credit{st->S(kPad), btnY, w - st->S(kPad) - st->S(2 * kBtnW + 12),
                btnY + st->S(kBtnH)};
    if (credit.right > credit.left) {
        DrawTextW(mem, L"\u5236\u4f5c\uff1aBu Xin    \u90ae\u7bb1\uff1abuxin0912@qq.com",
                  -1, &credit,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                      | DT_END_ELLIPSIS);
    }
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

static void ReleasePaintBuffer(SettingsState* st) {
    if (!st) return;
    if (st->paintDc) {
        if (st->paintBmp) {
            SelectObject(st->paintDc, st->paintOldBmp);
            DeleteObject(st->paintBmp);
        }
        DeleteDC(st->paintDc);
    }
    st->paintDc = nullptr;
    st->paintBmp = nullptr;
    st->paintOldBmp = nullptr;
    st->paintW = 0;
    st->paintH = 0;
}

static void PaintAbout(HDC dc, SettingsState* st, int w, int h) {
    // 图标
    if (st->aboutIcon) {
        int iconSize = st->S(48);
        DrawIconEx(dc, st->S(8), st->S(10), st->aboutIcon,
                   iconSize, iconSize, 0, nullptr, DI_NORMAL);
    }

    // 名称 / 版本
    RECT nameRc{st->S(72), st->S(6), w - st->S(8), st->S(34)};
    HFONT old = (HFONT)SelectObject(dc, st->aboutTitleFont);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kValueText);
    DrawTextW(dc, APP_NAME, -1, &nameRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);

    std::wstring version = L"\u7248\u672c " APP_VERSION_STRING;
    RECT verRc{st->S(72), st->S(36), w - st->S(8), st->S(58)};
    old = (HFONT)SelectObject(dc, st->font);
    SetTextColor(dc, kMuted);
    DrawTextW(dc, version.c_str(), -1, &verRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 简介
    RECT descRc{st->S(8), st->S(72), w - st->S(8), st->S(118)};
    SetTextColor(dc, kLabel);
    DrawTextW(dc,
              L"\u4e00\u4e2a\u8f7b\u91cf\u7684\u684c\u9762\u5012\u8ba1\u65f6\u5de5\u5177\uff0c"
              L"\u652f\u6301\u4e00\u8a00\u3001\u4e00\u6587\u4e0e DLL \u663e\u793a\u6a21\u5757\u3002",
              -1, &descRc,
              DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    // 分隔线
    HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ op = SelectObject(dc, pen);
    MoveToEx(dc, st->S(8), st->S(128), nullptr);
    LineTo(dc, w - st->S(8), st->S(128));
    SelectObject(dc, op);
    DeleteObject(pen);

    // 更新地址（与“获取更新”按钮垂直对齐，按钮由 OnCreate 创建）
    RECT urlRc{st->S(160), st->S(140), w - st->S(8), st->S(170)};
    SetTextColor(dc, kMuted);
    DrawTextW(dc, APP_UPDATE_URL, -1, &urlRc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                  | DT_END_ELLIPSIS);
    SelectObject(dc, old);
}

static void PaintPage(HWND hwnd, SettingsState* st, int page, PageViewData* data) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    if (!dc) return;
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (cr.right <= 0 || cr.bottom <= 0 || !data) {
        EndPaint(hwnd, &ps);
        return;
    }

    if (!data->mem) {
        data->mem = CreateCompatibleDC(dc);
        if (!data->mem) { EndPaint(hwnd, &ps); return; }
    }
    if (!data->bmp || data->memW != cr.right || data->memH != cr.bottom) {
        if (data->bmp) {
            SelectObject(data->mem, data->oldBmp);
            DeleteObject(data->bmp);
            data->bmp = nullptr;
        }
        data->bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        if (!data->bmp) { EndPaint(hwnd, &ps); return; }
        data->oldBmp = SelectObject(data->mem, data->bmp);
        data->memW = cr.right;
        data->memH = cr.bottom;
    }
    HDC mem = data->mem;
    FillRect(mem, &cr, st->brBg);

    if (page == PageAbout) {
        PaintAbout(mem, st, cr.right, cr.bottom);
    } else {
        int offset = st->pageScroll[page];
        HWND focus = GetFocus();

        for (const ChipSlot& chip : st->chips[page]) {
            RECT r = chip.rc;
            r.top -= offset;
            r.bottom -= offset;
            if (r.bottom < 0 || r.top > cr.bottom) continue;
            bool focused = chip.child && chip.child == focus;
            FillRound(mem, r, st->S(8), kChipBg,
                      focused ? kAccent : kChipBorder);
        }

        HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
        HGDIOBJ op = SelectObject(mem, pen);
        for (int y : st->rowDividerY[page]) {
            int yy = y - offset;
            if (yy < 0 || yy > cr.bottom) continue;
            MoveToEx(mem, 0, yy, nullptr);
            LineTo(mem, cr.right, yy);
        }
        SelectObject(mem, op);
        DeleteObject(pen);

        if (MaxScroll(st, page) > 0) {
            RECT track{}, thumb{};
            ScrollbarGeometry(st, page, track, thumb);
            HBRUSH tr = CreateSolidBrush(RGB(32, 32, 40));
            FillRect(mem, &track, tr);
            DeleteObject(tr);
            // 使用更亮的滑块，明确提示页面下方还有内容
            FillRound(mem, thumb, st->S(2), RGB(92, 92, 112), RGB(92, 92, 112));
        }
    }

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    EndPaint(hwnd, &ps);
}

// 自绘按钮
static void DrawOwnerButton(SettingsState* st, DRAWITEMSTRUCT* dis) {
    if (!st || !dis || dis->CtlType != ODT_BUTTON) return;

    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    int radius = std::min(st->S(8), static_cast<int>(rc.bottom - rc.top) / 2);
    HDC dc = dis->hDC;

    if (dis->CtlID == IDC_BTN_SAVE) {
        FillRound(dc, rc, radius, pressed ? RGB(48, 74, 196) : kAccent, kAccentHi);
        DrawCentered(dc, rc, L"\u4fdd\u5b58", st->boldFont, RGB(255, 255, 255));
        return;
    }
    if (dis->CtlID == IDC_BTN_CANCEL) {
        FillRound(dc, rc, radius, pressed ? RGB(48, 48, 60) : kChipBg, kChipBorder);
        DrawCentered(dc, rc, L"\u53d6\u6d88", st->font, kLabel);
        return;
    }
    if (dis->CtlID == IDC_BTN_UPDATE) {
        FillRound(dc, rc, radius, pressed ? RGB(48, 74, 196) : kAccent, kAccentHi);
        DrawCentered(dc, rc, L"\u83b7\u53d6\u66f4\u65b0", st->boldFont, RGB(255, 255, 255));
        return;
    }
    if (dis->CtlID == IDC_BTN_CALENDAR) {
        FillRound(dc, rc, radius, pressed ? RGB(48, 48, 60) : kChipBg,
                  focused ? kAccent : kChipBorder);
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, st->S(1)), kLabel);
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, cx - st->S(4), cy - st->S(2), nullptr);
        LineTo(dc, cx, cy + st->S(2));
        LineTo(dc, cx + st->S(4), cy - st->S(2));
        SelectObject(dc, op);
        DeleteObject(pen);
        return;
    }
    if (dis->CtlID == IDC_BTN_STEP_MINUS || dis->CtlID == IDC_BTN_STEP_PLUS) {
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        FillRound(dc, rc, radius, pressed ? RGB(46, 46, 57) : kChipBg, kChipBorder);
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        int arm = st->S(4);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, st->S(1)), enabled ? kLabel : kMuted);
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, cx - arm, cy, nullptr);
        LineTo(dc, cx + arm + 1, cy);
        if (dis->CtlID == IDC_BTN_STEP_PLUS) {
            MoveToEx(dc, cx, cy - arm, nullptr);
            LineTo(dc, cx, cy + arm + 1);
        }
        SelectObject(dc, op);
        DeleteObject(pen);
        return;
    }
    if (dis->CtlID == IDC_BTN_BG_COLOR || dis->CtlID == IDC_BTN_TEXT_COLOR ||
        dis->CtlID == IDC_BTN_ART_BG_COLOR || dis->CtlID == IDC_BTN_ART_TEXT_COLOR) {
        COLORREF color = kChipBg;
        if (dis->CtlID == IDC_BTN_BG_COLOR) color = st->cfg.backgroundColor;
        else if (dis->CtlID == IDC_BTN_TEXT_COLOR) color = st->cfg.textColor;
        else if (dis->CtlID == IDC_BTN_ART_BG_COLOR) color = st->cfg.articleBackgroundColor;
        else color = st->cfg.articleTextColor;

        FillRound(dc, rc, radius, color,
                  focused ? kAccent : (pressed ? kAccentHi : kChipBorder));
        std::string hex = utils::ColorToHex(color);
        std::wstring whex(hex.begin(), hex.end());
        int lum = (GetRValue(color) * 299 + GetGValue(color) * 587 +
                   GetBValue(color) * 114) / 1000;
        DrawCentered(dc, rc, whex.c_str(), st->font,
                     lum > 140 ? RGB(24, 24, 30) : RGB(245, 245, 250));
        return;
    }
    if (dis->CtlID == IDC_BTN_TEMP_CLEAN) {
        FillRound(dc, rc, radius, pressed ? RGB(48, 48, 60) : kChipBg,
                  focused ? kAccent : kChipBorder);
        DrawCentered(dc, rc, L"\u7acb\u5373\u6e05\u7406", st->font, kLabel);
        return;
    }
    if (dis->CtlID == IDC_BTN_MODULE_CONFIG) {
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        FillRound(dc, rc, radius, pressed ? RGB(48, 48, 60) : kChipBg,
                  focused ? kAccent : kChipBorder);
        DrawCentered(dc, rc, L"\u6a21\u5757\u8bbe\u7f6e", st->font,
                     enabled ? kLabel : kMuted);
        return;
    }
    if (IsToggleId(dis->CtlID)) {
        bool on = ToggleRef(st, dis->CtlID);
        COLORREF fill = on ? (pressed ? RGB(48, 60, 110) : kOnBg) : kChipBg;
        COLORREF border = on ? kOnBorder : (focused ? kAccent : kChipBorder);
        FillRound(dc, rc, radius, fill, border);
        DrawCentered(dc, rc, on ? L"\u5f00" : L"\u5173", st->font,
                     on ? kValueText : kMuted);
        return;
    }
    if (FindDropdownByTrigger(st, dis->hwndItem)) {
        bool open = (st->hDropdownTrigger == dis->hwndItem);
        FillRound(dc, rc, radius, pressed ? RGB(46, 46, 57) : kChipBg,
                  open ? kAccentHi : (focused ? kAccent : kChipBorder));

        wchar_t text[256] = {0};
        GetWindowTextW(dis->hwndItem, text, 256);
        RECT trc = rc;
        trc.left += st->S(10);
        trc.right -= st->S(24);
        HFONT old = (HFONT)SelectObject(dc, st->font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kValueText);
        DrawTextW(dc, text, -1, &trc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                      | DT_END_ELLIPSIS);
        if (old) SelectObject(dc, old);

        int cx = rc.right - st->S(13);
        int cy = (rc.top + rc.bottom) / 2;
        HPEN pen = CreatePen(PS_SOLID, std::max(1, st->S(1)), kMuted);
        HGDIOBJ op = SelectObject(dc, pen);
        MoveToEx(dc, cx - st->S(3), cy - st->S(2), nullptr);
        LineTo(dc, cx, cy + st->S(2));
        LineTo(dc, cx + st->S(3), cy - st->S(2));
        SelectObject(dc, op);
        DeleteObject(pen);
        return;
    }

    wchar_t text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);
    FillRound(dc, rc, radius, pressed ? RGB(46, 46, 57) : kChipBg,
              focused ? kAccent : kChipBorder);
    DrawCentered(dc, rc, text, st->font, kValueText);
}

// ---------------------------------------------------------------------------
// 下拉浮层
// ---------------------------------------------------------------------------
static bool RegisterDropdownClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kDropdownClass, &exist) != 0) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = DropdownWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kDropdownClass;
    return RegisterClassExW(&wc) != 0;
}

static LRESULT CALLBACK DropdownWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    DropdownPopupData* data = reinterpret_cast<DropdownPopupData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        data = reinterpret_cast<DropdownPopupData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
    }

    switch (msg) {
        case WM_CREATE: {
            if (!data) return -1;
            RECT cr{};
            GetClientRect(hwnd, &cr);
            SetWindowRgn(hwnd,
                CreateRoundRectRgn(0, 0, cr.right + 1, cr.bottom + 1,
                                   data->st->S(12), data->st->S(12)), TRUE);
            int pad = data->st->S(5);
            data->list = CreateWindowExW(0, L"LISTBOX", L"",
                WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS
                    | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                pad, pad, cr.right - 2 * pad, cr.bottom - 2 * pad,
                hwnd, (HMENU)(INT_PTR)IDC_DROPDOWN_LIST, InstOf(hwnd), nullptr);
            if (!data->list) return -1;
            SendMessageW(data->list, WM_SETFONT, (WPARAM)data->st->font, TRUE);
            SendMessageW(data->list, LB_SETITEMHEIGHT, 0, data->st->S(22));
            DropdownDef* def = FindDropdownByKind(data->st, data->kind);
            if (def) {
                for (const auto& item : def->items) {
                    SendMessageW(data->list, LB_ADDSTRING, 0, (LPARAM)item.c_str());
                }
                int top = def->current - 3;
                if (top < 0) top = 0;
                SendMessageW(data->list, LB_SETTOPINDEX, top, 0);
            }
            return 0;
        }

        case WM_SHOWWINDOW:
            if (wp && data && data->list) SetFocus(data->list);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC dc = BeginPaint(hwnd, &ps);
            RECT cr{};
            GetClientRect(hwnd, &cr);
            if (data && data->st->brChip) FillRect(dc, &cr, data->st->brChip);
            HPEN pen = CreatePen(PS_SOLID, 1, kChipBorder);
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Rectangle(dc, 0, 0, cr.right, cr.bottom);
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(pen);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_CTLCOLORLISTBOX:
            if (data && data->st->brChip) {
                HDC dc = reinterpret_cast<HDC>(wp);
                SetBkColor(dc, kChipBg);
                SetTextColor(dc, kValueText);
                return reinterpret_cast<LRESULT>(data->st->brChip);
            }
            break;

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!data || !dis || dis->CtlType != ODT_LISTBOX) break;
            int index = static_cast<int>(dis->itemID);
            if (index < 0) return TRUE;

            DropdownDef* def = FindDropdownByKind(data->st, data->kind);
            const wchar_t* text = L"";
            if (def && index < static_cast<int>(def->items.size())) {
                text = def->items[index].c_str();
            }
            bool selected = (dis->itemState & ODS_SELECTED) != 0;
            bool current = def && index == def->current;
            COLORREF bg = selected ? kAccent : (current ? RGB(44, 52, 78) : kChipBg);
            HBRUSH br = CreateSolidBrush(bg);
            FillRect(dis->hDC, &dis->rcItem, br);
            DeleteObject(br);

            RECT trc = dis->rcItem;
            trc.left += data->st->S(10);
            trc.right -= data->st->S(8);
            HFONT old = (HFONT)SelectObject(dis->hDC, data->st->font);
            SetBkMode(dis->hDC, TRANSPARENT);
            SetTextColor(dis->hDC,
                         selected ? RGB(255, 255, 255)
                                  : (current ? RGB(235, 240, 255) : kValueText));
            DrawTextW(dis->hDC, text, -1, &trc,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                          | DT_END_ELLIPSIS);
            if (old) SelectObject(dis->hDC, old);
            return TRUE;
        }

        case WM_COMMAND: {
            if (!data) break;
            if (LOWORD(wp) == IDC_DROPDOWN_LIST && HIWORD(wp) == LBN_SELCHANGE) {
                int index = static_cast<int>(
                    SendMessageW(data->list, LB_GETCURSEL, 0, 0));
                DropdownDef* def = FindDropdownByKind(data->st, data->kind);
                if (def && index >= 0) {
                    HWND trigger = def->trigger;
                    ApplyDropdownSelection(data->st, def, index);
                    if (trigger && IsWindow(trigger)) SetFocus(trigger);
                    PostMessageW(hwnd, WM_CLOSE, 0, 0);
                } else {
                    CloseDropdown(data->st);
                }
                return 0;
            }
            break;
        }

        case WM_LBUTTONDOWN:
            return 0;

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (data) {
                if (data->st->hDropdown == hwnd) {
                    data->st->hDropdown = nullptr;
                    data->st->hDropdownTrigger = nullptr;
                }
                delete data;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void CloseDropdown(SettingsState* st) {
    if (!st) return;
    if (st->hDropdown) {
        HWND h = st->hDropdown;
        st->hDropdown = nullptr;
        st->hDropdownTrigger = nullptr;
        DestroyWindow(h);
    }
}

static void OpenDropdown(SettingsState* st, DropdownDef* def) {
    if (!st || !st->dlg || !def || !def->trigger) return;
    if (st->hDropdown && st->hDropdownTrigger == def->trigger) {
        CloseDropdown(st);
        return;
    }
    CloseDropdown(st);
    CloseCalendarPopup(st);

    RECT tr{};
    GetWindowRect(def->trigger, &tr);
    MapWindowPoints(HWND_DESKTOP, st->dlg, reinterpret_cast<POINT*>(&tr), 2);

    RECT cr{};
    GetClientRect(st->dlg, &cr);
    int footerY = cr.bottom - st->S(kPad) - st->S(kBtnH) - st->S(12);
    int itemH = st->S(22);
    int count = static_cast<int>(def->items.size());
    if (count <= 0) return;

    int visible = std::min(count, 9);
    int h = visible * itemH + st->S(10);
    int w = std::max(static_cast<int>(tr.right - tr.left), st->S(180));
    int maxW = cr.right - 2 * st->S(kPad);
    if (w > maxW) w = maxW;

    int x = tr.right - w;
    if (x < st->S(kPad)) x = st->S(kPad);
    if (x + w > cr.right - st->S(kPad)) x = cr.right - st->S(kPad) - w;

    int y = tr.bottom + st->S(4);
    if (y + h > footerY) {
        int above = tr.top - st->S(4) - h;
        if (above >= st->S(kPad)) {
            y = above;
        } else {
            h = footerY - y;
            if (h < itemH + st->S(10)) {
                h = itemH + st->S(10);
                y = std::max(st->S(kPad), footerY - h);
            }
        }
    }
    if (h < itemH + st->S(10)) return;

    auto* data = new (std::nothrow) DropdownPopupData();
    if (!data) return;
    data->st = st;
    data->kind = def->kind;

    HWND pop = CreateWindowExW(0, kDropdownClass, L"",
        WS_CHILD | WS_CLIPSIBLINGS, x, y, w, h,
        st->dlg, nullptr, InstOf(st->dlg), data);
    if (!pop) return;

    st->hDropdown = pop;
    st->hDropdownTrigger = def->trigger;
    SetWindowPos(pop, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// 月历浮层
// ---------------------------------------------------------------------------
static void CloseCalendarPopup(SettingsState* st) {
    if (st->hCalPrevYear) {
        DestroyWindow(st->hCalPrevYear);
        st->hCalPrevYear = nullptr;
    }
    if (st->hCalNextYear) {
        DestroyWindow(st->hCalNextYear);
        st->hCalNextYear = nullptr;
    }
    if (st->hCalPopup) {
        HWND popup = st->hCalPopup;
        st->hCalPopup = nullptr;
        DestroyWindow(popup);
    }
}

static void AdjustCalendarYear(SettingsState* st, int delta) {
    if (!st || !st->hCalPopup) return;
    int year = st->calYear + delta;
    if (year < 1753) year = 1753;
    if (year > 9999) year = 9999;
    st->calYear = year;
    InvalidateRect(st->hCalPopup, nullptr, TRUE);
    SetFocus(st->hCalPopup);
}

// ---------------------------------------------------------------------------
// 自绘月历：原生 MonthCal 在深色主题下会出现文字重叠，这里完全自绘
// ---------------------------------------------------------------------------
static bool CalendarDateForCell(int year, int month, int index,
                                int& outYear, int& outMonth, int& outDay) {
    std::tm first{};
    first.tm_year = year - 1900;
    first.tm_mon  = month - 1;
    first.tm_mday = 1;
    first.tm_hour = 12; // 用中午避免夏令时造成日期偏移
    first.tm_isdst = -1;
    std::time_t t = std::mktime(&first);
    if (t == static_cast<std::time_t>(-1)) return false;
    std::tm w{};
    localtime_s(&w, &t);
    int weekday = (w.tm_wday == 0) ? 7 : w.tm_wday; // 1=周一
    t -= static_cast<std::time_t>((weekday - 1) * 86400);
    t += static_cast<std::time_t>(index) * 86400;
    std::tm out{};
    localtime_s(&out, &t);
    outYear = out.tm_year + 1900;
    outMonth = out.tm_mon + 1;
    outDay = out.tm_mday;
    return true;
}

static void CalendarMetrics(SettingsState* st, int clientW, int clientH,
                            int& pad, int& titleH, int& weekH, int& cellH,
                            int& cellW, int& gridTop) {
    pad = st->S(6);
    titleH = st->S(28);
    weekH = st->S(22);
    cellH = st->S(24);
    cellW = (clientW - 2 * pad) / 7;
    if (cellW < 1) cellW = 1;
    gridTop = pad + titleH + weekH;
    (void)clientH;
}

static void PaintCalendar(HWND hwnd, SettingsState* st) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) { EndPaint(hwnd, &ps); return; }

    FillRect(dc, &cr, st->brBg);
    FillRound(dc, RECT{0, 0, cr.right - 1, cr.bottom - 1}, st->S(14),
              kBg, kChipBorder);

    int pad, titleH, weekH, cellH, cellW, gridTop;
    CalendarMetrics(st, cr.right, cr.bottom, pad, titleH, weekH, cellH,
                    cellW, gridTop);

    // 标题：年月 + 月份切换箭头
    wchar_t title[32];
    swprintf_s(title, L"%d\u5e74%d\u6708", st->calYear, st->calMonth);
    RECT titleRc{pad, pad, cr.right - pad, pad + titleH};
    DrawCentered(dc, titleRc, title, st->boldFont, kValueText);
    DrawCentered(dc, RECT{pad, pad, pad + st->S(34), pad + titleH},
                 L"\u2039", st->boldFont, kMuted);
    DrawCentered(dc, RECT{cr.right - pad - st->S(34), pad,
                          cr.right - pad, pad + titleH},
                 L"\u203a", st->boldFont, kMuted);

    // 星期表头
    static const wchar_t* kWeek[7] = {
        L"\u5468\u4e00", L"\u5468\u4e8c", L"\u5468\u4e09", L"\u5468\u56db",
        L"\u5468\u4e94", L"\u5468\u516d", L"\u5468\u65e5",
    };
    for (int c = 0; c < 7; ++c) {
        RECT rc{pad + c * cellW, pad + titleH,
                pad + (c + 1) * cellW, pad + titleH + weekH};
        DrawCentered(dc, rc, kWeek[c], st->font, kMuted);
    }

    std::time_t now = std::time(nullptr);
    std::tm today{};
    localtime_s(&today, &now);

    for (int i = 0; i < 42; ++i) {
        int y, m, d;
        if (!CalendarDateForCell(st->calYear, st->calMonth, i, y, m, d)) continue;
        int row = i / 7;
        int col = i % 7;
        RECT cell{pad + col * cellW, gridTop + row * cellH,
                  pad + (col + 1) * cellW, gridTop + (row + 1) * cellH};
        bool inMonth = (y == st->calYear && m == st->calMonth);
        bool selected = st->calHasSelection &&
                        y == st->calSelYear && m == st->calSelMonth &&
                        d == st->calSelDay;
        bool isToday = (today.tm_year + 1900 == y) &&
                       (today.tm_mon + 1 == m) && today.tm_mday == d;

        if (selected) {
            RECT fill = cell;
            InflateRect(&fill, -st->S(1), -st->S(1));
            FillRound(dc, fill, st->S(6), kAccent, kAccentHi);
        } else if (isToday) {
            RECT outline = cell;
            InflateRect(&outline, -st->S(1), -st->S(1));
            HPEN pen = CreatePen(PS_SOLID, 1, kAccentHi);
            HGDIOBJ op = SelectObject(dc, pen);
            HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
            RoundRect(dc, outline.left, outline.top, outline.right,
                      outline.bottom, st->S(6), st->S(6));
            SelectObject(dc, op);
            SelectObject(dc, ob);
            DeleteObject(pen);
        }

        wchar_t text[8];
        swprintf_s(text, L"%d", d);
        DrawCentered(dc, cell, text, st->font,
                     selected ? RGB(255, 255, 255)
                              : (inMonth ? kValueText : kMuted));
    }

    EndPaint(hwnd, &ps);
}

static bool RegisterCalendarClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kCalendarClass, &exist) != 0) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = CalendarWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kCalendarClass;
    return RegisterClassExW(&wc) != 0;
}

static LRESULT CALLBACK CalendarWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    CalendarViewData* data = reinterpret_cast<CalendarViewData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        data = reinterpret_cast<CalendarViewData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
    }
    SettingsState* st = data ? data->st : nullptr;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT:
            if (st) PaintCalendar(hwnd, st);
            return 0;
        case WM_LBUTTONDOWN: {
            if (!st) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT cr{};
            GetClientRect(hwnd, &cr);
            int pad, titleH, weekH, cellH, cellW, gridTop;
            CalendarMetrics(st, cr.right, cr.bottom, pad, titleH, weekH,
                            cellH, cellW, gridTop);

            if (pt.y < pad + titleH) {
                if (pt.x < pad + st->S(34)) {
                    st->calMonth -= 1;
                    if (st->calMonth < 1) { st->calMonth = 12; st->calYear -= 1; }
                } else if (pt.x > cr.right - pad - st->S(34)) {
                    st->calMonth += 1;
                    if (st->calMonth > 12) { st->calMonth = 1; st->calYear += 1; }
                }
                if (st->calYear < 1753) st->calYear = 1753;
                if (st->calYear > 9999) st->calYear = 9999;
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
            if (pt.y >= gridTop) {
                int col = (pt.x - pad) / cellW;
                int row = (pt.y - gridTop) / cellH;
                if (col >= 0 && col < 7 && row >= 0 && row < 6) {
                    int y, m, d;
                    if (CalendarDateForCell(st->calYear, st->calMonth,
                                            row * 7 + col, y, m, d) &&
                        y == st->calYear && m == st->calMonth) {
                        st->calSelYear = y;
                        st->calSelMonth = m;
                        st->calSelDay = d;
                        st->calHasSelection = true;
                        wchar_t dateText[32];
                        swprintf_s(dateText, L"%04d-%02d-%02d", y, m, d);
                        if (st->hEditDate && IsWindow(st->hEditDate)) {
                            SetWindowTextW(st->hEditDate, dateText);
                        }
                        PostMessageW(st->dlg, WM_APP_CLOSE_CALENDAR, 0, 0);
                    }
                }
                return 0;
            }
            return 0;
        }
        case WM_DESTROY:
            if (data) {
                delete data;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void OpenCalendarPopup(SettingsState* st) {
    if (!st || !st->dlg || !st->hBtnCalendar) return;
    if (st->hCalPopup) {
        CloseCalendarPopup(st);
        return;
    }
    CloseDropdown(st);

    // 解析日期胶囊文本，确定当前视图与选中日期
    wchar_t dateBuf[64] = {0};
    GetWindowTextW(st->hEditDate, dateBuf, 64);
    int yy = 0, mm = 0, dd = 0;
    if (swscanf(dateBuf, L"%d-%d-%d", &yy, &mm, &dd) != 3 ||
        mm < 1 || mm > 12 || dd < 1 || dd > 31) {
        yy = st->cfg.targetTime.tm_year + 1900;
        mm = st->cfg.targetTime.tm_mon + 1;
        dd = st->cfg.targetTime.tm_mday;
    }
    if (yy < 1753) yy = 1753;
    if (yy > 9999) yy = 9999;
    st->calYear = yy;
    st->calMonth = mm;
    st->calSelYear = yy;
    st->calSelMonth = mm;
    st->calSelDay = dd;
    st->calHasSelection = true;

    int cw = st->S(250);
    int ch = st->S(6 + 28 + 22 + 6 * 24 + 6); // 206
    int barH = st->S(24);
    int gap = st->S(4);

    RECT anchor{};
    GetWindowRect(st->hBtnCalendar, &anchor);
    MapWindowPoints(HWND_DESKTOP, st->dlg, reinterpret_cast<POINT*>(&anchor), 2);

    RECT cr{};
    GetClientRect(st->dlg, &cr);
    int footerY = cr.bottom - st->S(kPad) - st->S(kBtnH) - st->S(12);

    int blockH = barH + gap + ch;
    int x = anchor.right - cw;
    int y = anchor.bottom + st->S(4);
    if (y + blockH > footerY) {
        int above = anchor.top - st->S(4) - blockH;
        if (above >= st->S(kPad)) y = above;
    }
    if (x < st->S(kPad)) x = st->S(kPad);
    if (x + cw > cr.right - st->S(kPad)) x = cr.right - st->S(kPad) - cw;
    if (y < st->S(kPad)) y = st->S(kPad);

    int btnGap = st->S(4);
    int bw = (cw - btnGap) / 2;
    st->hCalPrevYear = CreateWindowExW(0, L"BUTTON",
        L"\u2039 \u4e0a\u4e00\u5e74",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, y, bw, barH, st->dlg,
        (HMENU)(INT_PTR)IDC_CAL_PREV_YEAR, InstOf(st->dlg), nullptr);
    st->hCalNextYear = CreateWindowExW(0, L"BUTTON",
        L"\u4e0b\u4e00\u5e74 \u203a",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x + bw + btnGap, y, cw - bw - btnGap, barH, st->dlg,
        (HMENU)(INT_PTR)IDC_CAL_NEXT_YEAR, InstOf(st->dlg), nullptr);
    if (st->font) {
        SendMessageW(st->hCalPrevYear, WM_SETFONT, (WPARAM)st->font, TRUE);
        SendMessageW(st->hCalNextYear, WM_SETFONT, (WPARAM)st->font, TRUE);
    }

    CalendarViewData* viewData = new (std::nothrow) CalendarViewData();
    if (!viewData) return;
    viewData->st = st;
    HWND cal = CreateWindowExW(0, kCalendarClass, L"",
        WS_CHILD | WS_CLIPSIBLINGS | WS_TABSTOP,
        x, y + barH + gap, cw, ch, st->dlg,
        (HMENU)(INT_PTR)IDC_CAL_POPUP, InstOf(st->dlg), viewData);
    if (!cal) {
        delete viewData;
        return;
    }

    st->hCalPopup = cal;
    SetWindowPos(cal, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetFocus(cal);
}

// ---------------------------------------------------------------------------
// 页面子窗口
// ---------------------------------------------------------------------------
static bool RegisterPageClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kPageClass, &exist) != 0) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = PageWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kPageClass;
    return RegisterClassExW(&wc) != 0;
}

static LRESULT CALLBACK PageWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PageViewData* data = reinterpret_cast<PageViewData*>(
        GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        data = reinterpret_cast<PageViewData*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(data));
    }
    SettingsState* st = data ? data->st : nullptr;
    int page = data ? data->page : 0;

    switch (msg) {
        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            if (st) PaintPage(hwnd, st, page, data);
            return 0;

        case WM_COMMAND:
            return SendMessageW(GetParent(hwnd), WM_COMMAND, wp, lp);
        case WM_DRAWITEM:
            return SendMessageW(GetParent(hwnd), WM_DRAWITEM, wp, lp);
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX:
            return SendMessageW(GetParent(hwnd), msg, wp, lp);

        case WM_MOUSEWHEEL: {
            if (!st) break;
            int delta = GET_WHEEL_DELTA_WPARAM(wp);
            int step = st->S(48) * (-delta) / WHEEL_DELTA;
            if (step == 0) step = delta > 0 ? -st->S(24) : st->S(24);
            ScrollPageBy(st, page, step);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            if (!st) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT cr{};
            GetClientRect(hwnd, &cr);
            if (MaxScroll(st, page) > 0 && pt.x >= cr.right - st->S(10)) {
                RECT track{}, thumb{};
                ScrollbarGeometry(st, page, track, thumb);
                if (PtInRect(&thumb, pt)) {
                    st->dragPage = page;
                    st->dragStartY = pt.y;
                    st->dragStartScroll = st->pageScroll[page];
                } else {
                    int maxScroll = MaxScroll(st, page);
                    int thumbH = thumb.bottom - thumb.top;
                    int usable = (track.bottom - track.top) - thumbH;
                    if (usable > 0) {
                        int rel = pt.y - track.top - thumbH / 2;
                        int scroll = rel * maxScroll / usable;
                        st->pageScroll[page] = std::max(0, std::min(maxScroll, scroll));
                        ApplyScroll(st, page);
                    }
                }
                SetCapture(hwnd);
                return 0;
            }
            CloseDropdown(st);
            CloseCalendarPopup(st);
            return 0;
        }

        case WM_MOUSEMOVE: {
            if (!st || st->dragPage != page) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT track{}, thumb{};
            ScrollbarGeometry(st, page, track, thumb);
            int thumbH = thumb.bottom - thumb.top;
            int usable = (track.bottom - track.top) - thumbH;
            int maxScroll = MaxScroll(st, page);
            if (usable > 0 && maxScroll > 0) {
                int delta = pt.y - st->dragStartY;
                int scroll = st->dragStartScroll + delta * maxScroll / usable;
                st->pageScroll[page] = std::max(0, std::min(maxScroll, scroll));
                ApplyScroll(st, page);
            }
            return 0;
        }

        case WM_LBUTTONUP:
            if (st && st->dragPage == page) {
                st->dragPage = -1;
                ReleaseCapture();
            }
            return 0;

        case WM_DESTROY:
            if (data) {
                if (data->mem) {
                    if (data->bmp) {
                        SelectObject(data->mem, data->oldBmp);
                        DeleteObject(data->bmp);
                    }
                    DeleteDC(data->mem);
                }
                delete data;
                SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// 创建控件
// ---------------------------------------------------------------------------
static void OnCreate(HWND hwnd, SettingsState* st) {
    st->brBg = CreateSolidBrush(kBg);
    st->brChip = CreateSolidBrush(kChipBg);
    st->page = PageBasic;
    st->optStart = st->cfg.includeStartDay;
    st->optArticle = st->cfg.articleEnabled;
    st->optArtTop = st->cfg.articleIsTop;
    st->optTopMost = st->cfg.isTop;
    st->optAutoStart = st->cfg.autoStart;
    st->optHkBold = st->cfg.hitokotoBold;

    st->aboutIcon = (HICON)LoadImageW(InstOf(hwnd),
        MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
        st->S(48), st->S(48), LR_DEFAULTCOLOR);
    if (!st->aboutIcon) {
        st->aboutIcon = (HICON)LoadImageW(InstOf(hwnd),
            MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    }

    LayoutTabs(st, hwnd);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    int footerTop = cr.bottom - st->S(kPad) - st->S(kBtnH) - st->S(12);
    st->contentW = cr.right - 2 * st->S(kPad);
    st->viewH = footerTop - st->S(kContentTop);
    if (st->viewH < st->S(80)) st->viewH = st->S(80);

    // 页面容器
    for (int i = 0; i < kTabCount; ++i) {
        PageViewData* data = new (std::nothrow) PageViewData();
        if (!data) continue;
        data->st = st;
        data->page = i;
        st->pageViews[i] = CreateWindowExW(WS_EX_CONTROLPARENT, kPageClass, L"",
            WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
            st->S(kPad), st->S(kContentTop), st->contentW, st->viewH,
            hwnd, nullptr, InstOf(hwnd), data);
        if (!st->pageViews[i]) delete data;
        else st->pageContentH[i] = st->viewH;
    }

    const int rowH = st->S(kRowH);
    const int contentW = st->contentW;
    // 右侧预留滚动条宽度，避免滑块压住最右列控件
    const int slotRight = contentW - st->S(10);
    int y = 0;

    auto makeSlots = [&](int rowY, std::initializer_list<int> widths) {
        std::vector<RECT> out;
        int total = 0;
        int n = static_cast<int>(widths.size());
        for (int w : widths) total += st->S(w);
        if (n > 1) total += st->S(6) * (n - 1);
        int x = slotRight - total;
        for (int w : widths) {
            int cw = st->S(w);
            out.push_back(RECT{x, rowY, x + cw, rowY + rowH});
            x += cw + st->S(6);
        }
        return out;
    };

    auto label = [&](const wchar_t* text, int rowY, int page) -> HWND {
        return CreateLabel(st, page, text, 0, rowY, slotRight);
    };

    auto section = [&](int page, int& yy, const wchar_t* text) {
        if (yy > 0) {
            auto& lines = st->rowDividerY[page];
            // 上一行末尾已经画过分隔线时不再重复添加，避免出现双线
            bool close = !lines.empty() &&
                         std::abs(lines.back() - yy) <= st->S(5);
            if (!close) lines.push_back(yy);
            yy += st->S(10);
        }
        HWND h = CreateLabel(st, page, text, 0, yy, slotRight);
        st->sectionLabels.push_back(h);
        yy += st->S(28);
    };

    auto unit = [&](int page, RECT rc, const wchar_t* text) {
        CreateChild(st, page, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE, 0,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            0, nullptr, false);
    };

    auto edit = [&](int page, RECT slot, const wchar_t* text, DWORD style,
                    int id) -> HWND {
        st->chips[page].push_back(ChipSlot{nullptr, slot});
        int ix = st->S(8);
        int iy = st->S(3);
        HWND h = CreateChild(st, page, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | style, 0,
            slot.left + ix, slot.top + iy,
            (slot.right - slot.left) - 2 * ix,
            (slot.bottom - slot.top) - 2 * iy,
            id, nullptr);
        st->chips[page].back().child = h;
        return h;
    };

    auto dropdown = [&](int page, RECT slot, DropdownKind kind,
                        std::vector<std::wstring> items, int current) -> HWND {
        if (items.empty()) items.push_back(L"");
        if (current < 0 || current >= static_cast<int>(items.size())) current = 0;
        HWND btn = CreateChild(st, page, L"BUTTON", items[current].c_str(),
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.left, slot.top, slot.right - slot.left, slot.bottom - slot.top,
            0, nullptr);
        DropdownDef def;
        def.trigger = btn;
        def.kind = kind;
        def.items = std::move(items);
        def.current = current;
        st->dropdowns.push_back(std::move(def));
        return btn;
    };

    auto stepper = [&](int page, RECT slot, const wchar_t* value, int id,
                       int lo, int hi, DWORD editStyle) -> HWND {
        st->chips[page].push_back(ChipSlot{nullptr, slot});
        int pad = st->S(3);
        int btnW = st->S(22);
        int innerH = slot.bottom - slot.top - 2 * pad;
        HWND minus = CreateChild(st, page, L"BUTTON", L"\u2212",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.left + pad, slot.top + pad, btnW, innerH,
            IDC_BTN_STEP_MINUS, nullptr, false);
        HWND plus = CreateChild(st, page, L"BUTTON", L"+",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.right - pad - btnW, slot.top + pad, btnW, innerH,
            IDC_BTN_STEP_PLUS, nullptr, false);
        int leftX = slot.left + pad + btnW + st->S(2);
        int rightX = slot.right - pad - btnW - st->S(2);
        HWND e = CreateChild(st, page, L"EDIT", value,
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL | editStyle, 0,
            leftX, slot.top + st->S(4), rightX - leftX,
            (slot.bottom - slot.top) - st->S(8), id, nullptr);
        st->chips[page].back().child = e;

        NumberField nf;
        nf.minus = minus;
        nf.edit = e;
        nf.plus = plus;
        nf.lo = lo;
        nf.hi = hi;
        st->numbers.push_back(nf);
        return e;
    };

    auto chipButton = [&](int page, RECT slot, const wchar_t* text,
                          int id) -> HWND {
        return CreateChild(st, page, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.left, slot.top, slot.right - slot.left, slot.bottom - slot.top,
            id, nullptr);
    };

    auto divider = [&](int page, int rowY) {
        st->rowDividerY[page].push_back(rowY + rowH + st->S(3));
    };

    auto intText = [](int v) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", v);
        return std::wstring(buf);
    };

    FontCatalog fontCatalog = LoadFontCatalog();
    FontChoices fontChoices = BuildFontChoices(fontCatalog, st->cfg.font);
    if (st->cfg.font.empty()) st->cfg.font = fontChoices.values[0];

    // ======================= 页 1：基础设置 =======================
    {
        const int page = PageBasic;
        y = 0;

        section(page, y, L"\u5012\u8ba1\u65f6\u76ee\u6807"); // 倒计时目标
        {
            int rowY = y;
            label(L"\u6807\u9898", rowY, page);
            st->hEditTitle = edit(page, makeSlots(rowY, {300})[0],
                                  st->cfg.title.c_str(), ES_AUTOHSCROLL,
                                  IDC_EDIT_TITLE);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u76ee\u6807\u65e5\u671f", rowY, page);
            auto slots = makeSlots(rowY, {140, 34});
            st->hEditDate = edit(page, slots[0],
                                 FormatDateText(st->cfg.targetTime).c_str(),
                                 ES_AUTOHSCROLL, IDC_DTP_DATE);
            st->hBtnCalendar = chipButton(page, slots[1], L"", IDC_BTN_CALENDAR);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u76ee\u6807\u65f6\u95f4", rowY, page);
            st->hEditTime = edit(page, makeSlots(rowY, {110})[0],
                                 FormatClockText(st->cfg.targetTime).c_str(),
                                 ES_AUTOHSCROLL, IDC_DTP_TIME);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5faa\u73af", rowY, page);
            st->hBtnCycle = dropdown(page, makeSlots(rowY, {120})[0], DropCycle,
                                     CycleItems(), static_cast<int>(st->cfg.cycle));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u8d77\u59cb\u65e5\u8ba1\u5165", rowY, page);
            st->hBtnStart = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                       IDC_CHECK_INCLUDE_START);
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u4e3b\u7a97\u53e3"); // 主窗口
        {
            int rowY = y;
            label(L"\u7a97\u53e3\u4f4d\u7f6e", rowY, page);
            st->hBtnAnchor = dropdown(page, makeSlots(rowY, {140})[0],
                                      DropWinAnchor, AnchorItems(),
                                      static_cast<int>(st->cfg.anchor));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u504f\u79fb\uff08\u50cf\u7d20\uff09", rowY, page);
            auto slots = makeSlots(rowY, {116, 14, 116, 14});
            st->hEditPlaceX = stepper(page, slots[0],
                                      intText(st->cfg.placeX).c_str(),
                                      IDC_EDIT_PLACE_X, -10000, 10000,
                                      ES_AUTOHSCROLL);
            unit(page, slots[1], L"X");
            st->hEditPlaceY = stepper(page, slots[2],
                                      intText(st->cfg.placeY).c_str(),
                                      IDC_EDIT_PLACE_Y, -10000, 10000,
                                      ES_AUTOHSCROLL);
            unit(page, slots[3], L"Y");
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u59cb\u7ec8\u7f6e\u9876", rowY, page);
            st->hBtnTop = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                     IDC_CHECK_TOPMOST);
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u542f\u52a8"); // 启动
        {
            int rowY = y;
            label(L"\u5f00\u673a\u81ea\u542f\u52a8", rowY, page);
            st->hBtnAuto = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                      IDC_CHECK_AUTOSTART);
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u4e34\u65f6\u6570\u636e"); // 临时数据
        {
            int rowY = y;
            label(L"\u6e05\u7406\u903b\u8f91", rowY, page);
            dropdown(page, makeSlots(rowY, {120})[0], DropTempMode,
                     TempModeItems(),
                     st->cfg.tempCleanupMode == TempCleanupMode::Size ? 1 : 0);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5927\u5c0f\u5355\u4f4d", rowY, page);
            dropdown(page, makeSlots(rowY, {100})[0], DropTempUnit,
                     TempUnitItems(),
                     st->cfg.tempSizeUnit == TempSizeUnit::GB ? 1 : 0);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            st->hTempArtLabel = label(L"\u4e00\u6587\u4e0a\u9650", rowY, page);
            st->hEditTempArtLimit = stepper(page, makeSlots(rowY, {150})[0],
                                            intText(st->cfg.tempArticleLimit).c_str(),
                                            IDC_EDIT_TEMP_ART_LIMIT, 1, 1000000,
                                            ES_NUMBER);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            st->hTempHkLabel = label(L"\u4e00\u8a00\u4e0a\u9650", rowY, page);
            st->hEditTempHkLimit = stepper(page, makeSlots(rowY, {150})[0],
                                           intText(st->cfg.tempHitokotoLimit).c_str(),
                                           IDC_EDIT_TEMP_HK_LIMIT, 1, 1000000,
                                           ES_NUMBER);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u6e05\u7406\u65b9\u5f0f", rowY, page);
            dropdown(page, makeSlots(rowY, {190})[0], DropTempTrim,
                     TempTrimItems(),
                     st->cfg.tempTrimMode == TempTrimMode::DropOldest ? 1 : 0);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5220\u9664\u6570\u91cf", rowY, page);
            st->hEditTempTrimCount = stepper(page, makeSlots(rowY, {150})[0],
                                             intText(st->cfg.tempTrimCount).c_str(),
                                             IDC_EDIT_TEMP_TRIM_COUNT, 1, 100000,
                                             ES_NUMBER);
            st->tempTrimFieldIndex = static_cast<int>(st->numbers.size()) - 1;
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            st->hBtnTempClean = chipButton(page, makeSlots(rowY, {140})[0],
                                           L"\u7acb\u5373\u6e05\u7406",
                                           IDC_BTN_TEMP_CLEAN);
            y += st->S(kRowStep);
        }
    }

    // ======================= 页 2：倒计时 =======================
    {
        const int page = PageCountdown;
        y = 0;
        FontChoices hkFonts = BuildFontChoices(fontCatalog, st->cfg.hitokotoFont);
        if (st->cfg.hitokotoFont.empty()) st->cfg.hitokotoFont = hkFonts.values[0];

        section(page, y, L"\u5012\u8ba1\u65f6\u5916\u89c2"); // 倒计时外观
        {
            int rowY = y;
            label(L"\u663e\u793a\u683c\u5f0f", rowY, page);
            st->hBtnFormat = dropdown(page, makeSlots(rowY, {170})[0], DropFormat,
                                      FormatItems(),
                                      static_cast<int>(st->cfg.displayFormat));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5b57\u53f7\uff08\u50cf\u7d20\uff09", rowY, page);
            st->hEditSize = stepper(page, makeSlots(rowY, {110})[0],
                                    intText(st->cfg.fontSize).c_str(),
                                    IDC_EDIT_FONTSIZE, 8, 200, ES_NUMBER);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u4fe1\u606f\u5b57\u4f53", rowY, page);
            dropdown(page, makeSlots(rowY, {320})[0], DropFont,
                     fontChoices.items, FontIndex(fontChoices, st->cfg.font));
            st->dropdowns.back().values = fontChoices.values;
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u80cc\u666f\u989c\u8272", rowY, page);
            {
                std::string hex = utils::ColorToHex(st->cfg.backgroundColor);
                std::wstring whex(hex.begin(), hex.end());
                st->hBtnBgColor = chipButton(page, makeSlots(rowY, {140})[0],
                                             whex.c_str(), IDC_BTN_BG_COLOR);
            }
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5b57\u4f53\u989c\u8272", rowY, page);
            {
                std::string hex = utils::ColorToHex(st->cfg.textColor);
                std::wstring whex(hex.begin(), hex.end());
                st->hBtnTextColor = chipButton(page, makeSlots(rowY, {140})[0],
                                               whex.c_str(), IDC_BTN_TEXT_COLOR);
            }
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u4e00\u8a00"); // 一言
        {
            int rowY = y;
            label(L"\u4e00\u8a00\u6765\u6e90", rowY, page);
            st->hBtnHkSrc = dropdown(page, makeSlots(rowY, {120})[0], DropSource,
                                     SourceItems(),
                                     static_cast<int>(st->cfg.hitokotoSource));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u7f51\u7edc\u53e5\u5b50\u7c7b\u578b", rowY, page);
            {
                std::vector<std::wstring> cats;
                for (int i = 0; i < hitokoto_category::Count(); ++i) {
                    cats.push_back(hitokoto_category::kOptions[i].name);
                }
                int catIdx = hitokoto_category::IndexOfCode(st->cfg.hitokotoCategory);
                if (catIdx < 0) catIdx = 0;
                st->hBtnHkCategory = dropdown(page, makeSlots(rowY, {180})[0],
                                              DropHkCategory, std::move(cats), catIdx);
            }
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5237\u65b0\u65f6\u95f4\uff08\u79d2\uff09", rowY, page);
            st->hEditHkRefresh = stepper(page, makeSlots(rowY, {116})[0],
                                         intText(st->cfg.hitokotoRefreshSec).c_str(),
                                         IDC_EDIT_HITOKOTO_REFRESH, 5, 3600,
                                         ES_NUMBER);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u4e00\u8a00\u5b57\u4f53", rowY, page);
            dropdown(page, makeSlots(rowY, {320})[0], DropHkFont,
                     hkFonts.items, FontIndex(hkFonts, st->cfg.hitokotoFont));
            st->dropdowns.back().values = hkFonts.values;
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u6587\u672c\u52a0\u7c97", rowY, page);
            st->hBtnHkBold = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                        IDC_BTN_HK_BOLD);
            divider(page, rowY); y += st->S(kRowStep);
        }
    }

    // ======================= 页 3：一文 =======================
    {
        const int page = PageArticle;
        y = 0;
        FontChoices artFonts = BuildFontChoices(fontCatalog, st->cfg.articleFont);
        if (st->cfg.articleFont.empty()) st->cfg.articleFont = artFonts.values[0];

        section(page, y, L"\u4e00\u6587\u5185\u5bb9"); // 一文内容
        {
            int rowY = y;
            label(L"\u680f\u76ee", rowY, page);
            st->hBtnArticle = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                         IDC_CHECK_ARTICLE);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5237\u65b0\u65f6\u95f4", rowY, page);
            int totalSec = st->cfg.articleRefreshSec;
            int artH = totalSec / 3600;
            int artM = (totalSec / 60) % 60;
            int artS = totalSec % 60;
            auto slots = makeSlots(rowY, {90, 14, 90, 14, 90, 14});
            st->hEditArtH = stepper(page, slots[0], intText(artH).c_str(),
                                    IDC_EDIT_ARTICLE_H, 0, 23, ES_NUMBER);
            unit(page, slots[1], L"\u65f6");
            st->hEditArtM = stepper(page, slots[2], intText(artM).c_str(),
                                    IDC_EDIT_ARTICLE_M, 0, 59, ES_NUMBER);
            unit(page, slots[3], L"\u5206");
            st->hEditArtS = stepper(page, slots[4], intText(artS).c_str(),
                                    IDC_EDIT_ARTICLE_S, 0, 59, ES_NUMBER);
            unit(page, slots[5], L"\u79d2");
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u6b63\u6587\u5b57\u53f7", rowY, page);
            st->hEditArtFont = stepper(page, makeSlots(rowY, {110})[0],
                                       intText(st->cfg.articleFontSize).c_str(),
                                       IDC_EDIT_ARTICLE_FONT, 8, 96, ES_NUMBER);
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u6b63\u6587\u5b57\u4f53", rowY, page);
            dropdown(page, makeSlots(rowY, {320})[0], DropArtFont,
                     artFonts.items, FontIndex(artFonts, st->cfg.articleFont));
            st->dropdowns.back().values = artFonts.values;
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u80cc\u666f\u989c\u8272", rowY, page);
            {
                std::string hex = utils::ColorToHex(st->cfg.articleBackgroundColor);
                std::wstring whex(hex.begin(), hex.end());
                st->hBtnArtBgColor = chipButton(page, makeSlots(rowY, {140})[0],
                                                whex.c_str(), IDC_BTN_ART_BG_COLOR);
            }
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u6587\u5b57\u989c\u8272", rowY, page);
            {
                std::string hex = utils::ColorToHex(st->cfg.articleTextColor);
                std::wstring whex(hex.begin(), hex.end());
                st->hBtnArtTextColor = chipButton(page, makeSlots(rowY, {140})[0],
                                                  whex.c_str(), IDC_BTN_ART_TEXT_COLOR);
            }
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u4e00\u6587\u5e03\u5c40"); // 一文布局
        {
            int rowY = y;
            label(L"\u7a97\u53e3\u4f4d\u7f6e", rowY, page);
            st->hBtnArtAnchor = dropdown(page, makeSlots(rowY, {140})[0],
                                         DropArtAnchor, AnchorItems(),
                                         static_cast<int>(st->cfg.articleAnchor));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u504f\u79fb\uff08\u50cf\u7d20\uff09", rowY, page);
            auto slots = makeSlots(rowY, {116, 14, 116, 14});
            st->hEditArtX = stepper(page, slots[0],
                                    intText(st->cfg.articlePlaceX).c_str(),
                                    IDC_EDIT_ARTICLE_X, -10000, 10000,
                                    ES_AUTOHSCROLL);
            unit(page, slots[1], L"X");
            st->hEditArtY = stepper(page, slots[2],
                                    intText(st->cfg.articlePlaceY).c_str(),
                                    IDC_EDIT_ARTICLE_Y, -10000, 10000,
                                    ES_AUTOHSCROLL);
            unit(page, slots[3], L"Y");
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5c3a\u5bf8\u6a21\u5f0f", rowY, page);
            st->hBtnArtSize = dropdown(page, makeSlots(rowY, {180})[0],
                                       DropArtSize, ArticleSizeItems(),
                                       static_cast<int>(st->cfg.articleSizeMode));
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u5c3a\u5bf8\uff08\u50cf\u7d20\uff09", rowY, page);
            slots = makeSlots(rowY, {116, 14, 116, 14});
            st->hEditArtWidth = stepper(page, slots[0],
                                        intText(st->cfg.articleWidth).c_str(),
                                        IDC_EDIT_ARTICLE_WIDTH, 180, 1200,
                                        ES_NUMBER);
            unit(page, slots[1], L"\u5bbd");
            st->hEditArtHeight = stepper(page, slots[2],
                                         intText(st->cfg.articleHeight).c_str(),
                                         IDC_EDIT_ARTICLE_HEIGHT, 120, 2000,
                                         ES_NUMBER);
            unit(page, slots[3], L"\u9ad8");
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            label(L"\u59cb\u7ec8\u7f6e\u9876", rowY, page);
            st->hBtnArtTop = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                        IDC_CHECK_ARTICLE_TOP);
            divider(page, rowY); y += st->S(kRowStep);
        }

        section(page, y, L"\u663e\u793a\u65b9\u5f0f"); // 显示方式
        {
            std::vector<std::wstring> modItems;
            std::vector<std::wstring> modValues;
            for (const auto& entry : ArticleModuleRegistry::Instance().Entries()) {
                modValues.push_back(entry.id);
                modItems.push_back(entry.name + L"\uff08" + entry.id + L"\uff09");
            }
            if (modValues.empty()) {
                modValues.push_back(L"normal");
                modItems.push_back(L"\u5185\u7f6e\u666e\u901a");
            }
            int cur = -1;
            for (size_t i = 0; i < modValues.size(); ++i) {
                if (modValues[i] == st->cfg.articleModule) {
                    cur = static_cast<int>(i);
                    break;
                }
            }
            if (cur < 0) {
                modValues.insert(modValues.begin(), st->cfg.articleModule);
                modItems.insert(modItems.begin(), st->cfg.articleModule);
                cur = 0;
            }

            int rowY = y;
            label(L"\u663e\u793a\u6a21\u5757", rowY, page);
            st->hBtnArticleModule = dropdown(page, makeSlots(rowY, {220})[0],
                                             DropArticleModule,
                                             modItems, cur);
            st->dropdowns.back().values = modValues;
            divider(page, rowY); y += st->S(kRowStep);

            rowY = y;
            st->hModuleHint = CreateLabel(st, page, L"", 0, rowY,
                                          slotRight - st->S(160));
            st->hBtnModuleConfig = chipButton(page, makeSlots(rowY, {140})[0],
                                              L"\u6a21\u5757\u8bbe\u7f6e",
                                              IDC_BTN_MODULE_CONFIG);
            y += st->S(kRowStep);
        }
    }

    // ======================= 页 4：关于 =======================
    {
        const int page = PageAbout;
        y = 0;
        // 内容由 PaintAbout 绘制，仅创建“获取更新”按钮
        st->hBtnUpdate = chipButton(page, RECT{st->S(8), st->S(140),
                                               st->S(148), st->S(170)},
                                    L"\u83b7\u53d6\u66f4\u65b0",
                                    IDC_BTN_UPDATE);
        st->pageContentH[page] = st->viewH;
    }

    // ======================= 底部按钮 =======================
    int btnY = cr.bottom - st->S(kPad) - st->S(kBtnH);
    int bx = cr.right - st->S(kPad) - st->S(kBtnW);
    st->hBtnSave = CreateWindowExW(0, L"BUTTON", L"\u4fdd\u5b58",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        bx, btnY, st->S(kBtnW), st->S(kBtnH), hwnd,
        (HMENU)(INT_PTR)IDC_BTN_SAVE, InstOf(hwnd), nullptr);
    bx -= st->S(kBtnW) + st->S(12);
    st->hBtnCancel = CreateWindowExW(0, L"BUTTON", L"\u53d6\u6d88",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        bx, btnY, st->S(kBtnW), st->S(kBtnH), hwnd,
        (HMENU)(INT_PTR)IDC_BTN_CANCEL, InstOf(hwnd), nullptr);

    // 统一字体
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, lp, TRUE);
        return TRUE;
    }, (LPARAM)st->font);
    for (HWND h : st->sectionLabels) {
        SendMessageW(h, WM_SETFONT, (WPARAM)st->boldFont, TRUE);
    }
    if (st->hModuleHint) {
        SendMessageW(st->hModuleHint, WM_SETFONT, (WPARAM)st->font, TRUE);
    }

    UpdateTempLabels(st);
    UpdateTempControls(st);
    UpdateModuleHint(st);

    // 页面内容高度取控件底部
    for (int i = 0; i < kTabCount; ++i) {
        if (st->pageContentH[i] < st->viewH) st->pageContentH[i] = st->viewH;
    }

    ShowPage(st, PageBasic);
    SetFocus(st->hEditTitle);
    // 光标置于末尾，避免整段文本被选中形成蓝色高亮
    SendMessageW(st->hEditTitle, EM_SETSEL, static_cast<WPARAM>(-1),
                 static_cast<LPARAM>(-1));
}

// ---------------------------------------------------------------------------
// 收集与校验
// ---------------------------------------------------------------------------
static bool ShowError(SettingsState* st, HWND hwnd, int page, HWND focus,
                      const wchar_t* text) {
    MessageBoxW(hwnd, text, L"\u63d0\u793a", MB_ICONWARNING);
    ShowPage(st, page);
    if (focus) SetFocus(focus);
    return false;
}

static bool CollectAndValidate(HWND hwnd, SettingsState* st) {
    wchar_t buf[256];

    GetWindowTextW(st->hEditTitle, buf, 256);
    st->cfg.title = buf;
    if (st->cfg.title.empty()) {
        return ShowError(st, hwnd, PageBasic, st->hEditTitle,
                         L"\u6807\u9898\u4e0d\u80fd\u4e3a\u7a7a");
    }

    wchar_t dateBuf[64] = {0};
    wchar_t clockBuf[64] = {0};
    GetWindowTextW(st->hEditDate, dateBuf, 64);
    GetWindowTextW(st->hEditTime, clockBuf, 64);
    std::wstring full = std::wstring(dateBuf) + L" " + clockBuf;
    std::tm target{};
    if (!utils::ParseTargetTime(full, target)) {
        return ShowError(st, hwnd, PageBasic, st->hEditDate,
            L"\u76ee\u6807\u65f6\u95f4\u683c\u5f0f\u65e0\u6548"
            L"\uff08\u65e5\u671f YYYY-MM-DD\uff0c\u65f6\u95f4 HH:MM:SS\uff09");
    }
    st->cfg.targetTime = target;
    st->cfg.includeStartDay = st->optStart;
    st->cfg.isTop = st->optTopMost;
    st->cfg.autoStart = st->optAutoStart;
    st->cfg.hitokotoBold = st->optHkBold;
    st->cfg.articleEnabled = st->optArticle;
    st->cfg.articleIsTop = st->optArtTop;

    int px = 0, py = 0;
    if (!ParseIntField(st->hEditPlaceX, px) ||
        !ParseIntField(st->hEditPlaceY, py)) {
        return ShowError(st, hwnd, PageBasic, st->hEditPlaceX,
                         L"\u504f\u79fb\u5fc5\u987b\u662f\u6574\u6570");
    }
    if (px < -10000 || px > 10000 || py < -10000 || py > 10000) {
        return ShowError(st, hwnd, PageBasic, st->hEditPlaceX,
                         L"\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4");
    }
    st->cfg.placeX = px;
    st->cfg.placeY = py;

    // 临时数据上限
    int artLimit = 0, hkLimit = 0, trimCount = 0;
    if (!ParseIntField(st->hEditTempArtLimit, artLimit) ||
        !ParseIntField(st->hEditTempHkLimit, hkLimit) ||
        !ParseIntField(st->hEditTempTrimCount, trimCount)) {
        return ShowError(st, hwnd, PageBasic, st->hEditTempArtLimit,
                         L"\u4e34\u65f6\u6570\u636e\u4e0a\u9650\u5fc5\u987b\u662f\u6574\u6570");
    }
    int maxLimit = st->cfg.tempCleanupMode == TempCleanupMode::Count
                       ? 1000000 : 10240;
    if (artLimit < 1 || artLimit > maxLimit ||
        hkLimit < 1 || hkLimit > maxLimit) {
        return ShowError(st, hwnd, PageBasic, st->hEditTempArtLimit,
            st->cfg.tempCleanupMode == TempCleanupMode::Count
                ? L"\u4e34\u65f6\u6570\u636e\u4e0a\u9650\u5e94\u5728 1\u20131000000 \u9879\u4e4b\u95f4"
                : L"\u4e34\u65f6\u6570\u636e\u4e0a\u9650\u5e94\u5728 1\u201310240 MB/GB \u4e4b\u95f4");
    }
    if (trimCount < 1 || trimCount > 100000) {
        return ShowError(st, hwnd, PageBasic, st->hEditTempTrimCount,
                         L"\u5220\u9664\u6570\u91cf\u5e94\u5728 1\u2013100000 \u4e4b\u95f4");
    }
    st->cfg.tempArticleLimit = artLimit;
    st->cfg.tempHitokotoLimit = hkLimit;
    st->cfg.tempTrimCount = trimCount;

    // 倒计时外观 / 一言
    int size = 0;
    if (!ParseIntField(st->hEditSize, size) || size < 8 || size > 200) {
        return ShowError(st, hwnd, PageCountdown, st->hEditSize,
                         L"\u5b57\u53f7\u5e94\u5728 8\u2013200 \u4e4b\u95f4");
    }
    st->cfg.fontSize = size;

    int hkSec = 0;
    if (!ParseIntField(st->hEditHkRefresh, hkSec) || hkSec < 5 || hkSec > 3600) {
        return ShowError(st, hwnd, PageCountdown, st->hEditHkRefresh,
                         L"\u4e00\u8a00\u5237\u65b0\u65f6\u95f4\u5e94\u5728 5\u20133600 \u79d2\u4e4b\u95f4");
    }
    st->cfg.hitokotoRefreshSec = hkSec;

    // 一文
    int artX = 0, artY = 0;
    if (!ParseIntField(st->hEditArtX, artX) ||
        !ParseIntField(st->hEditArtY, artY)) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtX,
                         L"\u4e00\u6587\u504f\u79fb\u5fc5\u987b\u662f\u6574\u6570");
    }
    if (artX < -10000 || artX > 10000 || artY < -10000 || artY > 10000) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtX,
                         L"\u4e00\u6587\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4");
    }
    st->cfg.articlePlaceX = artX;
    st->cfg.articlePlaceY = artY;

    int artH = 0, artM = 0, artS = 0;
    if (!ParseIntField(st->hEditArtH, artH) ||
        !ParseIntField(st->hEditArtM, artM) ||
        !ParseIntField(st->hEditArtS, artS)) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtH,
                         L"\u4e00\u6587\u5237\u65b0\u65f6\u95f4\u5fc5\u987b\u662f\u6574\u6570");
    }
    int artTotal = artH * 3600 + artM * 60 + artS;
    if (artH < 0 || artH > 23 || artM < 0 || artM > 59 ||
        artS < 0 || artS > 59 || artTotal < 1 || artTotal > 86399) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtH,
                         L"\u4e00\u6587\u5237\u65b0\u65f6\u95f4\u5e94\u5728 00:00:01\u201323:59:59 \u4e4b\u95f4");
    }
    st->cfg.articleRefreshSec = artTotal;

    int artFont = 0;
    if (!ParseIntField(st->hEditArtFont, artFont) ||
        artFont < 8 || artFont > 96) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtFont,
                         L"\u4e00\u6587\u5b57\u53f7\u5e94\u5728 8\u201396 \u4e4b\u95f4");
    }
    st->cfg.articleFontSize = artFont;

    int artW = 0, artHgt = 0;
    if (!ParseIntField(st->hEditArtWidth, artW) ||
        !ParseIntField(st->hEditArtHeight, artHgt)) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtWidth,
                         L"\u4e00\u6587\u5c3a\u5bf8\u5fc5\u987b\u662f\u6574\u6570");
    }
    if (artW < 180 || artW > 1200 || artHgt < 120 || artHgt > 2000) {
        return ShowError(st, hwnd, PageArticle, st->hEditArtWidth,
                         L"\u4e00\u6587\u5c3a\u5bf8\u5e94\u4e3a\uff1a\u5bbd 180\u20131200\u3001\u9ad8 120\u20132000");
    }
    st->cfg.articleWidth = artW;
    st->cfg.articleHeight = artHgt;

    if (!st->cfg.Save()) {
        MessageBoxW(hwnd, L"\u4fdd\u5b58\u914d\u7f6e\u5931\u8d25",
                    L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    if (!autostart::SetEnabled(st->cfg.autoStart)) {
        MessageBoxW(hwnd,
                    L"\u5f00\u673a\u81ea\u542f\u52a8\u8bbe\u7f6e\u5931\u8d25\uff08\u914d\u7f6e\u5df2\u4fdd\u5b58\uff09",
                    L"\u63d0\u793a", MB_ICONWARNING);
    }
    st->saved = true;
    return true;
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
static LRESULT CALLBACK SettingsWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    SettingsState* st = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        st = reinterpret_cast<SettingsState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->dlg = hwnd;
    } else {
        st = reinterpret_cast<SettingsState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_CREATE:
            OnCreate(hwnd, st);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintDialog(hwnd, st);
            return 0;

        case WM_ACTIVATE:
            if (st && LOWORD(wp) == WA_INACTIVE) {
                CloseDropdown(st);
                CloseCalendarPopup(st);
            }
            break;

        case WM_ENTERSIZEMOVE:
            if (st) {
                CloseDropdown(st);
                CloseCalendarPopup(st);
            }
            break;

        case WM_NCHITTEST: {
            if (!st) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            ScreenToClient(hwnd, &pt);
            if (pt.y < st->S(10)) return HTCAPTION;
            if (pt.y < st->S(kTabDivider) && !st->tabRects.empty() &&
                pt.x > st->tabRects.back().right + st->S(16)) {
                return HTCAPTION;
            }
            return HTCLIENT;
        }

        case WM_MOUSEMOVE: {
            if (!st) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            int hover = -1;
            for (int i = 0; i < static_cast<int>(st->tabRects.size()); ++i) {
                if (PtInRect(&st->tabRects[i], pt)) { hover = i; break; }
            }
            if (hover != st->hoverTab) {
                st->hoverTab = hover;
                RECT bar{0, 0, 4000, st->S(kTabDivider + 4)};
                InvalidateRect(hwnd, &bar, FALSE);
            }
            if (!st->trackingMouse) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                st->trackingMouse = true;
            }
            break;
        }

        case WM_MOUSELEAVE:
            if (st) {
                st->trackingMouse = false;
                if (st->hoverTab != -1) {
                    st->hoverTab = -1;
                    RECT bar{0, 0, 4000, st->S(kTabDivider + 4)};
                    InvalidateRect(hwnd, &bar, FALSE);
                }
            }
            break;

        case WM_LBUTTONDOWN: {
            if (!st) break;
            POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            CloseDropdown(st);
            CloseCalendarPopup(st);
            for (int i = 0; i < static_cast<int>(st->tabRects.size()); ++i) {
                if (PtInRect(&st->tabRects[i], pt)) {
                    if (st->page != i) {
                        ShowPage(st, i);
                        FocusPageControl(st, i);
                    }
                    return 0;
                }
            }
            break;
        }

        case WM_MOUSEWHEEL: {
            if (!st) break;
            int page = st->page;
            if (page >= 0 && page < kTabCount && st->pageViews[page]) {
                SendMessageW(st->pageViews[page], WM_MOUSEWHEEL, wp, lp);
            }
            return 0;
        }

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (st && dis && dis->CtlType == ODT_BUTTON) {
                DrawOwnerButton(st, dis);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            if (!st || !st->brBg) break;
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, kLabel);
            SetBkColor(dc, kBg);
            return reinterpret_cast<LRESULT>(st->brBg);
        }

        case WM_CTLCOLOREDIT: {
            if (!st || !st->brChip) break;
            HDC dc = reinterpret_cast<HDC>(wp);
            // 不透明背景：避免编辑框滚动或重绘时残留上一帧文字
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kValueText);
            SetBkColor(dc, kChipBg);
            return reinterpret_cast<LRESULT>(st->brChip);
        }

        case WM_CTLCOLORLISTBOX: {
            if (!st || !st->brChip) break;
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, OPAQUE);
            SetTextColor(dc, kValueText);
            SetBkColor(dc, kChipBg);
            return reinterpret_cast<LRESULT>(st->brChip);
        }

        case DM_GETDEFID:
            return MAKELRESULT(IDC_BTN_SAVE, DC_HASDEFID);

        case WM_APP_CLOSE_CALENDAR: {
            if (st && st->hCalPopup) {
                HWND dateEdit = st->hEditDate;
                CloseCalendarPopup(st);
                if (dateEdit && IsWindow(dateEdit)) SetFocus(dateEdit);
            }
            return 0;
        }

        case WM_COMMAND: {
            if (!st) break;
            WORD id = LOWORD(wp);
            WORD code = HIWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);

            if (code == EN_SETFOCUS || code == EN_KILLFOCUS ||
                code == CBN_SETFOCUS || code == CBN_KILLFOCUS ||
                code == BN_SETFOCUS || code == BN_KILLFOCUS) {
                if (st->pageViews[st->page]) {
                    InvalidateRect(st->pageViews[st->page], nullptr, TRUE);
                }
            }

            if (code == EN_SETFOCUS || code == CBN_SETFOCUS || code == BN_SETFOCUS) {
                if (st->hDropdown && ctl != st->hDropdownTrigger) {
                    CloseDropdown(st);
                }
                if (st->hCalPopup && ctl != st->hBtnCalendar &&
                    ctl != st->hCalPrevYear && ctl != st->hCalNextYear) {
                    CloseCalendarPopup(st);
                }
                if (ctl) EnsureControlVisible(st, ctl);
            }

            if (ctl) {
                for (auto& d : st->dropdowns) {
                    if (d.trigger == ctl) {
                        if (code == BN_CLICKED) OpenDropdown(st, &d);
                        return 0;
                    }
                }
                for (auto& n : st->numbers) {
                    if (ctl == n.minus || ctl == n.plus) {
                        if (code == BN_CLICKED) {
                            AdjustNumber(n, ctl == n.plus ? 1 : -1);
                        }
                        return 0;
                    }
                }
            }

            switch (id) {
                case IDC_BTN_SAVE:
                    if (code != BN_CLICKED) return 0;
                    if (CollectAndValidate(hwnd, st)) DestroyWindow(hwnd);
                    return 0;
                case IDOK:
                    if (CollectAndValidate(hwnd, st)) DestroyWindow(hwnd);
                    return 0;
                case IDC_BTN_CANCEL:
                    if (code != BN_CLICKED) return 0;
                    DestroyWindow(hwnd);
                    return 0;
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;

                case IDC_BTN_CALENDAR:
                    if (code == BN_CLICKED) OpenCalendarPopup(st);
                    return 0;

                case IDC_BTN_BG_COLOR:
                    if (code == BN_CLICKED) PickColor(hwnd, st, ColorCountdownBg);
                    return 0;
                case IDC_BTN_TEXT_COLOR:
                    if (code == BN_CLICKED) PickColor(hwnd, st, ColorCountdownText);
                    return 0;
                case IDC_BTN_ART_BG_COLOR:
                    if (code == BN_CLICKED) PickColor(hwnd, st, ColorArticleBg);
                    return 0;
                case IDC_BTN_ART_TEXT_COLOR:
                    if (code == BN_CLICKED) PickColor(hwnd, st, ColorArticleText);
                    return 0;

                case IDC_CAL_PREV_YEAR:
                    if (code == BN_CLICKED) AdjustCalendarYear(st, -1);
                    return 0;
                case IDC_CAL_NEXT_YEAR:
                    if (code == BN_CLICKED) AdjustCalendarYear(st, +1);
                    return 0;

                case IDC_BTN_MODULE_CONFIG:
                    if (code == BN_CLICKED) {
                        const ArticleModuleEntry* entry =
                            ArticleModuleRegistry::Instance().Find(st->cfg.articleModule);
                        if (entry) {
                            std::string out;
                            if (OpenArticleModuleConfigDialog(hwnd, *entry,
                                    st->cfg.ArticleModuleConfig(entry->id), out)) {
                                st->cfg.SetArticleModuleConfig(entry->id, out);
                            }
                            UpdateModuleHint(st);
                        }
                    }
                    return 0;

                case IDC_BTN_UPDATE:
                    if (code == BN_CLICKED) {
                        ShellExecuteW(nullptr, L"open", APP_UPDATE_URL,
                                      nullptr, nullptr, SW_SHOWNORMAL);
                    }
                    return 0;

                case IDC_BTN_TEMP_CLEAN:
                    if (code == BN_CLICKED) {
                        if (MessageBoxW(hwnd,
                                L"\u786e\u5b9a\u6e05\u7a7a\u4e00\u8a00\u4e0e\u4e00\u6587\u7684\u5df2\u663e\u793a\u8bb0\u5f55\uff1f",
                                L"\u63d0\u793a", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                            history::ClearAll();
                            MessageBoxW(hwnd, L"\u4e34\u65f6\u8bb0\u5f55\u5df2\u6e05\u7a7a",
                                        L"\u63d0\u793a", MB_ICONINFORMATION);
                        }
                    }
                    return 0;

                default:
                    if (IsToggleId(id)) {
                        if (code == BN_CLICKED) {
                            bool& v = ToggleRef(st, id);
                            v = !v;
                            InvalidateRect(reinterpret_cast<HWND>(lp), nullptr, TRUE);
                        }
                        return 0;
                    }
                    break;
            }
            return 0;
        }

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (st) {
                CloseDropdown(st);
                CloseCalendarPopup(st);
                if (st->aboutIcon) {
                    DestroyIcon(st->aboutIcon);
                    st->aboutIcon = nullptr;
                }
                ReleasePaintBuffer(st);
                st->dlg = nullptr;
                st->running = false;
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------------------
bool OpenSettingsDialog(HWND parent, Config& cfg) {
    if (FindWindowW(kClass, nullptr) != nullptr) return false;

    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE);
    if (!RegisterSettingsClass(hInst)) {
        MessageBoxW(parent, L"\u6ce8\u518c\u8bbe\u7f6e\u7a97\u53e3\u7c7b\u5931\u8d25",
                    L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }

    SettingsState st;
    st.cfg = cfg;
    st.parent = parent;
    st.dpi = static_cast<UINT>(utils::GetWindowDpi(parent));

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST);
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int winW = std::min(st.S(kWinW), workW - st.S(16));
    int winH = std::min(st.S(kWinH), workH - st.S(16));
    int x = mi.rcWork.left + (workW - winW) / 2;
    int y = mi.rcWork.top + (workH - winH) / 2;

    std::wstring defFont = utils::GetSystemDefaultFontName();
    st.font = CreateFontW(-st.S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, defFont.c_str());
    st.boldFont = CreateFontW(-st.S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE, defFont.c_str());
    st.aboutTitleFont = CreateFontW(-st.S(16), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, defFont.c_str());

    st.dlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass,
        L"\u8bbe\u7f6e",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, winW, winH,
        parent, nullptr, hInst, &st);

    if (!st.dlg) {
        if (st.font) DeleteObject(st.font);
        if (st.boldFont) DeleteObject(st.boldFont);
        if (st.aboutTitleFont) DeleteObject(st.aboutTitleFont);
        return false;
    }

    SetWindowRgn(st.dlg,
        CreateRoundRectRgn(0, 0, winW + 1, winH + 1, st.S(24), st.S(24)), TRUE);

    EnableWindow(parent, FALSE);
    ShowWindow(st.dlg, SW_SHOW);
    UpdateWindow(st.dlg);

    MSG msg;
    while (st.running) {
        if (st.parent && !IsWindow(st.parent)) break;
        BOOL got = GetMessage(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            if (st.hDropdown || st.hCalPopup) {
                CloseDropdown(&st);
                CloseCalendarPopup(&st);
            } else if (st.dlg) {
                DestroyWindow(st.dlg);
            }
            continue;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN &&
            !(st.hDropdown || st.hCalPopup)) {
            // 焦点在输入框时 Enter 保存；在按钮/下拉触发器上则交给
            // IsDialogMessage 处理，保留按钮原本的进入/切换行为
            HWND focus = GetFocus();
            wchar_t cls[32] = {0};
            if (focus) GetClassNameW(focus, cls, 32);
            if (lstrcmpiW(cls, L"EDIT") == 0 && st.dlg) {
                if (CollectAndValidate(st.dlg, &st)) DestroyWindow(st.dlg);
                continue;
            }
        }
        if (!st.dlg || !IsDialogMessageW(st.dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (st.dlg) {
        DestroyWindow(st.dlg);
        st.dlg = nullptr;
    }

    if (parent && IsWindow(parent)) {
        EnableWindow(parent, TRUE);
        SetForegroundWindow(parent);
    }
    if (st.font) DeleteObject(st.font);
    if (st.boldFont) DeleteObject(st.boldFont);
    if (st.aboutTitleFont) DeleteObject(st.aboutTitleFont);
    if (st.brBg) DeleteObject(st.brBg);
    if (st.brChip) DeleteObject(st.brChip);

    if (st.saved) {
        cfg = st.cfg;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
void CloseSettingsWindow() {
    if (HWND dlg = FindWindowW(kClass, nullptr)) {
        SendMessageW(dlg, WM_CLOSE, 0, 0);
    }
}
