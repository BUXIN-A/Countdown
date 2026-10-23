// =============================================================================
// settings_window.cpp - 深色无边框设置窗口
//
// 视觉参考：深色背景、胶囊标签页、右侧值胶囊、细分隔线、圆角无边框窗口。
// 交互：
//  - 枚举值（循环/格式/来源/位置/尺寸模式/字体）使用自绘深色下拉列表；
//  - 布尔值显示为“开/关”胶囊；
//  - 日期为文本胶囊 + 日历月历浮层；时间为文本胶囊；
//  - 数字字段使用 左“−” / 中间数值 / 右“+” 的自绘步进器。
// =============================================================================
#include "settings_window.h"
#include "../resource.h"
#include "autostart.h"
#include "utils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
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
// 配色（深色卡片风格）
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
// 布局常量（96 DPI 逻辑像素，运行时按 DPI 缩放）
// ---------------------------------------------------------------------------
static const int kPad        = 24;
static const int kWinW       = 640;
static const int kWinH       = 470;
static const int kTabTop     = 12;
static const int kTabH       = 30;
static const int kTabDivider = 54;
static const int kContentTop = 72;
static const int kRowH       = 32;
static const int kRowStep    = 38;
static const int kBtnH       = 30;
static const int kBtnW       = 88;
static const int kTabCount   = 5;

enum SettingsPage {
    PageBasic = 0,
    PageAppearance,
    PageHitokoto,
    PageArticle,
    PageWindow,
};

struct ChipSlot {
    HWND child = nullptr;
    RECT rc{};
};

// 下拉列表类型（每种只有一个实例，作为浮层数据的键）
enum DropdownKind {
    DropCycle = 0,
    DropFormat,
    DropSource,
    DropArtAnchor,
    DropWinAnchor,
    DropArtSize,
    DropFont,
};

struct DropdownDef {
    HWND trigger = nullptr;             // 显示当前值的胶囊按钮
    DropdownKind kind = DropCycle;
    std::vector<std::wstring> items;
    int current = 0;
};

// 数字步进器：左减、中值、右加
struct NumberField {
    HWND minus = nullptr;
    HWND edit = nullptr;
    HWND plus = nullptr;
    int lo = 0;
    int hi = 0;
};

// 下拉浮层窗口的数据（窗口销毁时释放）
struct SettingsState; // 前置声明

struct DropdownPopupData {
    SettingsState* st = nullptr;
    DropdownKind kind = DropCycle;
    HWND list = nullptr;
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
    HBRUSH brBg = nullptr;
    HBRUSH brChip = nullptr;

    int  page = PageBasic;
    int  hoverTab = -1;
    bool trackingMouse = false;

    // 布尔胶囊的状态（BS_OWNERDRAW 不能与 BS_AUTOCHECKBOX 共存，需自己维护）
    bool optStart = false;
    bool optArticle = false;
    bool optArtTop = false;
    bool optTopMost = false;
    bool optAutoStart = false;
    bool optDummy = false; // ToggleRef 的兜底

    std::vector<RECT> tabRects;
    std::vector<HWND> pages[kTabCount];
    std::vector<ChipSlot> chips[kTabCount];
    std::vector<int> rowDividerY[kTabCount];

    HWND hEditTitle = nullptr;
    HWND hEditDate = nullptr;
    HWND hBtnCalendar = nullptr;
    HWND hEditTime = nullptr;
    HWND hBtnCycle = nullptr;
    HWND hBtnStart = nullptr;
    HWND hBtnFormat = nullptr;
    HWND hEditSize = nullptr;
    HWND hBtnHkSrc = nullptr;
    HWND hEditHkRefresh = nullptr;
    HWND hBtnArticle = nullptr;
    HWND hBtnArtAnchor = nullptr;
    HWND hEditArtX = nullptr;
    HWND hEditArtY = nullptr;
    HWND hEditArtH = nullptr;
    HWND hEditArtM = nullptr;
    HWND hEditArtS = nullptr;
    HWND hBtnArtTop = nullptr;
    HWND hEditArtFont = nullptr;
    HWND hBtnArtSize = nullptr;
    HWND hEditArtWidth = nullptr;
    HWND hEditArtHeight = nullptr;
    HWND hBtnTop = nullptr;
    HWND hBtnAnchor = nullptr;
    HWND hEditPlaceX = nullptr;
    HWND hEditPlaceY = nullptr;
    HWND hBtnAuto = nullptr;
    HWND hBtnSave = nullptr;
    HWND hBtnCancel = nullptr;

    HWND hCalPopup = nullptr;       // 日期月历（子窗口浮层）
    HWND hCalPrevYear = nullptr;    // 月历上方的“上一年”
    HWND hCalNextYear = nullptr;    // 月历上方的“下一年”
    HWND hDropdown = nullptr;       // 下拉列表浮层（子窗口）
    HWND hDropdownTrigger = nullptr; // 当前打开下拉的触发按钮

    std::vector<DropdownDef> dropdowns;
    std::vector<NumberField> numbers;

    int S(int value) const {
        return utils::ScaleDpi(value, static_cast<int>(dpi));
    }
};

// ---------------------------------------------------------------------------
static const wchar_t* kClass = L"CountdownSettingsClass";
static const wchar_t* kDropdownClass = L"CountdownSettingsDropdown";
static const wchar_t* kTabNames[kTabCount] = {
    L"\u57fa\u672c",                    // 基本
    L"\u5916\u89c2",                    // 外观
    L"\u4e00\u8a00",                    // 一言
    L"\u4e00\u6587",                    // 一文
    L"\u7a97\u53e3\u4e0e\u542f\u52a8",  // 窗口与启动
};

// 前置声明
static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
static LRESULT CALLBACK DropdownWndProc(HWND, UINT, WPARAM, LPARAM);
static bool RegisterDropdownClass(HINSTANCE hInst);
static void OnCreate(HWND hwnd, SettingsState* st);
static bool CollectAndValidate(HWND hwnd, SettingsState* st);
static void ShowPage(SettingsState* st, int page);
static void LayoutTabs(SettingsState* st, HWND hwnd);
static void CloseCalendarPopup(SettingsState* st);
static void CloseDropdown(SettingsState* st);
static void OpenDropdown(SettingsState* st, DropdownDef* def);

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

// 下拉列表内容：顺序必须与对应枚举值一致
static std::vector<std::wstring> CycleItems() {
    return {
        L"\u5173\u95ed",            // 关闭（None=0）
        L"\u6bcf\u5e74",            // 每年
        L"\u6bcf\u6708",            // 每月
        L"\u6bcf\u5929",            // 每天
        L"\u6bcf\u5c0f\u65f6",      // 每小时
        L"\u6bcf\u5206\u949f",      // 每分钟
        L"\u6bcf\u79d2",            // 每秒
    };
}

static std::vector<std::wstring> FormatItems() {
    return {
        L"\u65f6:\u5206:\u79d2",                 // HMS
        L"\u5e74\u6708\u65e5 \u65f6\u5206\u79d2", // Full
        L"\u5929\u6570 \u65f6:\u5206:\u79d2",     // Days
    };
}

static std::vector<std::wstring> SourceItems() {
    return {
        L"\u672c\u5730",                       // 本地
        L"\u7f51\u7edc",                       // 网络
        L"\u672c\u5730\u548c\u7f51\u7edc",     // 本地和网络
    };
}

static std::vector<std::wstring> AnchorItems() {
    return {
        L"\u5de6\u4e0a\u89d2",   L"\u9876\u90e8\u5c45\u4e2d", L"\u53f3\u4e0a\u89d2",
        L"\u5de6\u5c45\u4e2d",   L"\u5c4f\u5e55\u5c45\u4e2d", L"\u53f3\u5c45\u4e2d",
        L"\u5de6\u4e0b\u89d2",   L"\u5e95\u90e8\u5c45\u4e2d", L"\u53f3\u4e0b\u89d2",
    };
}

static std::vector<std::wstring> ArticleSizeItems() {
    return {
        L"\u81ea\u9002\u5e94\uff08\u56fa\u5b9a\u5bbd\uff09", // 自适应（固定宽）
        L"\u5168\u81ea\u9002\u5e94",                       // 全自适应
        L"\u81ea\u5b9a\u4e49",                             // 自定义
    };
}

// 严格解析整数输入：允许首尾空白，空文本按 0 处理
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

// 选中下拉项：写入配置并更新触发按钮文字
static void ApplyDropdownSelection(SettingsState* st, DropdownDef* def, int index) {
    if (!st || !def || index < 0 || index >= static_cast<int>(def->items.size())) {
        return;
    }
    def->current = index;
    switch (def->kind) {
        case DropCycle:      st->cfg.cycle = static_cast<CycleUnit>(index); break;
        case DropFormat:     st->cfg.displayFormat =
                                 static_cast<utils::DisplayFormat>(index); break;
        case DropSource:     st->cfg.hitokotoSource =
                                 static_cast<HitokotoSource>(index); break;
        case DropArtAnchor:  st->cfg.articleAnchor =
                                 static_cast<WindowAnchor>(index); break;
        case DropWinAnchor:  st->cfg.anchor = static_cast<WindowAnchor>(index); break;
        case DropArtSize:    st->cfg.articleSizeMode =
                                 static_cast<ArticleSizeMode>(index); break;
        case DropFont:       st->cfg.font = def->items[index]; break;
    }
    SetWindowTextW(def->trigger, def->items[index].c_str());
    InvalidateRect(def->trigger, nullptr, TRUE);
}

// 步进器：按一次加/减 1 并钳制在范围内
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

static void CloseDropdown(SettingsState* st) {
    if (!st) return;
    if (st->hDropdown) {
        HWND h = st->hDropdown;
        st->hDropdown = nullptr;
        st->hDropdownTrigger = nullptr;
        DestroyWindow(h);
    }
}

// ---------------------------------------------------------------------------
// 窗口类
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
        wc.hbrBackground = nullptr; // 自绘深色背景
        wc.lpszClassName = kClass;
        if (!RegisterClassExW(&wc)) return false;
    }
    // 下拉浮层类与设置窗口同生命周期，顺带保证已注册
    return RegisterDropdownClass(hInst);
}

// ---------------------------------------------------------------------------
// 控件创建辅助
// ---------------------------------------------------------------------------
static HINSTANCE InstOf(HWND h) {
    return (HINSTANCE)GetWindowLongPtrW(h, GWLP_HINSTANCE);
}

static HWND CreateChild(SettingsState* st, int page, const wchar_t* cls,
                        const wchar_t* text, DWORD style, DWORD exStyle,
                        int x, int y, int w, int h, int id, HWND parent,
                        bool tabStop = true) {
    // 可交互控件统一补 WS_TABSTOP；步进器的 −/+ 按钮显式关闭，避免 Tab 过于琐碎
    if (tabStop && (style & WS_CHILD) &&
        (lstrcmpiW(cls, L"EDIT") == 0 || lstrcmpiW(cls, L"BUTTON") == 0 ||
         lstrcmpiW(cls, L"COMBOBOX") == 0)) {
        style |= WS_TABSTOP;
    }
    HWND hwnd = CreateWindowExW(exStyle, cls, text, style, x, y, w, h,
                                parent, (HMENU)(INT_PTR)id, InstOf(parent),
                                nullptr);
    if (hwnd && page >= 0 && page < kTabCount) st->pages[page].push_back(hwnd);
    return hwnd;
}

static HWND CreateLabel(SettingsState* st, HWND parent, const wchar_t* text,
                        int x, int y, int page) {
    HWND h = CreateWindowExW(0, L"STATIC", text,
        WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE,
        x, y, st->S(210), st->S(kRowH), parent, nullptr, InstOf(parent), nullptr);
    if (h) st->pages[page].push_back(h);
    return h;
}

static bool IsToggleId(int id) {
    return id == IDC_CHECK_INCLUDE_START || id == IDC_CHECK_ARTICLE ||
           id == IDC_CHECK_ARTICLE_TOP || id == IDC_CHECK_TOPMOST ||
           id == IDC_CHECK_AUTOSTART;
}

static bool& ToggleRef(SettingsState* st, int id) {
    switch (id) {
        case IDC_CHECK_INCLUDE_START: return st->optStart;
        case IDC_CHECK_ARTICLE:       return st->optArticle;
        case IDC_CHECK_ARTICLE_TOP:   return st->optArtTop;
        case IDC_CHECK_TOPMOST:       return st->optTopMost;
        case IDC_CHECK_AUTOSTART:     return st->optAutoStart;
        default:                      return st->optDummy;
    }
}

// ---------------------------------------------------------------------------
// 标签页布局/切换
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
    if (!st) return;
    CloseDropdown(st);
    CloseCalendarPopup(st);
    st->page = page;
    for (int i = 0; i < kTabCount; ++i) {
        int cmd = (i == page) ? SW_SHOW : SW_HIDE;
        for (HWND h : st->pages[i]) {
            if (h) ShowWindow(h, cmd);
        }
    }
    if (st->dlg) InvalidateRect(st->dlg, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// 自绘
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

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, w, h);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    FillRect(mem, &cr, st->brBg);
    FillRound(mem, RECT{0, 0, w - 1, h - 1}, st->S(24), kBg, kChipBorder);

    // ---- 胶囊标签页 ----
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

    // ---- 分隔线 ----
    HPEN penDiv = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ oldPen = SelectObject(mem, penDiv);
    MoveToEx(mem, st->S(kPad), st->S(kTabDivider), nullptr);
    LineTo(mem, w - st->S(kPad), st->S(kTabDivider));

    if (st->page >= 0 && st->page < kTabCount) {
        for (int y : st->rowDividerY[st->page]) {
            MoveToEx(mem, st->S(kPad), y, nullptr);
            LineTo(mem, w - st->S(kPad), y);
        }
    }

    // ---- 值胶囊底 ----
    if (st->page >= 0 && st->page < kTabCount) {
        HWND focus = GetFocus();
        for (const ChipSlot& chip : st->chips[st->page]) {
            bool focused = chip.child && chip.child == focus;
            FillRound(mem, chip.rc, st->S(8), kChipBg,
                      focused ? kAccent : kChipBorder);
        }
    }

    // ---- 底部按钮分隔线与制作者信息 ----
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
    DrawTextW(mem, L"\u5236\u4f5c\uff1aBu Xin    \u90ae\u7bb1\uff1abuxin0912@qq.com",
              -1, &credit,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(mem, oldFont);

    BitBlt(dc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// 自绘按钮：开关、枚举值、保存/取消、日历
static void DrawOwnerButton(SettingsState* st, DRAWITEMSTRUCT* dis) {
    if (!st || !dis || dis->CtlType != ODT_BUTTON) return;

    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    int radius = std::min(st->S(8), static_cast<int>(rc.bottom - rc.top) / 2);
    HDC dc = dis->hDC;

    if (dis->CtlID == IDC_BTN_SAVE) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 74, 196) : kAccent, kAccentHi);
        DrawCentered(dc, rc, L"\u4fdd\u5b58", st->boldFont, RGB(255, 255, 255));
        return;
    }
    if (dis->CtlID == IDC_BTN_CANCEL) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 48, 60) : kChipBg, kChipBorder);
        DrawCentered(dc, rc, L"\u53d6\u6d88", st->font, kLabel);
        return;
    }
    if (dis->CtlID == IDC_BTN_CALENDAR) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 48, 60) : kChipBg,
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
        FillRound(dc, rc, radius,
                  pressed ? RGB(46, 46, 57) : kChipBg, kChipBorder);
        int cx = (rc.left + rc.right) / 2;
        int cy = (rc.top + rc.bottom) / 2;
        int arm = st->S(4);
        HPEN pen = CreatePen(PS_SOLID, std::max(1, st->S(1)), kLabel);
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
        FillRound(dc, rc, radius,
                  pressed ? RGB(46, 46, 57) : kChipBg,
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

    // 其余自绘按钮（预留）
    wchar_t text[128] = {0};
    GetWindowTextW(dis->hwndItem, text, 128);
    FillRound(dc, rc, radius,
              pressed ? RGB(46, 46, 57) : kChipBg,
              focused ? kAccent : kChipBorder);
    DrawCentered(dc, rc, text, st->font, kValueText);
}

// ---------------------------------------------------------------------------
// 深色下拉浮层（子窗口，避免独立弹窗的焦点/激活问题）
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
                // 不预选当前项：这样点击任何一项（包括当前项）都会触发
                // LBN_SELCHANGE，避免“点当前项没反应”
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
            // 直接取配置里的原始字符串，不依赖 LB_GETTEXT，
            // 避免 owner-draw 列表字符串存储差异导致乱码
            const wchar_t* text = L"";
            if (def && index >= 0 && index < static_cast<int>(def->items.size())) {
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
                    // 延后销毁：不要在该列表自身的通知里销毁它
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
    if (!pop) {
        // CreateWindowEx 失败时若已走过 WM_CREATE，WM_DESTROY 已释放 data；
        // 这里不再重复 delete
        return;
    }

    st->hDropdown = pop;
    st->hDropdownTrigger = def->trigger;
    SetWindowPos(pop, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW | SWP_NOACTIVATE);
}

// ---------------------------------------------------------------------------
// 日期弹层（月历 + 年份切换）
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

// 按年切换月历当前选中日期（只改视图，点日期后才回填）
static void AdjustCalendarYear(SettingsState* st, int delta) {
    if (!st || !st->hCalPopup) return;
    SYSTEMTIME cur{};
    if (!SendMessageW(st->hCalPopup, MCM_GETCURSEL, 0, (LPARAM)&cur)) {
        cur.wYear = static_cast<WORD>(st->cfg.targetTime.tm_year + 1900);
        cur.wMonth = static_cast<WORD>(st->cfg.targetTime.tm_mon + 1);
        cur.wDay = static_cast<WORD>(st->cfg.targetTime.tm_mday);
    }
    int year = static_cast<int>(cur.wYear) + delta;
    if (year < 1753) year = 1753;
    if (year > 9999) year = 9999;
    if (year != static_cast<int>(cur.wYear)) {
        cur.wYear = static_cast<WORD>(year);
        SendMessageW(st->hCalPopup, MCM_SETCURSEL, 0, (LPARAM)&cur);
    }
    SetFocus(st->hCalPopup);
}

static void OpenCalendarPopup(SettingsState* st) {
    if (!st || !st->dlg || !st->hBtnCalendar) return;
    if (st->hCalPopup) {
        CloseCalendarPopup(st);
        return;
    }
    CloseDropdown(st); // 两个浮层不叠加

    // 子窗口形式：与设置窗口同属一个消息循环，焦点/通知稳定
    HWND cal = CreateWindowExW(0, MONTHCAL_CLASSW, L"",
        WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 10, 10,
        st->dlg, (HMENU)(INT_PTR)IDC_CAL_POPUP, InstOf(st->dlg), nullptr);
    if (!cal) return;

    // 关闭月历的视觉样式，MCM_SETCOLOR 才会生效，从而得到深色月历
    SetWindowTheme(cal, L"", L"");
    SendMessageW(cal, MCM_SETCOLOR, MCSC_BACKGROUND, (LPARAM)kBg);
    SendMessageW(cal, MCM_SETCOLOR, MCSC_MONTHBK, (LPARAM)kChipBg);
    SendMessageW(cal, MCM_SETCOLOR, MCSC_TEXT, (LPARAM)kValueText);
    SendMessageW(cal, MCM_SETCOLOR, MCSC_TITLEBK, (LPARAM)kAccentDim);
    SendMessageW(cal, MCM_SETCOLOR, MCSC_TITLETEXT, (LPARAM)RGB(235, 239, 255));
    SendMessageW(cal, MCM_SETCOLOR, MCSC_TRAILINGTEXT, (LPARAM)kMuted);

    RECT need{};
    SendMessageW(cal, MCM_GETMINREQRECT, 0, (LPARAM)&need);
    int cw = need.right > 0 ? need.right : st->S(240);
    int ch = need.bottom > 0 ? need.bottom : st->S(200);
    int barH = st->S(24);
    int gap = st->S(4);

    RECT anchor{};
    GetWindowRect(st->hBtnCalendar, &anchor);
    MapWindowPoints(HWND_DESKTOP, st->dlg,
                    reinterpret_cast<POINT*>(&anchor), 2);

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
    if (y + blockH > footerY) {
        ch = footerY - y - barH - gap;
        if (ch < st->S(100)) {
            DestroyWindow(cal);
            return;
        }
    }
    if (cw < st->S(180)) {
        DestroyWindow(cal);
        return;
    }
    if (x < st->S(kPad)) x = st->S(kPad);
    if (x + cw > cr.right - st->S(kPad)) x = cr.right - st->S(kPad) - cw;
    if (y < st->S(kPad)) y = st->S(kPad);

    // 年份切换按钮：两条等宽胶囊铺满月历上方，避免点击空隙
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

    // 当前选中日期：优先取日期胶囊文本，失败则用配置值
    wchar_t dateBuf[64] = {0};
    GetWindowTextW(st->hEditDate, dateBuf, 64);
    SYSTEMTIME cur{};
    int yy = 0, mm = 0, dd = 0;
    if (swscanf(dateBuf, L"%d-%d-%d", &yy, &mm, &dd) == 3 &&
        mm >= 1 && mm <= 12 && dd >= 1 && dd <= 31) {
        cur.wYear = static_cast<WORD>(yy);
        cur.wMonth = static_cast<WORD>(mm);
        cur.wDay = static_cast<WORD>(dd);
    } else {
        cur.wYear = static_cast<WORD>(st->cfg.targetTime.tm_year + 1900);
        cur.wMonth = static_cast<WORD>(st->cfg.targetTime.tm_mon + 1);
        cur.wDay = static_cast<WORD>(st->cfg.targetTime.tm_mday);
    }
    SendMessageW(cal, MCM_SETCURSEL, 0, (LPARAM)&cur);

    SetWindowPos(cal, HWND_TOP, x, y + barH + gap, cw, ch, SWP_SHOWWINDOW);
    st->hCalPopup = cal;
    SetFocus(cal);
}

// ---------------------------------------------------------------------------
// 创建所有控件
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

    LayoutTabs(st, hwnd);

    RECT cr{};
    GetClientRect(hwnd, &cr);
    const int right = cr.right - st->S(kPad);
    const int rowH = st->S(kRowH);
    const int top = st->S(kContentTop);
    int y = top;

    auto makeSlots = [&](int rowY, std::initializer_list<int> widths) {
        std::vector<RECT> out;
        int total = 0;
        int n = static_cast<int>(widths.size());
        for (int w : widths) total += st->S(w);
        if (n > 1) total += st->S(6) * (n - 1);
        int x = right - total;
        for (int w : widths) {
            int cw = st->S(w);
            out.push_back(RECT{x, rowY, x + cw, rowY + rowH});
            x += cw + st->S(6);
        }
        return out;
    };

    auto label = [&](const wchar_t* text, int rowY, int page) {
        CreateLabel(st, hwnd, text, st->S(kPad), rowY, page);
    };

    auto unit = [&](int page, RECT rc, const wchar_t* text) {
        HWND h = CreateWindowExW(0, L"STATIC", text,
            WS_CHILD | WS_VISIBLE | SS_CENTER | SS_CENTERIMAGE,
            rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
            hwnd, nullptr, InstOf(hwnd), nullptr);
        if (h) st->pages[page].push_back(h);
    };

    auto edit = [&](int page, RECT slot, const wchar_t* text,
                    DWORD style, int id) -> HWND {
        st->chips[page].push_back(ChipSlot{nullptr, slot});
        int ix = st->S(8);
        int iy = st->S(3);
        HWND h = CreateChild(st, page, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | style, 0,
            slot.left + ix, slot.top + iy,
            (slot.right - slot.left) - 2 * ix,
            (slot.bottom - slot.top) - 2 * iy,
            id, hwnd);
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
            0, hwnd);
        DropdownDef def;
        def.trigger = btn;
        def.kind = kind;
        def.items = std::move(items);
        def.current = current;
        st->dropdowns.push_back(std::move(def));
        return btn;
    };

    // 数字步进器：左侧 −，右侧 +，中间为可输入数值
    auto stepper = [&](int page, RECT slot, const wchar_t* value, int id,
                       int lo, int hi, DWORD editStyle) -> HWND {
        st->chips[page].push_back(ChipSlot{nullptr, slot});
        int pad = st->S(3);
        int btnW = st->S(22);
        int innerH = slot.bottom - slot.top - 2 * pad;
        HWND minus = CreateChild(st, page, L"BUTTON", L"\u2212",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.left + pad, slot.top + pad, btnW, innerH,
            IDC_BTN_STEP_MINUS, hwnd, false);
        HWND plus = CreateChild(st, page, L"BUTTON", L"+",
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0,
            slot.right - pad - btnW, slot.top + pad, btnW, innerH,
            IDC_BTN_STEP_PLUS, hwnd, false);
        int leftX = slot.left + pad + btnW + st->S(2);
        int rightX = slot.right - pad - btnW - st->S(2);
        HWND e = CreateChild(st, page, L"EDIT", value,
            WS_CHILD | WS_VISIBLE | ES_CENTER | ES_AUTOHSCROLL | editStyle, 0,
            leftX, slot.top + st->S(4), rightX - leftX,
            (slot.bottom - slot.top) - st->S(8), id, hwnd);
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
            id, hwnd);
    };

    auto divider = [&](int page, int rowY) {
        st->rowDividerY[page].push_back(rowY + rowH + st->S(3));
    };

    auto intText = [](int v) {
        wchar_t buf[32];
        swprintf_s(buf, L"%d", v);
        return std::wstring(buf);
    };

    // ======================= 页 1：基本 =======================
    y = top;
    {
        const int page = PageBasic;
        int rowY = y;
        label(L"\u5012\u8ba1\u65f6\u6807\u9898", rowY, page);
        st->hEditTitle = edit(page, makeSlots(rowY, {300})[0],
                              st->cfg.title.c_str(), ES_AUTOHSCROLL,
                              IDC_EDIT_TITLE);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u76ee\u6807\u65e5\u671f", rowY, page); // 目标日期
        auto slots = makeSlots(rowY, {140, 34});
        st->hEditDate = edit(page, slots[0],
                             FormatDateText(st->cfg.targetTime).c_str(),
                             ES_AUTOHSCROLL, IDC_DTP_DATE);
        st->hBtnCalendar = chipButton(page, slots[1], L"", IDC_BTN_CALENDAR);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u76ee\u6807\u65f6\u95f4", rowY, page); // 目标时间
        st->hEditTime = edit(page, makeSlots(rowY, {110})[0],
                             FormatClockText(st->cfg.targetTime).c_str(),
                             ES_AUTOHSCROLL, IDC_DTP_TIME);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5faa\u73af", rowY, page); // 循环
        st->hBtnCycle = dropdown(page, makeSlots(rowY, {120})[0], DropCycle,
                                 CycleItems(),
                                 static_cast<int>(st->cfg.cycle));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u8d77\u59cb\u65e5\u8ba1\u5165", rowY, page); // 起始日计入
        st->hBtnStart = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                   IDC_CHECK_INCLUDE_START);
        divider(page, rowY); y += st->S(kRowStep);
    }

    // ======================= 页 2：外观 =======================
    y = top;
    {
        const int page = PageAppearance;
        int rowY = y;
        label(L"\u663e\u793a\u683c\u5f0f", rowY, page); // 显示格式
        st->hBtnFormat = dropdown(page, makeSlots(rowY, {170})[0], DropFormat,
                                  FormatItems(),
                                  static_cast<int>(st->cfg.displayFormat));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5b57\u53f7\uff08\u50cf\u7d20\uff09", rowY, page); // 字号（像素）
        st->hEditSize = stepper(page, makeSlots(rowY, {110})[0],
                                intText(st->cfg.fontSize).c_str(),
                                IDC_EDIT_FONTSIZE, 8, 200, ES_NUMBER);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5b57\u4f53", rowY, page); // 字体
        if (st->cfg.font.empty()) {
            st->cfg.font = utils::GetSystemDefaultFontName();
        }
        auto fonts = utils::EnumerateSystemFonts();
        if (fonts.empty()) fonts.push_back(st->cfg.font);
        int fontIdx = -1;
        for (size_t i = 0; i < fonts.size(); ++i) {
            if (fonts[i] == st->cfg.font) {
                fontIdx = static_cast<int>(i);
                break;
            }
        }
        if (fontIdx < 0) {
            // 配置里的字体不在系统列表（例如从别处拷来的配置）：
            // 保留它并插到第一项，保证下拉显示与配置一致
            fonts.insert(fonts.begin(), st->cfg.font);
            fontIdx = 0;
        }
        dropdown(page, makeSlots(rowY, {280})[0], DropFont,
                 std::move(fonts), fontIdx);
        divider(page, rowY); y += st->S(kRowStep);
    }

    // ======================= 页 3：一言 =======================
    y = top;
    {
        const int page = PageHitokoto;
        int rowY = y;
        label(L"\u4e00\u8a00\u6765\u6e90", rowY, page); // 一言来源
        st->hBtnHkSrc = dropdown(page, makeSlots(rowY, {120})[0], DropSource,
                                 SourceItems(),
                                 static_cast<int>(st->cfg.hitokotoSource));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5237\u65b0\u65f6\u95f4\uff08\u79d2\uff09", rowY, page); // 刷新时间（秒）
        st->hEditHkRefresh = stepper(page, makeSlots(rowY, {116})[0],
                                     intText(st->cfg.hitokotoRefreshSec).c_str(),
                                     IDC_EDIT_HITOKOTO_REFRESH,
                                     5, 3600, ES_NUMBER);
        divider(page, rowY); y += st->S(kRowStep);
    }

    // ======================= 页 4：一文 =======================
    y = top;
    {
        const int page = PageArticle;
        int rowY = y;
        label(L"\u680f\u76ee", rowY, page); // 栏目
        st->hBtnArticle = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                     IDC_CHECK_ARTICLE);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u7a97\u53e3\u4f4d\u7f6e", rowY, page); // 窗口位置
        st->hBtnArtAnchor = dropdown(page, makeSlots(rowY, {140})[0],
                                     DropArtAnchor, AnchorItems(),
                                     static_cast<int>(st->cfg.articleAnchor));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u504f\u79fb\uff08\u50cf\u7d20\uff09", rowY, page); // 偏移（像素）
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
        label(L"\u5237\u65b0\u65f6\u95f4", rowY, page); // 刷新时间
        int totalSec = st->cfg.articleRefreshSec;
        int artH = totalSec / 3600;
        int artM = (totalSec / 60) % 60;
        int artS = totalSec % 60;
        slots = makeSlots(rowY, {90, 14, 90, 14, 90, 14});
        st->hEditArtH = stepper(page, slots[0], intText(artH).c_str(),
                                IDC_EDIT_ARTICLE_H, 0, 23, ES_NUMBER);
        unit(page, slots[1], L"\u65f6"); // 时
        st->hEditArtM = stepper(page, slots[2], intText(artM).c_str(),
                                IDC_EDIT_ARTICLE_M, 0, 59, ES_NUMBER);
        unit(page, slots[3], L"\u5206"); // 分
        st->hEditArtS = stepper(page, slots[4], intText(artS).c_str(),
                                IDC_EDIT_ARTICLE_S, 0, 59, ES_NUMBER);
        unit(page, slots[5], L"\u79d2"); // 秒
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u59cb\u7ec8\u7f6e\u9876", rowY, page); // 始终置顶
        st->hBtnArtTop = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                    IDC_CHECK_ARTICLE_TOP);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u6b63\u6587\u5b57\u53f7", rowY, page); // 正文字号
        st->hEditArtFont = stepper(page, makeSlots(rowY, {110})[0],
                                   intText(st->cfg.articleFontSize).c_str(),
                                   IDC_EDIT_ARTICLE_FONT, 8, 96, ES_NUMBER);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5c3a\u5bf8\u6a21\u5f0f", rowY, page); // 尺寸模式
        st->hBtnArtSize = dropdown(page, makeSlots(rowY, {180})[0],
                                   DropArtSize, ArticleSizeItems(),
                                   static_cast<int>(st->cfg.articleSizeMode));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5c3a\u5bf8\uff08\u50cf\u7d20\uff09", rowY, page); // 尺寸（像素）
        slots = makeSlots(rowY, {116, 14, 116, 14});
        st->hEditArtWidth = stepper(page, slots[0],
                                    intText(st->cfg.articleWidth).c_str(),
                                    IDC_EDIT_ARTICLE_WIDTH, 180, 1200,
                                    ES_NUMBER);
        unit(page, slots[1], L"\u5bbd"); // 宽
        st->hEditArtHeight = stepper(page, slots[2],
                                     intText(st->cfg.articleHeight).c_str(),
                                     IDC_EDIT_ARTICLE_HEIGHT, 120, 2000,
                                     ES_NUMBER);
        unit(page, slots[3], L"\u9ad8"); // 高
        divider(page, rowY); y += st->S(kRowStep);
    }

    // ======================= 页 5：窗口与启动 =======================
    y = top;
    {
        const int page = PageWindow;
        int rowY = y;
        label(L"\u7a97\u53e3\u4f4d\u7f6e", rowY, page); // 窗口位置
        st->hBtnAnchor = dropdown(page, makeSlots(rowY, {140})[0],
                                  DropWinAnchor, AnchorItems(),
                                  static_cast<int>(st->cfg.anchor));
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u504f\u79fb\uff08\u50cf\u7d20\uff09", rowY, page); // 偏移（像素）
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
        label(L"\u59cb\u7ec8\u7f6e\u9876", rowY, page); // 始终置顶
        st->hBtnTop = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                 IDC_CHECK_TOPMOST);
        divider(page, rowY); y += st->S(kRowStep);

        rowY = y;
        label(L"\u5f00\u673a\u81ea\u542f\u52a8", rowY, page); // 开机自启动
        st->hBtnAuto = chipButton(page, makeSlots(rowY, {64})[0], L"",
                                  IDC_CHECK_AUTOSTART);
        divider(page, rowY); y += st->S(kRowStep);
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

    ShowPage(st, PageBasic);
    SetFocus(st->hEditTitle);
    SendMessageW(st->hEditTitle, EM_SETSEL, 0, 0);
}

// ---------------------------------------------------------------------------
// 从控件收集值并校验
// ---------------------------------------------------------------------------
static bool CollectAndValidate(HWND hwnd, SettingsState* st) {
    wchar_t buf[256];

    // 标题
    GetWindowTextW(st->hEditTitle, buf, 256);
    st->cfg.title = buf;
    if (st->cfg.title.empty()) {
        MessageBoxW(hwnd, L"\u6807\u9898\u4e0d\u80fd\u4e3a\u7a7a",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageBasic);
        SetFocus(st->hEditTitle);
        return false;
    }

    // 目标时间：日期 + 时间文本合并后严格解析
    wchar_t dateBuf[64] = {0};
    wchar_t clockBuf[64] = {0};
    GetWindowTextW(st->hEditDate, dateBuf, 64);
    GetWindowTextW(st->hEditTime, clockBuf, 64);
    std::wstring full = std::wstring(dateBuf) + L" " + clockBuf;
    std::tm target{};
    if (!utils::ParseTargetTime(full, target)) {
        MessageBoxW(hwnd,
            L"\u76ee\u6807\u65f6\u95f4\u683c\u5f0f\u65e0\u6548"
            L"\uff08\u65e5\u671f YYYY-MM-DD\uff0c\u65f6\u95f4 HH:MM:SS\uff09",
            L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageBasic);
        SetFocus(st->hEditDate);
        return false;
    }
    st->cfg.targetTime = target;

    // 循环/显示格式/一言来源/位置/尺寸模式：点击时装已写入 cfg
    st->cfg.includeStartDay = st->optStart;

    // 字号
    int size = 0;
    if (!ParseIntField(st->hEditSize, size) || size < 8 || size > 200) {
        MessageBoxW(hwnd, L"\u5b57\u53f7\u5e94\u5728 8\u2013200 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageAppearance);
        SetFocus(st->hEditSize);
        return false;
    }
    st->cfg.fontSize = size;

    // 字体：由下拉列表直接写入 cfg，这里只兜底默认值
    if (st->cfg.font.empty()) st->cfg.font = utils::GetSystemDefaultFontName();

    // 一言刷新时间
    int sec = 0;
    if (!ParseIntField(st->hEditHkRefresh, sec) || sec < 5 || sec > 3600) {
        MessageBoxW(hwnd, L"\u5237\u65b0\u65f6\u95f4\u5e94\u5728 5\u20133600 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageHitokoto);
        SetFocus(st->hEditHkRefresh);
        return false;
    }
    st->cfg.hitokotoRefreshSec = sec;

    // 一文栏目
    st->cfg.articleEnabled = st->optArticle;

    // 一文偏移
    int artX = 0, artY = 0;
    if (!ParseIntField(st->hEditArtX, artX) || !ParseIntField(st->hEditArtY, artY)) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u504f\u79fb\u5fc5\u987b\u662f\u6574\u6570",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtX);
        return false;
    }
    if (artX < -10000 || artX > 10000 || artY < -10000 || artY > 10000) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtX);
        return false;
    }
    st->cfg.articlePlaceX = artX;
    st->cfg.articlePlaceY = artY;

    // 一文刷新间隔
    int artH = 0, artM = 0, artS = 0;
    if (!ParseIntField(st->hEditArtH, artH) ||
        !ParseIntField(st->hEditArtM, artM) ||
        !ParseIntField(st->hEditArtS, artS)) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5237\u65b0\u65f6\u95f4\u5fc5\u987b\u662f\u6574\u6570",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtH);
        return false;
    }
    int artTotal = artH * 3600 + artM * 60 + artS;
    if (artH < 0 || artH > 23 || artM < 0 || artM > 59
        || artS < 0 || artS > 59
        || artTotal < 1 || artTotal > 86399) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5237\u65b0\u65f6\u95f4\u5e94\u5728 00:00:01\u201323:59:59 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtH);
        return false;
    }
    st->cfg.articleRefreshSec = artTotal;

    st->cfg.articleIsTop = st->optArtTop;

    // 一文字号
    int artFont = 0;
    if (!ParseIntField(st->hEditArtFont, artFont) || artFont < 8 || artFont > 96) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5b57\u53f7\u5e94\u5728 8\u201396 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtFont);
        return false;
    }
    st->cfg.articleFontSize = artFont;

    // 一文尺寸
    int artW = 0, artHgt = 0;
    if (!ParseIntField(st->hEditArtWidth, artW)
        || !ParseIntField(st->hEditArtHeight, artHgt)) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5c3a\u5bf8\u5fc5\u987b\u662f\u6574\u6570",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtWidth);
        return false;
    }
    if (artW < 180 || artW > 1200 || artHgt < 120 || artHgt > 2000) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5c3a\u5bf8\u5e94\u4e3a\uff1a\u5bbd 180\u20131200\u3001\u9ad8 120\u20132000",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtWidth);
        return false;
    }
    st->cfg.articleWidth = artW;
    st->cfg.articleHeight = artHgt;

    // 始终置顶 / 开机自启动
    st->cfg.isTop = st->optTopMost;
    st->cfg.autoStart = st->optAutoStart;

    // 窗口偏移
    int px = 0, py = 0;
    if (!ParseIntField(st->hEditPlaceX, px) || !ParseIntField(st->hEditPlaceY, py)) {
        MessageBoxW(hwnd, L"\u504f\u79fb\u5fc5\u987b\u662f\u6574\u6570",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageWindow);
        SetFocus(st->hEditPlaceX);
        return false;
    }
    if (px < -10000 || px > 10000 || py < -10000 || py > 10000) {
        MessageBoxW(hwnd, L"\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        ShowPage(st, PageWindow);
        SetFocus(st->hEditPlaceX);
        return false;
    }
    st->cfg.placeX = px;
    st->cfg.placeY = py;

    if (!st->cfg.Save()) {
        MessageBoxW(hwnd, L"\u4fdd\u5b58\u914d\u7f6e\u5931\u8d25",
                    L"\u9519\u8bef", MB_ICONERROR);
        return false;
    }
    if (!autostart::SetEnabled(st->cfg.autoStart)) {
        MessageBoxW(hwnd, L"\u5f00\u673a\u81ea\u542f\u52a8\u8bbe\u7f6e\u5931\u8d25\uff08\u914d\u7f6e\u5df2\u4fdd\u5b58\uff09",
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
            return 0; // 继续创建窗口

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
            // 顶部窄条与标签栏右侧空白区域可拖动窗口
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
                    if (st->page != i) ShowPage(st, i);
                    return 0;
                }
            }
            break;
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
            SetBkMode(dc, TRANSPARENT);
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

        case DM_GETDEFID: // 让 IsDialogMessage 的 Enter 键落到“保存”
            return MAKELRESULT(IDC_BTN_SAVE, DC_HASDEFID);

        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (st && nm && nm->hwndFrom == st->hCalPopup) {
                if (nm->code == MCN_SELECT) {
                    auto* sel = reinterpret_cast<NMSELCHANGE*>(lp);
                    wchar_t dateText[32];
                    swprintf_s(dateText, L"%04d-%02d-%02d",
                               sel->stSelStart.wYear, sel->stSelStart.wMonth,
                               sel->stSelStart.wDay);
                    SetWindowTextW(st->hEditDate, dateText);
                    // 延后关闭：不要在月历自身的通知里销毁月历
                    PostMessageW(st->dlg, WM_APP_CLOSE_CALENDAR, 0, 0);
                    return 0;
                }
                // 不处理 NM_KILLFOCUS：年月按钮会临时拿走焦点，
                // 若在此关闭月历会导致年份按钮点不动；关闭统一走
                // WM_COMMAND 的焦点通知 + 背景点击 + WM_ACTIVATE
            }
            break;
        }

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

            // 焦点切换时重绘值胶囊的强调边框
            if (code == EN_SETFOCUS || code == EN_KILLFOCUS ||
                code == CBN_SETFOCUS || code == CBN_KILLFOCUS ||
                code == BN_SETFOCUS || code == BN_KILLFOCUS) {
                InvalidateRect(hwnd, nullptr, TRUE);
            }

            // 点击其它控件时收起浮层（用焦点通知判断，避免 WM_PARENTNOTIFY
            // 的 lParam 坐标被误当成窗口句柄导致浮层被立刻关闭）
            if (code == EN_SETFOCUS || code == CBN_SETFOCUS || code == BN_SETFOCUS) {
                if (st->hDropdown && ctl != st->hDropdownTrigger) {
                    CloseDropdown(st);
                }
                if (st->hCalPopup && ctl != st->hBtnCalendar &&
                    ctl != st->hCalPrevYear && ctl != st->hCalNextYear) {
                    CloseCalendarPopup(st);
                }
            }

            if (ctl) {
                // 下拉触发按钮：点击展开/收起
                for (auto& d : st->dropdowns) {
                    if (d.trigger == ctl) {
                        if (code == BN_CLICKED) OpenDropdown(st, &d);
                        return 0;
                    }
                }
                // 步进器的 − / + 按钮
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

                case IDC_CAL_PREV_YEAR:
                    if (code == BN_CLICKED) AdjustCalendarYear(st, -1);
                    return 0;

                case IDC_CAL_NEXT_YEAR:
                    if (code == BN_CLICKED) AdjustCalendarYear(st, +1);
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
    // 防重入：已有设置窗口时忽略本次调用
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

    // 窗口尺寸按当前显示器工作区裁剪，避免高缩放下底部按钮被裁掉
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

    // 字体必须先于窗口创建（OnCreate 需要用它测量标签页）
    st.font = CreateFontW(-st.S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE,
                          utils::GetSystemDefaultFontName().c_str());
    st.boldFont = CreateFontW(-st.S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_DONTCARE,
                              utils::GetSystemDefaultFontName().c_str());

    st.dlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass,
        L"\u8bbe\u7f6e",
        WS_POPUP | WS_CLIPCHILDREN,
        x, y, winW, winH,
        parent, nullptr, hInst, &st);

    if (!st.dlg) {
        DWORD err = GetLastError();
        wchar_t buf[128];
        swprintf_s(buf, 128, L"CreateWindowEx failed, GetLastError=%lu", err);
        MessageBoxW(parent, buf, L"\u9519\u8bef", MB_ICONERROR);
        if (st.font) DeleteObject(st.font);
        if (st.boldFont) DeleteObject(st.boldFont);
        return false;
    }

    // 圆角窗口
    SetWindowRgn(st.dlg,
        CreateRoundRectRgn(0, 0, winW + 1, winH + 1, st.S(24), st.S(24)), TRUE);

    EnableWindow(parent, FALSE);
    ShowWindow(st.dlg, SW_SHOW);
    UpdateWindow(st.dlg);

    // 模态消息循环。需自行处理三种提前退出情形，避免吞掉退出消息或留下无主窗口：
    //  1) 设置窗口关闭 -> st.running=false；
    //  2) 父窗口先被销毁（如托盘“退出”）；
    //  3) 收到 WM_QUIT -> 重新投递，交给外层主消息循环
    MSG msg;
    while (st.running) {
        if (st.parent && !IsWindow(st.parent)) break;
        BOOL got = GetMessage(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        // 浮层打开时 Esc 只收起浮层，不关闭整个设置窗口
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE &&
            (st.hDropdown || st.hCalPopup)) {
            CloseDropdown(&st);
            CloseCalendarPopup(&st);
            continue;
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

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (st.font) DeleteObject(st.font);
    if (st.boldFont) DeleteObject(st.boldFont);
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
    // 设置窗口当前位于它自己的模态消息循环中；这里同步关闭后，
    // OpenSettingsDialog 的循环会因 running=false 正常退出并返回调用方
    if (HWND dlg = FindWindowW(kClass, nullptr)) {
        SendMessageW(dlg, WM_CLOSE, 0, 0);
    }
}
