// =============================================================================
// settings_window.cpp - 设置窗口实现
// =============================================================================
#include "settings_window.h"
#include "../resource.h"
#include "utils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// 布局常量
static const int kPadding    = 18;
static const int kLabelW     = 100;
static const int kCtrlX      = kPadding + kLabelW + 6;
static const int kRowH       = 26;
static const int kRowGap     = 10;
static const int kWinW       = 460;
static const int kWinH       = 500;

struct SettingsState {
    Config cfg;
    HWND   parent = nullptr;
    HWND   dlg    = nullptr;
    bool   saved  = false;
    bool   running = true;

    // 控件句柄
    HWND hEditTitle   = nullptr;
    HWND hDtpDate     = nullptr;
    HWND hDtpTime     = nullptr;
    HWND hComboFmt    = nullptr;
    HWND hEditSize    = nullptr;
    HWND hUdSize      = nullptr;
    HWND hComboFont   = nullptr;
    HWND hComboHkSrc  = nullptr;
    HWND hEditHkRefresh = nullptr;
    HWND hUdHkRefresh = nullptr;
    HWND hCheckTop    = nullptr; // 始终置顶
    HWND hComboAnchor = nullptr; // 窗口位置锚点
    HWND hEditPlaceX  = nullptr; // 水平偏移
    HWND hEditPlaceY  = nullptr; // 垂直偏移
};

// ---------------------------------------------------------------------------
static const wchar_t* kClass = L"CountdownSettingsClass";

// 前置声明
static LRESULT CALLBACK SettingsWndProc(HWND, UINT, WPARAM, LPARAM);
static void OnCreate(HWND hwnd, SettingsState* st);
static bool CollectAndValidate(HWND hwnd, SettingsState* st);

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
static HWND CreateLabel(HWND parent, const wchar_t* text, int x, int y) {
    return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_RIGHT,
                         x, y, kLabelW, kRowH, parent, nullptr,
                         (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
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
// WM_CREATE 处理：创建所有子控件并填入当前值
// ---------------------------------------------------------------------------
static void OnCreate(HWND hwnd, SettingsState* st) {
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
    int y = kPadding;

    // 行 1：标题
    CreateLabel(hwnd, L"\u5012\u8ba1\u65f6\u6807\u9898", kPadding, y);
    st->hEditTitle = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT",
        st->cfg.title.c_str(),
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        kCtrlX, y, kWinW - kCtrlX - kPadding, kRowH,
        hwnd, (HMENU)IDC_EDIT_TITLE, hInst, nullptr);
    y += kRowH + kRowGap;

    // 行 2：目标时间（日期 DTP + 时间 DTP）
    CreateLabel(hwnd, L"\u76ee\u6807\u65f6\u95f4", kPadding, y);
    int dateW = 170, timeW = 110;
    st->hDtpDate = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | DTS_SHORTDATEFORMAT,
        kCtrlX, y, dateW, kRowH, hwnd, (HMENU)IDC_DTP_DATE, hInst, nullptr);
    st->hDtpTime = CreateWindowExW(0, DATETIMEPICK_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | DTS_TIMEFORMAT | DTS_UPDOWN,
        kCtrlX + dateW + 8, y, timeW, kRowH, hwnd, (HMENU)IDC_DTP_TIME, hInst, nullptr);

    SYSTEMTIME stDate{}, stTime{};
    TmToSystem(st->cfg.targetTime, stDate);
    TmToSystem(st->cfg.targetTime, stTime);
    DateTime_SetSystemtime(st->hDtpDate, GDT_VALID, &stDate);
    DateTime_SetSystemtime(st->hDtpTime, GDT_VALID, &stTime);
    y += kRowH + kRowGap;

    // 行 3：显示格式
    CreateLabel(hwnd, L"\u663e\u793a\u683c\u5f0f", kPadding, y);
    st->hComboFmt = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        kCtrlX, y, 200, 200, hwnd, (HMENU)IDC_COMBO_FORMAT, hInst, nullptr);
    ComboBox_AddString(st->hComboFmt, L"HH:MM:SS");
    ComboBox_AddString(st->hComboFmt, L"YYYY-MM-DD HH:MM:SS");
    ComboBox_AddString(st->hComboFmt, L"DDDD HH:MM:SS");
    ComboBox_SetCurSel(st->hComboFmt,
        st->cfg.displayFormat == utils::DisplayFormat::Full ? 1 :
        st->cfg.displayFormat == utils::DisplayFormat::Days ? 2 : 0);
    y += kRowH + kRowGap;

    // 行 4：字号
    CreateLabel(hwnd, L"\u5b57\u53f7(\u50cf\u7d20)", kPadding, y);
    st->hEditSize = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
        kCtrlX, y, 80, kRowH, hwnd, (HMENU)IDC_EDIT_FONTSIZE, hInst, nullptr);
    st->hUdSize = CreateWindowW(UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS,
        0, 0, 0, 0, hwnd, (HMENU)IDC_UD_FONTSIZE, hInst, nullptr);
    SendMessageW(st->hUdSize, UDM_SETRANGE32, 8, 200);
    SendMessageW(st->hUdSize, UDM_SETPOS32, 0, st->cfg.fontSize);
    y += kRowH + kRowGap;

    // 行 5：字体
    CreateLabel(hwnd, L"\u5b57\u4f53", kPadding, y);
    st->hComboFont = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL | WS_VSCROLL,
        kCtrlX, y, kWinW - kCtrlX - kPadding, 240,
        hwnd, (HMENU)IDC_COMBO_FONT, hInst, nullptr);

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
    y += kRowH + kRowGap + 6;

    // 行 6：一言来源（本地/网络/本地和网络）
    CreateLabel(hwnd, L"\u4e00\u8a00\u6765\u6e90", kPadding, y);
    st->hComboHkSrc = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        kCtrlX, y, 200, 120, hwnd, (HMENU)IDC_COMBO_HITOKOTO_SRC, hInst, nullptr);
    ComboBox_AddString(st->hComboHkSrc, L"\u672c\u5730");
    ComboBox_AddString(st->hComboHkSrc, L"\u7f51\u7edc");
    ComboBox_AddString(st->hComboHkSrc, L"\u672c\u5730\u548c\u7f51\u7edc");
    ComboBox_SetCurSel(st->hComboHkSrc, static_cast<int>(st->cfg.hitokotoSource));
    y += kRowH + kRowGap;

    // 行 7：一言刷新时间（秒）
    CreateLabel(hwnd, L"\u5237\u65b0\u65f6\u95f4(\u79d2)", kPadding, y);
    st->hEditHkRefresh = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_NUMBER,
        kCtrlX, y, 80, kRowH, hwnd, (HMENU)IDC_EDIT_HITOKOTO_REFRESH, hInst, nullptr);
    st->hUdHkRefresh = CreateWindowW(UPDOWN_CLASSW, L"",
        WS_CHILD | WS_VISIBLE | UDS_SETBUDDYINT | UDS_ALIGNRIGHT
            | UDS_AUTOBUDDY | UDS_ARROWKEYS,
        0, 0, 0, 0, hwnd, (HMENU)IDC_UD_HITOKOTO_REFRESH, hInst, nullptr);
    SendMessageW(st->hUdHkRefresh, UDM_SETRANGE32, 5, 3600);
    SendMessageW(st->hUdHkRefresh, UDM_SETPOS32, 0, st->cfg.hitokotoRefreshSec);
    y += kRowH + kRowGap;

    // 行 8：始终置顶（复选框）
    st->hCheckTop = CreateWindowW(L"BUTTON", L"\u59cb\u7ec8\u7f6e\u9876",
        WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
        kCtrlX, y, 160, kRowH, hwnd, (HMENU)IDC_CHECK_TOPMOST, hInst, nullptr);
    Button_SetCheck(st->hCheckTop, st->cfg.isTop ? BST_CHECKED : BST_UNCHECKED);
    y += kRowH + kRowGap;

    // 行 9：窗口位置（9 宫格锚点模板）
    CreateLabel(hwnd, L"\u7a97\u53e3\u4f4d\u7f6e", kPadding, y);
    st->hComboAnchor = CreateWindowW(L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
        kCtrlX, y, 180, 200, hwnd, (HMENU)IDC_COMBO_ANCHOR, hInst, nullptr);
    // 顺序与 WindowAnchor 枚举一致
    static const wchar_t* kAnchors[] = {
        L"\u5de6\u4e0a\u89d2",   L"\u9876\u90e8\u5c45\u4e2d", L"\u53f3\u4e0a\u89d2",
        L"\u5de6\u5c45\u4e2d",   L"\u5c4f\u5e55\u5c45\u4e2d", L"\u53f3\u5c45\u4e2d",
        L"\u5de6\u4e0b\u89d2",   L"\u5e95\u90e8\u5c45\u4e2d", L"\u53f3\u4e0b\u89d2",
    };
    for (auto* s : kAnchors) ComboBox_AddString(st->hComboAnchor, s);
    ComboBox_SetCurSel(st->hComboAnchor, static_cast<int>(st->cfg.anchor));
    y += kRowH + kRowGap;

    // 行 10：偏移（相对锚点位置的像素偏移，可为负）
    CreateLabel(hwnd, L"\u504f\u79fb(\u50cf\u7d20)", kPadding, y);
    st->hEditPlaceX = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        kCtrlX, y, 64, kRowH, hwnd, (HMENU)IDC_EDIT_PLACE_X, hInst, nullptr);
    CreateWindowW(L"STATIC", L"X", WS_CHILD | WS_VISIBLE | SS_CENTER,
        kCtrlX + 70, y, 18, kRowH, hwnd, nullptr, hInst, nullptr);
    st->hEditPlaceY = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
        kCtrlX + 94, y, 64, kRowH, hwnd, (HMENU)IDC_EDIT_PLACE_Y, hInst, nullptr);
    CreateWindowW(L"STATIC", L"Y", WS_CHILD | WS_VISIBLE | SS_CENTER,
        kCtrlX + 164, y, 18, kRowH, hwnd, nullptr, hInst, nullptr);
    wchar_t nb[32];
    swprintf_s(nb, L"%d", st->cfg.placeX);
    SetWindowTextW(st->hEditPlaceX, nb);
    swprintf_s(nb, L"%d", st->cfg.placeY);
    SetWindowTextW(st->hEditPlaceY, nb);
    y += kRowH + kRowGap;

    // 按钮
    int btnW = 92, btnH = 28;
    int bx = kWinW - kPadding - btnW;
    CreateWindowW(L"BUTTON", L"\u53d6\u6d88",
        WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        bx, y, btnW, btnH, hwnd, (HMENU)IDC_BTN_CANCEL, hInst, nullptr);
    bx -= (btnW + 8);
    CreateWindowW(L"BUTTON", L"\u4fdd\u5b58",
        WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
        bx, y, btnW, btnH, hwnd, (HMENU)IDC_BTN_SAVE, hInst, nullptr);

    // 底部制作者信息（居中显示：作者 + 邮箱）
    CreateWindowW(L"STATIC", L"\u5236\u4f5c\uff1aBu Xin",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        kPadding, kWinH - kPadding - 60, kWinW - kPadding * 2, 18,
        hwnd, (HMENU)IDC_STATIC_CREDIT, hInst, nullptr);
    CreateWindowW(L"STATIC", L"\u90ae\u7bb1\uff1abuxin0912@qq.com",
        WS_CHILD | WS_VISIBLE | SS_CENTER,
        kPadding, kWinH - kPadding - 40, kWinW - kPadding * 2, 18,
        hwnd, (HMENU)IDC_STATIC_CREDIT_MAIL, hInst, nullptr);

    // 统一字体
    HFONT hUi = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
    EnumChildWindows(hwnd, [](HWND child, LPARAM lp) -> BOOL {
        SendMessageW(child, WM_SETFONT, lp, TRUE);
        return TRUE;
    }, (LPARAM)hUi);

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
        SetFocus(st->hEditHkRefresh);
        return false;
    }
    st->cfg.hitokotoRefreshSec = sec;

    // 始终置顶
    st->cfg.isTop = (Button_GetCheck(st->hCheckTop) == BST_CHECKED);

    // 窗口位置锚点
    int anchorSel = ComboBox_GetCurSel(st->hComboAnchor);
    if (anchorSel < 0 || anchorSel > 8) {
        MessageBoxW(hwnd, L"\u8bf7\u9009\u62e9\u7a97\u53e3\u4f4d\u7f6e",
                    L"\u63d0\u793a", MB_ICONWARNING);
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

    // 居中到父窗口（如父窗口尺寸过小，则退回屏幕居中）
    RECT prc{};
    GetWindowRect(parent, &prc);
    int x = prc.left + ((prc.right - prc.left) - kWinW) / 2;
    int y = prc.top + ((prc.bottom - prc.top) - kWinH) / 2;
    if (prc.right - prc.left < kWinW || prc.bottom - prc.top < kWinH) {
        RECT work{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
        x = work.left + ((work.right - work.left) - kWinW) / 2;
        y = work.top + ((work.bottom - work.top) - kWinH) / 2;
    }
    if (x < 0) x = 0;
    if (y < 0) y = 0;

    // 通过 lpParam 把 SettingsState* 传给窗口过程（WM_NCCREATE 时取出）
    st.dlg = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kClass,
        L"\u8bbe\u7f6e",
        WS_POPUPWINDOW | WS_CAPTION,
        x, y, kWinW, kWinH,
        parent, nullptr, hInst, &st);

    if (!st.dlg) {
        DWORD err = GetLastError();
        wchar_t buf[128];
        swprintf_s(buf, 128, L"CreateWindowEx failed, GetLastError=%lu", err);
        MessageBoxW(parent, buf, L"\u9519\u8bef", MB_ICONERROR);
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

    if (st.saved) {
        cfg = st.cfg;
        return true;
    }
    return false;
}
