// =============================================================================
// settings_window.cpp - 设置窗口实现
//
// 自定义窗口 + 四标签页（基本 / 外观 / 一言 / 窗口与启动）。
// 保存后写回 config.json；返回 true 时调用方应用新配置。
// =============================================================================
#include "settings_window.h"
#include "../resource.h"
#include "autostart.h"
#include "utils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// 布局常量（96 DPI 下的逻辑尺寸，运行时按 DPI 缩放）
static const int kPadding = 18;
static const int kLabelW  = 92;
static const int kRowH    = 26;
static const int kRowGap  = 12;
static const int kWinW    = 560;
static const int kWinH    = 500;
static const int kTabH    = 400;
static const int kTabCount = 5;

enum SettingsPage {
    PageBasic = 0,
    PageAppearance,
    PageHitokoto,
    PageArticle,
    PageWindow,
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

    // 控件句柄
    HWND hTab         = nullptr;
    HWND hEditTitle   = nullptr;
    HWND hDtpDate     = nullptr;
    HWND hDtpTime     = nullptr;
    HWND hCheckCycle  = nullptr;
    HWND hComboCycle  = nullptr;
    HWND hCheckStart  = nullptr;
    HWND hComboFmt    = nullptr;
    HWND hEditSize    = nullptr;
    HWND hUdSize      = nullptr;
    HWND hComboFont   = nullptr;
    HWND hComboHkSrc  = nullptr;
    HWND hEditHkRefresh = nullptr;
    HWND hUdHkRefresh = nullptr;
    HWND hCheckArticle = nullptr;
    HWND hComboArtAnchor = nullptr;
    HWND hEditArtX     = nullptr;
    HWND hEditArtY     = nullptr;
    HWND hEditArtH     = nullptr;
    HWND hUdArtH       = nullptr;
    HWND hEditArtM     = nullptr;
    HWND hUdArtM       = nullptr;
    HWND hEditArtS     = nullptr;
    HWND hUdArtS       = nullptr;
    HWND hCheckArtTop  = nullptr;
    HWND hEditArtFont  = nullptr;
    HWND hUdArtFont    = nullptr;
    HWND hComboArtSize = nullptr;
    HWND hEditArtWidth = nullptr;
    HWND hEditArtHeight = nullptr;
    HWND hCheckTop    = nullptr;
    HWND hComboAnchor = nullptr;
    HWND hEditPlaceX  = nullptr;
    HWND hEditPlaceY  = nullptr;
    HWND hCheckAuto   = nullptr;

    RECT pageRect{};
    std::vector<HWND> pages[kTabCount];

    int S(int value) const { return utils::ScaleDpi(value, static_cast<int>(dpi)); }
};

// ---------------------------------------------------------------------------
static const wchar_t* kClass = L"CountdownSettingsClass";

// 前置声明
static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
static void OnCreate(HWND hwnd, SettingsState* st);
static bool CollectAndValidate(HWND hwnd, SettingsState* st);
static void ShowPage(SettingsState* st, int page);

// 注意：函数名刻意避开 RegisterClass（windows.h 中是宏，会展开为 RegisterClassW）
static bool RegisterSettingsClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kClass, &exist) != 0) return true; // 已注册
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = SettingsWndProc; // 使用自有窗口过程
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = kClass;
    wc.hIcon         = nullptr;
    return RegisterClassExW(&wc) != 0;
}

// ---------------------------------------------------------------------------
// 创建子控件的小工具
// ---------------------------------------------------------------------------
static HWND CreateLabel(SettingsState* st, HWND parent, const wchar_t* text,
                        int x, int y, int page) {
    HWND h = CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                           x, y, st->S(kLabelW), st->S(kRowH), parent, nullptr,
                           (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                           nullptr);
    if (h) st->pages[page].push_back(h);
    return h;
}

static HWND CreateChild(SettingsState* st, int page, const wchar_t* cls,
                        const wchar_t* text, DWORD style, DWORD exStyle,
                        int x, int y, int w, int h, int id, HWND parent) {
    HWND hwnd = CreateWindowExW(exStyle, cls, text, style, x, y, w, h,
                                parent, (HMENU)(INT_PTR)id,
                                (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE),
                                nullptr);
    if (hwnd) st->pages[page].push_back(hwnd);
    return hwnd;
}

// 把 std::tm 转为 SYSTEMTIME
static void TmToSystem(const std::tm& t, SYSTEMTIME& st) {
    st.wYear   = static_cast<WORD>(t.tm_year + 1900);
    st.wMonth  = static_cast<WORD>(t.tm_mon + 1);
    st.wDay    = static_cast<WORD>(t.tm_mday);
    st.wDayOfWeek = 0;
    st.wHour   = static_cast<WORD>(t.tm_hour);
    st.wMinute = static_cast<WORD>(t.tm_min);
    st.wSecond = static_cast<WORD>(t.tm_sec);
    st.wMilliseconds = 0;
}

static void SystemToTm(const SYSTEMTIME& st, std::tm& t) {
    t = std::tm{};
    t.tm_year = st.wYear - 1900;
    t.tm_mon  = st.wMonth - 1;
    t.tm_mday = st.wDay;
    t.tm_hour = st.wHour;
    t.tm_min  = st.wMinute;
    t.tm_sec  = st.wSecond;
    t.tm_isdst = -1;
}

// ---------------------------------------------------------------------------
// 标签页切换
// ---------------------------------------------------------------------------
static void ShowPage(SettingsState* st, int page) {
    for (int i = 0; i < kTabCount; ++i) {
        int cmd = (i == page) ? SW_SHOW : SW_HIDE;
        for (HWND h : st->pages[i]) {
            if (h) ShowWindow(h, cmd);
        }
    }
}

// 按尺寸模式启用/禁用宽高输入
static void UpdateArticleSizeControls(SettingsState* st) {
    if (!st || !st->hComboArtSize) return;
    int mode = ComboBox_GetCurSel(st->hComboArtSize);
    bool widthEnabled  = (mode == 0 || mode == 2); // 固定宽 / 自定义
    bool heightEnabled = (mode == 2);              // 仅自定义
    if (st->hEditArtWidth)  EnableWindow(st->hEditArtWidth, widthEnabled ? TRUE : FALSE);
    if (st->hEditArtHeight) EnableWindow(st->hEditArtHeight, heightEnabled ? TRUE : FALSE);
}

// ---------------------------------------------------------------------------
// WM_CREATE 处理：创建标签页与所有子控件，并填入当前值
// ---------------------------------------------------------------------------
static void OnCreate(HWND hwnd, SettingsState* st) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);

    // 标签控件
    st->hTab = CreateWindowExW(0, WC_TABCONTROLW, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | TCS_HOTTRACK | TCS_FOCUSNEVER
            | TCS_OWNERDRAWFIXED,
        st->S(kPadding), st->S(kPadding),
        st->S(kWinW - 2 * kPadding), st->S(kTabH),
        hwnd, (HMENU)IDC_TAB_MAIN, hInst, nullptr);

    static const wchar_t* kTabNames[kTabCount] = {
        L"\u57fa\u672c",        // 基本
        L"\u5916\u89c2",        // 外观
        L"\u4e00\u8a00",        // 一言
        L"\u4e00\u6587",        // 一文
        L"\u7a97\u53e3\u4e0e\u542f\u52a8", // 窗口与启动
    };
    for (int i = 0; i < kTabCount; ++i) {
        TCITEMW item{};
        item.mask = TCIF_TEXT;
        item.pszText = const_cast<wchar_t*>(kTabNames[i]);
        TabCtrl_InsertItem(st->hTab, i, &item);
    }
    TabCtrl_SetCurSel(st->hTab, 0);
    // 统一标签宽度，选中态更醒目
    TabCtrl_SetItemSize(st->hTab, st->S(96), st->S(28));

    // 页内容区域（标签控件客户区 -> 显示区域，再偏移到父窗口坐标）
    RECT rc = {0, 0, st->S(kWinW - 2 * kPadding), st->S(kTabH)};
    TabCtrl_AdjustRect(st->hTab, FALSE, &rc);
    OffsetRect(&rc, st->S(kPadding), st->S(kPadding));
    st->pageRect = rc;

    const int ctrlX = rc.left + st->S(kLabelW) + st->S(10);
    const int ctrlW = rc.right - ctrlX - st->S(8);
    int y = rc.top + st->S(4);
    const int rowStep = st->S(kRowH) + st->S(kRowGap);

    // ======================= 页 1：基本 =======================
    CreateLabel(st, hwnd, L"\u5012\u8ba1\u65f6\u6807\u9898", rc.left, y, PageBasic);
    st->hEditTitle = CreateChild(st, PageBasic, L"EDIT", st->cfg.title.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        ctrlX, y, ctrlW, st->S(kRowH), IDC_EDIT_TITLE, hwnd);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u76ee\u6807\u65f6\u95f4", rc.left, y, PageBasic);
    st->hDtpDate = CreateChild(st, PageBasic, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT, 0,
        ctrlX, y, st->S(170), st->S(kRowH), IDC_DTP_DATE, hwnd);
    st->hDtpTime = CreateChild(st, PageBasic, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT | DTS_UPDOWN, 0,
        ctrlX + st->S(178), y, st->S(118), st->S(kRowH), IDC_DTP_TIME, hwnd);

    SYSTEMTIME stDate{}, stTime{};
    TmToSystem(st->cfg.targetTime, stDate);
    TmToSystem(st->cfg.targetTime, stTime);
    DateTime_SetSystemtime(st->hDtpDate, GDT_VALID, &stDate);
    DateTime_SetSystemtime(st->hDtpTime, GDT_VALID, &stTime);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5faa\u73af", rc.left, y, PageBasic);
    st->hCheckCycle = CreateChild(st, PageBasic, L"BUTTON",
        L"\u5230\u671f\u540e\u81ea\u52a8\u5faa\u73af", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, ctrlX, y, st->S(150), st->S(kRowH), IDC_CHECK_CYCLE, hwnd);
    st->hComboCycle = CreateChild(st, PageBasic, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX + st->S(158), y, st->S(110), st->S(160), IDC_COMBO_CYCLE, hwnd);
    static const wchar_t* kCycles[] = {
        L"\u5e74",   // 年
        L"\u6708",   // 月
        L"\u65e5",   // 日
        L"\u65f6",   // 时
        L"\u5206",   // 分
        L"\u79d2",   // 秒
    };
    for (auto* s : kCycles) ComboBox_AddString(st->hComboCycle, s);
    bool cycleOn = st->cfg.cycle != CycleUnit::None;
    Button_SetCheck(st->hCheckCycle, cycleOn ? BST_CHECKED : BST_UNCHECKED);
    if (cycleOn) {
        ComboBox_SetCurSel(st->hComboCycle,
                           static_cast<int>(st->cfg.cycle) - 1);
    } else {
        ComboBox_SetCurSel(st->hComboCycle, 0);
    }
    EnableWindow(st->hComboCycle, cycleOn ? TRUE : FALSE);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u8d77\u59cb\u65e5", rc.left, y, PageBasic);
    st->hCheckStart = CreateChild(st, PageBasic, L"BUTTON",
        L"\u628a\u8d77\u59cb\u65e5\u8ba1\u5165\u5012\u8ba1\u65f6\uff08+1 \u5929\uff09",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
        ctrlX, y, ctrlW, st->S(kRowH), IDC_CHECK_INCLUDE_START, hwnd);
    Button_SetCheck(st->hCheckStart,
                    st->cfg.includeStartDay ? BST_CHECKED : BST_UNCHECKED);

    // ======================= 页 2：外观 =======================
    y = rc.top + st->S(4);
    CreateLabel(st, hwnd, L"\u663e\u793a\u683c\u5f0f", rc.left, y, PageAppearance);
    st->hComboFmt = CreateChild(st, PageAppearance, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX, y, st->S(230), st->S(140), IDC_COMBO_FORMAT, hwnd);
    ComboBox_AddString(st->hComboFmt, L"\u65f6:\u5206:\u79d2");                 // 时:分:秒
    ComboBox_AddString(st->hComboFmt, L"\u5e74\u6708\u65e5 \u65f6\u5206\u79d2"); // 年月日 时分秒
    ComboBox_AddString(st->hComboFmt, L"\u5929\u6570 \u65f6:\u5206:\u79d2");     // 天数 时:分:秒
    ComboBox_SetCurSel(st->hComboFmt,
        st->cfg.displayFormat == utils::DisplayFormat::Full ? 1 :
        st->cfg.displayFormat == utils::DisplayFormat::Days ? 2 : 0);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5b57\u53f7(\u50cf\u7d20)", rc.left, y, PageAppearance);
    st->hEditSize = CreateChild(st, PageAppearance, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(80), st->S(kRowH), IDC_EDIT_FONTSIZE, hwnd);
    st->hUdSize = CreateChild(st, PageAppearance, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_FONTSIZE, hwnd);
    SendMessageW(st->hUdSize, UDM_SETRANGE32, 8, 200);
    SendMessageW(st->hUdSize, UDM_SETPOS32, 0, st->cfg.fontSize);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5b57\u4f53", rc.left, y, PageAppearance);
    st->hComboFont = CreateChild(st, PageAppearance, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL, 0,
        ctrlX, y, ctrlW, st->S(260), IDC_COMBO_FONT, hwnd);

    auto fonts = utils::EnumerateSystemFonts();
    int sel = -1;
    for (size_t i = 0; i < fonts.size(); ++i) {
        ComboBox_AddString(st->hComboFont, fonts[i].c_str());
        if (fonts[i] == st->cfg.font) sel = static_cast<int>(i);
    }
    if (sel >= 0) {
        ComboBox_SetCurSel(st->hComboFont, sel);
    } else if (!fonts.empty()) {
        ComboBox_SetCurSel(st->hComboFont, 0);
    } else {
        ComboBox_AddString(st->hComboFont, st->cfg.font.c_str());
        ComboBox_SetCurSel(st->hComboFont, 0);
    }

    // ======================= 页 3：一言 =======================
    y = rc.top + st->S(4);
    CreateLabel(st, hwnd, L"\u4e00\u8a00\u6765\u6e90", rc.left, y, PageHitokoto);
    st->hComboHkSrc = CreateChild(st, PageHitokoto, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX, y, st->S(200), st->S(120), IDC_COMBO_HITOKOTO_SRC, hwnd);
    ComboBox_AddString(st->hComboHkSrc, L"\u672c\u5730");           // 本地
    ComboBox_AddString(st->hComboHkSrc, L"\u7f51\u7edc");           // 网络
    ComboBox_AddString(st->hComboHkSrc, L"\u672c\u5730\u548c\u7f51\u7edc"); // 本地和网络
    ComboBox_SetCurSel(st->hComboHkSrc, static_cast<int>(st->cfg.hitokotoSource));
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5237\u65b0\u65f6\u95f4(\u79d2)", rc.left, y, PageHitokoto);
    st->hEditHkRefresh = CreateChild(st, PageHitokoto, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(80), st->S(kRowH), IDC_EDIT_HITOKOTO_REFRESH, hwnd);
    st->hUdHkRefresh = CreateChild(st, PageHitokoto, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_HITOKOTO_REFRESH, hwnd);
    SendMessageW(st->hUdHkRefresh, UDM_SETRANGE32, 5, 3600);
    SendMessageW(st->hUdHkRefresh, UDM_SETPOS32, 0, st->cfg.hitokotoRefreshSec);

    // ======================= 页 4：一文 =======================
    y = rc.top + st->S(4);
    CreateLabel(st, hwnd, L"\u680f\u76ee", rc.left, y, PageArticle);
    st->hCheckArticle = CreateChild(st, PageArticle, L"BUTTON",
        L"\u663e\u793a\u4e00\u6587\u680f\u76ee", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, ctrlX, y, ctrlW, st->S(kRowH), IDC_CHECK_ARTICLE, hwnd);
    Button_SetCheck(st->hCheckArticle,
                    st->cfg.articleEnabled ? BST_CHECKED : BST_UNCHECKED);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u7a97\u53e3\u4f4d\u7f6e", rc.left, y, PageArticle);
    st->hComboArtAnchor = CreateChild(st, PageArticle, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX, y, st->S(180), st->S(220), IDC_COMBO_ARTICLE_ANCHOR, hwnd);
    static const wchar_t* kArticleAnchors[] = {
        L"\u5de6\u4e0a\u89d2",   L"\u9876\u90e8\u5c45\u4e2d", L"\u53f3\u4e0a\u89d2",
        L"\u5de6\u5c45\u4e2d",   L"\u5c4f\u5e55\u5c45\u4e2d", L"\u53f3\u5c45\u4e2d",
        L"\u5de6\u4e0b\u89d2",   L"\u5e95\u90e8\u5c45\u4e2d", L"\u53f3\u4e0b\u89d2",
    };
    for (auto* s : kArticleAnchors) ComboBox_AddString(st->hComboArtAnchor, s);
    ComboBox_SetCurSel(st->hComboArtAnchor, static_cast<int>(st->cfg.articleAnchor));
    y += rowStep;

    CreateLabel(st, hwnd, L"\u504f\u79fb(\u50cf\u7d20)", rc.left, y, PageArticle);
    st->hEditArtX = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(64), st->S(kRowH), IDC_EDIT_ARTICLE_X, hwnd);
    CreateChild(st, PageArticle, L"STATIC", L"X", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, ctrlX + st->S(70), y, st->S(18), st->S(kRowH), 0, hwnd);
    st->hEditArtY = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        ctrlX + st->S(94), y, st->S(64), st->S(kRowH), IDC_EDIT_ARTICLE_Y, hwnd);
    CreateChild(st, PageArticle, L"STATIC", L"Y", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, ctrlX + st->S(164), y, st->S(18), st->S(kRowH), 0, hwnd);
    wchar_t ab[32];
    swprintf_s(ab, L"%d", st->cfg.articlePlaceX);
    SetWindowTextW(st->hEditArtX, ab);
    swprintf_s(ab, L"%d", st->cfg.articlePlaceY);
    SetWindowTextW(st->hEditArtY, ab);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5237\u65b0\u65f6\u95f4", rc.left, y, PageArticle);
    int totalSec = st->cfg.articleRefreshSec;
    int artH = totalSec / 3600;
    int artM = (totalSec / 60) % 60;
    int artS = totalSec % 60;

    st->hEditArtH = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(56), st->S(kRowH), IDC_EDIT_ARTICLE_H, hwnd);
    st->hUdArtH = CreateChild(st, PageArticle, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_ARTICLE_H, hwnd);
    SendMessageW(st->hUdArtH, UDM_SETRANGE32, 0, 23);
    SendMessageW(st->hUdArtH, UDM_SETPOS32, 0, artH);
    CreateChild(st, PageArticle, L"STATIC", L"\u65f6", WS_CHILD | WS_VISIBLE,
        0, ctrlX + st->S(60), y, st->S(20), st->S(kRowH), 0, hwnd);

    st->hEditArtM = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX + st->S(84), y, st->S(56), st->S(kRowH), IDC_EDIT_ARTICLE_M, hwnd);
    st->hUdArtM = CreateChild(st, PageArticle, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_ARTICLE_M, hwnd);
    SendMessageW(st->hUdArtM, UDM_SETRANGE32, 0, 59);
    SendMessageW(st->hUdArtM, UDM_SETPOS32, 0, artM);
    CreateChild(st, PageArticle, L"STATIC", L"\u5206", WS_CHILD | WS_VISIBLE,
        0, ctrlX + st->S(144), y, st->S(20), st->S(kRowH), 0, hwnd);

    st->hEditArtS = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX + st->S(168), y, st->S(56), st->S(kRowH), IDC_EDIT_ARTICLE_S, hwnd);
    st->hUdArtS = CreateChild(st, PageArticle, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_ARTICLE_S, hwnd);
    SendMessageW(st->hUdArtS, UDM_SETRANGE32, 0, 59);
    SendMessageW(st->hUdArtS, UDM_SETPOS32, 0, artS);
    CreateChild(st, PageArticle, L"STATIC", L"\u79d2", WS_CHILD | WS_VISIBLE,
        0, ctrlX + st->S(228), y, st->S(20), st->S(kRowH), 0, hwnd);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u7f6e\u9876", rc.left, y, PageArticle);
    st->hCheckArtTop = CreateChild(st, PageArticle, L"BUTTON",
        L"\u4e00\u6587\u59cb\u7ec8\u7f6e\u9876", WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        0, ctrlX, y, ctrlW, st->S(kRowH), IDC_CHECK_ARTICLE_TOP, hwnd);
    Button_SetCheck(st->hCheckArtTop,
                    st->cfg.articleIsTop ? BST_CHECKED : BST_UNCHECKED);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5b57\u53f7(\u50cf\u7d20)", rc.left, y, PageArticle);
    st->hEditArtFont = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(56), st->S(kRowH), IDC_EDIT_ARTICLE_FONT, hwnd);
    st->hUdArtFont = CreateChild(st, PageArticle, UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS, 0,
        0, 0, 0, 0, IDC_UD_ARTICLE_FONT, hwnd);
    SendMessageW(st->hUdArtFont, UDM_SETRANGE32, 8, 96);
    SendMessageW(st->hUdArtFont, UDM_SETPOS32, 0, st->cfg.articleFontSize);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5c3a\u5bf8\u6a21\u5f0f", rc.left, y, PageArticle);
    st->hComboArtSize = CreateChild(st, PageArticle, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX, y, st->S(240), st->S(160), IDC_COMBO_ARTICLE_SIZE_MODE, hwnd);
    ComboBox_AddString(st->hComboArtSize, L"\u81ea\u9002\u5e94\uff08\u56fa\u5b9a\u5bbd\uff09");
    ComboBox_AddString(st->hComboArtSize, L"\u81ea\u9002\u5e94\u8c03\u6574\uff08\u5168\u81ea\u9002\u5e94\uff09");
    ComboBox_AddString(st->hComboArtSize, L"\u81ea\u5b9a\u4e49");
    ComboBox_SetCurSel(st->hComboArtSize,
                       static_cast<int>(st->cfg.articleSizeMode));
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5c3a\u5bf8(\u50cf\u7d20)", rc.left, y, PageArticle);
    st->hEditArtWidth = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(64), st->S(kRowH), IDC_EDIT_ARTICLE_WIDTH, hwnd);
    CreateChild(st, PageArticle, L"STATIC", L"\u5bbd", WS_CHILD | WS_VISIBLE,
        0, ctrlX + st->S(70), y, st->S(20), st->S(kRowH), 0, hwnd);
    st->hEditArtHeight = CreateChild(st, PageArticle, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER, WS_EX_CLIENTEDGE,
        ctrlX + st->S(94), y, st->S(64), st->S(kRowH), IDC_EDIT_ARTICLE_HEIGHT, hwnd);
    CreateChild(st, PageArticle, L"STATIC", L"\u9ad8", WS_CHILD | WS_VISIBLE,
        0, ctrlX + st->S(164), y, st->S(20), st->S(kRowH), 0, hwnd);
    wchar_t szbuf[32];
    swprintf_s(szbuf, L"%d", st->cfg.articleWidth);
    SetWindowTextW(st->hEditArtWidth, szbuf);
    swprintf_s(szbuf, L"%d", st->cfg.articleHeight);
    SetWindowTextW(st->hEditArtHeight, szbuf);
    UpdateArticleSizeControls(st);

    // ======================= 页 5：窗口与启动 =======================
    y = rc.top + st->S(4);
    CreateLabel(st, hwnd, L"\u7a97\u53e3\u4f4d\u7f6e", rc.left, y, PageWindow);
    st->hComboAnchor = CreateChild(st, PageWindow, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL, 0,
        ctrlX, y, st->S(180), st->S(220), IDC_COMBO_ANCHOR, hwnd);
    // 顺序与 WindowAnchor 枚举一致
    static const wchar_t* kAnchors[] = {
        L"\u5de6\u4e0a\u89d2",   L"\u9876\u90e8\u5c45\u4e2d", L"\u53f3\u4e0a\u89d2",
        L"\u5de6\u5c45\u4e2d",   L"\u5c4f\u5e55\u5c45\u4e2d", L"\u53f3\u5c45\u4e2d",
        L"\u5de6\u4e0b\u89d2",   L"\u5e95\u90e8\u5c45\u4e2d", L"\u53f3\u4e0b\u89d2",
    };
    for (auto* s : kAnchors) ComboBox_AddString(st->hComboAnchor, s);
    ComboBox_SetCurSel(st->hComboAnchor, static_cast<int>(st->cfg.anchor));
    y += rowStep;

    CreateLabel(st, hwnd, L"\u504f\u79fb(\u50cf\u7d20)", rc.left, y, PageWindow);
    st->hEditPlaceX = CreateChild(st, PageWindow, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        ctrlX, y, st->S(64), st->S(kRowH), IDC_EDIT_PLACE_X, hwnd);
    CreateChild(st, PageWindow, L"STATIC", L"X", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, ctrlX + st->S(70), y, st->S(18), st->S(kRowH), 0, hwnd);
    st->hEditPlaceY = CreateChild(st, PageWindow, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, WS_EX_CLIENTEDGE,
        ctrlX + st->S(94), y, st->S(64), st->S(kRowH), IDC_EDIT_PLACE_Y, hwnd);
    CreateChild(st, PageWindow, L"STATIC", L"Y", WS_CHILD | WS_VISIBLE | SS_CENTER,
        0, ctrlX + st->S(164), y, st->S(18), st->S(kRowH), 0, hwnd);
    wchar_t nb[32];
    swprintf_s(nb, L"%d", st->cfg.placeX);
    SetWindowTextW(st->hEditPlaceX, nb);
    swprintf_s(nb, L"%d", st->cfg.placeY);
    SetWindowTextW(st->hEditPlaceY, nb);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u7a97\u53e3\u884c\u4e3a", rc.left, y, PageWindow);
    st->hCheckTop = CreateChild(st, PageWindow, L"BUTTON", L"\u59cb\u7ec8\u7f6e\u9876",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
        ctrlX, y, st->S(140), st->S(kRowH), IDC_CHECK_TOPMOST, hwnd);
    Button_SetCheck(st->hCheckTop, st->cfg.isTop ? BST_CHECKED : BST_UNCHECKED);
    y += rowStep;

    CreateLabel(st, hwnd, L"\u5f00\u673a\u542f\u52a8", rc.left, y, PageWindow);
    st->hCheckAuto = CreateChild(st, PageWindow, L"BUTTON", L"\u5f00\u673a\u81ea\u52a8\u542f\u52a8",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX, 0,
        ctrlX, y, ctrlW, st->S(kRowH), IDC_CHECK_AUTOSTART, hwnd);
    Button_SetCheck(st->hCheckAuto, st->cfg.autoStart ? BST_CHECKED : BST_UNCHECKED);

    // ======================= 底部按钮与制作者信息 =======================
    int btnW = st->S(92), btnH = st->S(28);
    int btnY = st->S(kWinH - kPadding - 28 - 24);
    int bx = st->S(kWinW - kPadding) - btnW;
    CreateWindowW(L"BUTTON", L"\u53d6\u6d88", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                  bx, btnY, btnW, btnH, hwnd, (HMENU)IDC_BTN_CANCEL, hInst, nullptr);
    bx -= (btnW + st->S(8));
    CreateWindowW(L"BUTTON", L"\u4fdd\u5b58", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
                  bx, btnY, btnW, btnH, hwnd, (HMENU)IDC_BTN_SAVE, hInst, nullptr);

    CreateWindowW(L"STATIC",
        L"\u5236\u4f5c\uff1aBu Xin    \u90ae\u7bb1\uff1abuxin0912@qq.com",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        st->S(kPadding), st->S(kWinH - kPadding - 16),
        st->S(kWinW - 2 * kPadding), st->S(16),
        hwnd, (HMENU)IDC_STATIC_CREDIT, hInst, nullptr);

    // 统一字体
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, lp, TRUE);
        return TRUE;
    }, (LPARAM)st->font);

    ShowPage(st, 0);
    SetFocus(st->hEditTitle);
    Edit_SetSel(st->hEditTitle, 0, 0);
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
        TabCtrl_SetCurSel(st->hTab, PageBasic);
        ShowPage(st, PageBasic);
        SetFocus(st->hEditTitle);
        return false;
    }

    // 目标时间
    SYSTEMTIME sd{}, stTime{};
    if (DateTime_GetSystemtime(st->hDtpDate, &sd) != GDT_VALID
        || DateTime_GetSystemtime(st->hDtpTime, &stTime) != GDT_VALID) {
        MessageBoxW(hwnd, L"\u65f6\u95f4\u683c\u5f0f\u65e0\u6548",
                    L"\u63d0\u793a", MB_ICONWARNING);
        return false;
    }
    sd.wHour = stTime.wHour;
    sd.wMinute = stTime.wMinute;
    sd.wSecond = stTime.wSecond;
    SystemToTm(sd, st->cfg.targetTime);

    // 循环
    bool cycleOn = (Button_GetCheck(st->hCheckCycle) == BST_CHECKED);
    if (cycleOn) {
        int cycSel = ComboBox_GetCurSel(st->hComboCycle);
        if (cycSel < 0 || cycSel > 5) {
            MessageBoxW(hwnd, L"\u8bf7\u9009\u62e9\u5faa\u73af\u5468\u671f",
                        L"\u63d0\u793a", MB_ICONWARNING);
            TabCtrl_SetCurSel(st->hTab, PageBasic);
            ShowPage(st, PageBasic);
            SetFocus(st->hComboCycle);
            return false;
        }
        st->cfg.cycle = static_cast<CycleUnit>(cycSel + 1);
    } else {
        st->cfg.cycle = CycleUnit::None;
    }

    // 起始日
    st->cfg.includeStartDay =
        (Button_GetCheck(st->hCheckStart) == BST_CHECKED);

    // 显示格式
    int fmtSel = ComboBox_GetCurSel(st->hComboFmt);
    st->cfg.displayFormat = (fmtSel == 1) ? utils::DisplayFormat::Full
                          : (fmtSel == 2) ? utils::DisplayFormat::Days
                                          : utils::DisplayFormat::HMS;

    // 字号
    // UDM_GETPOS32 的 lParam 指向 BOOL 错误标志：0=成功，非0=错误
    BOOL error = FALSE;
    int size = static_cast<int>(SendMessageW(st->hUdSize, UDM_GETPOS32, 0,
                          reinterpret_cast<LPARAM>(&error)));
    if (error || size < 8 || size > 200) {
        MessageBoxW(hwnd, L"\u5b57\u53f7\u5e94\u5728 8\u2013200 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageAppearance);
        ShowPage(st, PageAppearance);
        SetFocus(st->hEditSize);
        return false;
    }
    st->cfg.fontSize = size;

    // 字体：从组合框列表获取选中项文本。
    // 注意不能用 GetWindowTextW —— CBS_DROPDOWN 的编辑框部分经 WM_SETFONT
    // 重建后文本不可靠；且 CB_GETLBTEXT 不写 null 终止符，必须用返回长度构造。
    int fontSel = ComboBox_GetCurSel(st->hComboFont);
    int n = 0;
    if (fontSel != CB_ERR) {
        n = ComboBox_GetLBTextLen(st->hComboFont, fontSel);
        if (n > 0 && n < 256) {
            ComboBox_GetLBText(st->hComboFont, fontSel, buf);
            st->cfg.font.assign(buf, n);
        }
    }
    if (st->cfg.font.empty()) st->cfg.font = utils::GetSystemDefaultFontName();

    // 一言来源
    int hkSel = ComboBox_GetCurSel(st->hComboHkSrc);
    st->cfg.hitokotoSource = (hkSel == 0) ? HitokotoSource::Local
                          : (hkSel == 1) ? HitokotoSource::Network
                                         : HitokotoSource::Both;

    // 一言刷新时间（秒）
    BOOL err2 = FALSE;
    int sec = static_cast<int>(SendMessageW(st->hUdHkRefresh, UDM_GETPOS32, 0,
                            reinterpret_cast<LPARAM>(&err2)));
    if (err2 || sec < 5 || sec > 3600) {
        MessageBoxW(hwnd, L"\u5237\u65b0\u65f6\u95f4\u5e94\u5728 5\u20133600 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageHitokoto);
        ShowPage(st, PageHitokoto);
        SetFocus(st->hEditHkRefresh);
        return false;
    }
    st->cfg.hitokotoRefreshSec = sec;

    // 一文栏目
    st->cfg.articleEnabled = (Button_GetCheck(st->hCheckArticle) == BST_CHECKED);

    int artAnchorSel = ComboBox_GetCurSel(st->hComboArtAnchor);
    if (artAnchorSel < 0 || artAnchorSel > 8) {
        MessageBoxW(hwnd, L"\u8bf7\u9009\u62e9\u4e00\u6587\u7a97\u53e3\u4f4d\u7f6e",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hComboArtAnchor);
        return false;
    }
    st->cfg.articleAnchor = static_cast<WindowAnchor>(artAnchorSel);

    GetWindowTextW(st->hEditArtX, buf, 256);
    int artX = _wtoi(buf);
    GetWindowTextW(st->hEditArtY, buf, 256);
    int artY = _wtoi(buf);
    if (artX < -10000 || artX > 10000 || artY < -10000 || artY > 10000) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtX);
        return false;
    }
    st->cfg.articlePlaceX = artX;
    st->cfg.articlePlaceY = artY;

    BOOL errH = FALSE, errM = FALSE, errS = FALSE;
    int artH = static_cast<int>(SendMessageW(st->hUdArtH, UDM_GETPOS32, 0,
                          reinterpret_cast<LPARAM>(&errH)));
    int artM = static_cast<int>(SendMessageW(st->hUdArtM, UDM_GETPOS32, 0,
                          reinterpret_cast<LPARAM>(&errM)));
    int artS = static_cast<int>(SendMessageW(st->hUdArtS, UDM_GETPOS32, 0,
                          reinterpret_cast<LPARAM>(&errS)));
    int artTotal = artH * 3600 + artM * 60 + artS;
    if (errH || errM || errS || artH < 0 || artH > 23
        || artM < 0 || artM > 59 || artS < 0 || artS > 59
        || artTotal < 1 || artTotal > 86399) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5237\u65b0\u65f6\u95f4\u5e94\u5728 00:00:01\u201323:59:59 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtH);
        return false;
    }
    st->cfg.articleRefreshSec = artTotal;

    // 一文置顶（独立于倒计时）
    st->cfg.articleIsTop = (Button_GetCheck(st->hCheckArtTop) == BST_CHECKED);

    // 一文字号
    BOOL errFont = FALSE;
    int artFont = static_cast<int>(SendMessageW(st->hUdArtFont, UDM_GETPOS32, 0,
                          reinterpret_cast<LPARAM>(&errFont)));
    if (errFont || artFont < 8 || artFont > 96) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5b57\u53f7\u5e94\u5728 8\u201396 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtFont);
        return false;
    }
    st->cfg.articleFontSize = artFont;

    // 一文尺寸模式
    int sizeMode = ComboBox_GetCurSel(st->hComboArtSize);
    if (sizeMode < 0 || sizeMode > 2) {
        MessageBoxW(hwnd, L"\u8bf7\u9009\u62e9\u4e00\u6587\u5c3a\u5bf8\u6a21\u5f0f",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hComboArtSize);
        return false;
    }
    st->cfg.articleSizeMode = static_cast<ArticleSizeMode>(sizeMode);

    GetWindowTextW(st->hEditArtWidth, buf, 256);
    int artW = _wtoi(buf);
    GetWindowTextW(st->hEditArtHeight, buf, 256);
    int artHgt = _wtoi(buf);
    if (artW < 180 || artW > 1200 || artHgt < 120 || artHgt > 2000) {
        MessageBoxW(hwnd, L"\u4e00\u6587\u5c3a\u5bf8\u5e94\u4e3a\uff1a\u5bbd 180\u20131200\u3001\u9ad8 120\u20132000",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageArticle);
        ShowPage(st, PageArticle);
        SetFocus(st->hEditArtWidth);
        return false;
    }
    st->cfg.articleWidth = artW;
    st->cfg.articleHeight = artHgt;

    // 始终置顶
    st->cfg.isTop = (Button_GetCheck(st->hCheckTop) == BST_CHECKED);

    // 窗口位置锚点
    int anchorSel = ComboBox_GetCurSel(st->hComboAnchor);
    if (anchorSel < 0 || anchorSel > 8) {
        MessageBoxW(hwnd, L"\u8bf7\u9009\u62e9\u7a97\u53e3\u4f4d\u7f6e",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageWindow);
        ShowPage(st, PageWindow);
        SetFocus(st->hComboAnchor);
        return false;
    }
    st->cfg.anchor = static_cast<WindowAnchor>(anchorSel);

    // 偏移（可为负，空输入按 0 处理）
    GetWindowTextW(st->hEditPlaceX, buf, 256);
    int px = _wtoi(buf);
    GetWindowTextW(st->hEditPlaceY, buf, 256);
    int py = _wtoi(buf);
    if (px < -10000 || px > 10000 || py < -10000 || py > 10000) {
        MessageBoxW(hwnd, L"\u504f\u79fb\u8303\u56f4\u5e94\u5728 -10000\u201310000 \u4e4b\u95f4",
                    L"\u63d0\u793a", MB_ICONWARNING);
        TabCtrl_SetCurSel(st->hTab, PageWindow);
        ShowPage(st, PageWindow);
        SetFocus(st->hEditPlaceX);
        return false;
    }
    st->cfg.placeX = px;
    st->cfg.placeY = py;

    // 开机自启动
    st->cfg.autoStart = (Button_GetCheck(st->hCheckAuto) == BST_CHECKED);

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
// 自有窗口过程：通过 GWLP_USERDATA 持有 SettingsState*
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
        case WM_NOTIFY: {
            auto* nm = reinterpret_cast<NMHDR*>(lp);
            if (nm && nm->idFrom == IDC_TAB_MAIN
                && nm->code == TCN_SELCHANGE && st && st->hTab) {
                ShowPage(st, TabCtrl_GetCurSel(st->hTab));
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (st && st->hTab && dis && dis->hwndItem == st->hTab) {
                wchar_t text[128] = {0};
                TCITEMW item{};
                item.mask = TCIF_TEXT;
                item.pszText = text;
                item.cchTextMax = 128;
                TabCtrl_GetItem(st->hTab, static_cast<int>(dis->itemID), &item);

                bool selected = (dis->itemState & ODS_SELECTED) != 0;
                HBRUSH bg = CreateSolidBrush(selected ? RGB(0, 95, 184)
                                                      : GetSysColor(COLOR_BTNFACE));
                FillRect(dis->hDC, &dis->rcItem, bg);
                DeleteObject(bg);

                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC,
                             selected ? RGB(255, 255, 255) : RGB(40, 40, 40));
                HFONT useFont = (selected && st->boldFont) ? st->boldFont : st->font;
                HFONT oldFont = useFont
                    ? (HFONT)SelectObject(dis->hDC, useFont) : nullptr;
                RECT rc = dis->rcItem;
                DrawTextW(dis->hDC, text, -1, &rc,
                          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                if (oldFont) SelectObject(dis->hDC, oldFont);
                return TRUE;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            // 明确设置深色文字 + 对话框底色，提升标签对比度
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, RGB(24, 24, 24));
            SetBkColor(dc, GetSysColor(COLOR_BTNFACE));
            return reinterpret_cast<LRESULT>(GetSysColorBrush(COLOR_BTNFACE));
        }
        case WM_COMMAND: {
            WORD id = LOWORD(wp);
            switch (id) {
                case IDC_BTN_SAVE:
                case IDOK:
                    if (CollectAndValidate(hwnd, st)) {
                        DestroyWindow(hwnd);
                    }
                    return 0;
                case IDC_BTN_CANCEL:
                case IDCANCEL:
                    DestroyWindow(hwnd);
                    return 0;
                case IDC_CHECK_CYCLE:
                    if (HIWORD(wp) == BN_CLICKED && st && st->hComboCycle) {
                        bool on = (Button_GetCheck(st->hCheckCycle) == BST_CHECKED);
                        EnableWindow(st->hComboCycle, on ? TRUE : FALSE);
                    }
                    return 0;
                case IDC_COMBO_ARTICLE_SIZE_MODE:
                    if (HIWORD(wp) == CBN_SELCHANGE && st) {
                        UpdateArticleSizeControls(st);
                    }
                    return 0;
            }
            return 0;
        }
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            if (st) {
                st->dlg = nullptr;
                st->running = false;
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---------------------------------------------------------------------------
bool OpenSettingsDialog(HWND parent, Config& cfg) {
    // 防重入：已有设置窗口时忽略本次调用（否则托盘消息在模态循环中被
    // 派发到主窗口，会嵌套打开第二个设置窗口，破坏模态消息循环）
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

    int winW = st.S(kWinW);
    int winH = st.S(kWinH);

    // 居中到父窗口（如父窗口尺寸过小，则退回屏幕居中）
    RECT prc{};
    GetWindowRect(parent, &prc);
    int x = prc.left + ((prc.right - prc.left) - winW) / 2;
    int y = prc.top + ((prc.bottom - prc.top) - winH) / 2;
    if (prc.right - prc.left < winW || prc.bottom - prc.top < winH) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - winW) / 2;
        y = work.top + ((work.bottom - work.top) - winH) / 2;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    // UI 字体按 DPI 缩放（DEFAULT_GUI_FONT 在 PerMonitor 下不会跟随窗口 DPI）
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

    // 通过 lpParam 把 SettingsState* 传给窗口过程（WM_NCCREATE 时取出）
    st.dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kClass,
        L"\u8bbe\u7f6e",
        WS_POPUPWINDOW | WS_CAPTION,
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

    EnableWindow(parent, FALSE);
    ShowWindow(st.dlg, SW_SHOW);
    UpdateWindow(st.dlg);

    // 模态消息循环
    MSG msg;
    while (st.running && GetMessage(&msg, nullptr, 0, 0) > 0) {
        if (!st.dlg || !IsDialogMessageW(st.dlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    EnableWindow(parent, TRUE);
    SetForegroundWindow(parent);
    if (st.font) DeleteObject(st.font);
    if (st.boldFont) DeleteObject(st.boldFont);

    if (st.saved) {
        cfg = st.cfg;
        return true;
    }
    return false;
}
