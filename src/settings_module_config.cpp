// =============================================================================
// settings_module_config.cpp - 由 DLL schema 驱动的通用配置对话框
// =============================================================================
#include "settings_module_config.h"
#include "../resource.h"
#include "json.h"
#include "utils.h"

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <uxtheme.h>
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

// 配色与主设置窗口保持一致
const COLORREF kBg         = RGB(24, 24, 30);
const COLORREF kDivider    = RGB(44, 44, 54);
const COLORREF kLabel      = RGB(212, 212, 221);
const COLORREF kMuted      = RGB(138, 138, 150);
const COLORREF kChipBg     = RGB(36, 36, 45);
const COLORREF kChipBorder = RGB(56, 56, 69);
const COLORREF kValueText  = RGB(233, 233, 240);
const COLORREF kAccent     = RGB(62, 94, 240);
const COLORREF kAccentHi   = RGB(92, 124, 255);
const COLORREF kOnBg       = RGB(38, 48, 88);
const COLORREF kOnBorder   = RGB(80, 110, 255);

const int kPad     = 20;
const int kWinW    = 500;
const int kRowH    = 32;
const int kBtnH    = 30;
const int kRangeRowH = 36;

const int kItemBoolBase   = 3280;
const int kItemIntBase    = 3280 + 20;
const int kItemChoiceBase = 3280 + 40;

enum class ItemType { Bool, Int, Choice, DayRangeList };

struct ModuleItem {
    std::string key;
    ItemType type = ItemType::Bool;
    std::wstring label;
    std::wstring hint;

    bool defBool = false;
    int defInt = 0;
    int min = 0;
    int max = 100;
    int step = 1;
    std::string defChoice;
    std::vector<std::pair<std::string, std::wstring>> options; // value, label
    std::vector<std::pair<int, int>> defRanges;
    int maxRows = 7;

    bool valueBool = false;
    int valueInt = 0;
    std::string valueChoice;
    std::vector<std::pair<int, int>> ranges;

    HWND boolBtn = nullptr;
    HWND intEdit = nullptr;
    HWND choiceCombo = nullptr;
    HWND addBtn = nullptr;
    int  rowsTop = 0;
    std::vector<HWND> rowStart;
    std::vector<HWND> rowEnd;
    std::vector<HWND> rowDelete;
};

struct McState {
    HWND dlg = nullptr;
    HWND parent = nullptr;
    bool saved = false;
    bool running = true;
    UINT dpi = 96;

    HFONT font = nullptr;
    HFONT bold = nullptr;
    HBRUSH brBg = nullptr;
    HBRUSH brChip = nullptr;
    std::string lastJson;

    std::wstring moduleId;
    std::wstring moduleName;
    std::wstring moduleDescription;
    std::vector<ModuleItem> items;
    std::vector<HWND> mutedLabels;

    HWND okBtn = nullptr;
    HWND cancelBtn = nullptr;
    bool initialFocusDone = false;
    int  footerY = 0;

    // 星期选择浮层：独立顶层弹出窗口，避免被对话框内的兄弟控件遮挡
    HWND hDayPopup = nullptr;
    HWND popupTrigger = nullptr;
    ModuleItem* popupItem = nullptr;
    int popupRow = -1;
    bool popupIsEnd = false;
    int  dayPopupSel = 1;
    int  dayPopupHover = -1;
    bool dayPopupTracking = false;

    int S(int value) const {
        return utils::ScaleDpi(value, static_cast<int>(dpi));
    }
};

const wchar_t* kClass = L"CountdownModuleConfigClass";
const wchar_t* kDayPopupClass = L"CountdownDayPopupClass";
const wchar_t* kDayNames[7] = {
    L"\u5468\u4e00", L"\u5468\u4e8c", L"\u5468\u4e09",
    L"\u5468\u56db", L"\u5468\u4e94", L"\u5468\u516d", L"\u5468\u65e5",
};

// ---------------------------------------------------------------------------
// 基础辅助
// ---------------------------------------------------------------------------
HINSTANCE InstOf(HWND hwnd) {
    return (HINSTANCE)GetWindowLongPtrW(hwnd, GWLP_HINSTANCE);
}

bool ParseIntText(const wchar_t* text, int& out) {
    const wchar_t* p = text;
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p == L'\0') { out = 0; return true; }
    errno = 0;
    wchar_t* end = nullptr;
    long v = wcstol(p, &end, 10);
    if (end == p || errno == ERANGE || v < INT_MIN || v > INT_MAX) return false;
    while (*end == L' ' || *end == '\t') ++end;
    if (*end != L'\0') return false;
    out = static_cast<int>(v);
    return true;
}

void FillRound(HDC dc, RECT rc, int radius, COLORREF fill,
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

void DrawCentered(HDC dc, RECT rc, const wchar_t* text,
                  HFONT font, COLORREF color) {
    HFONT old = font ? (HFONT)SelectObject(dc, font) : nullptr;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (old) SelectObject(dc, old);
}

HWND CreateLabel(McState* st, const wchar_t* text, int x, int y, int w,
                 int height, bool bold = false, bool muted = false,
                 bool wrap = false) {
    DWORD style = WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX;
    // 多行提示不能加 SS_CENTERIMAGE，否则文字被压成一行并溢出到下方控件
    if (!wrap) style |= SS_CENTERIMAGE;
    HWND label = CreateWindowExW(0, L"STATIC", text, style,
        x, y, w, height, st->dlg, nullptr, InstOf(st->dlg), nullptr);
    if (label) {
        SendMessageW(label, WM_SETFONT,
                     (WPARAM)(bold ? st->bold : st->font), TRUE);
        if (muted) st->mutedLabels.push_back(label);
    }
    return label;
}

// 按实际字体与可用宽度测量自动换行后的文字高度（含少量留白）
int WrapTextHeight(McState* st, const std::wstring& text, int width) {
    if (text.empty()) return 0;
    HDC dc = CreateCompatibleDC(nullptr);
    if (!dc) return st->S(20);
    HGDIOBJ old = SelectObject(dc, st->font);
    RECT rc{0, 0, std::max(1, width), 0};
    DrawTextW(dc, text.c_str(), -1, &rc,
              DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX);
    SelectObject(dc, old);
    DeleteDC(dc);
    int height = rc.bottom - rc.top;
    if (height < st->S(16)) height = st->S(16);
    return height + st->S(6);
}

HWND CreateOwnerButton(McState* st, const wchar_t* text,
                       int x, int y, int w, int h, int id) {
    HWND btn = CreateWindowExW(0, L"BUTTON", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        x, y, w, h, st->dlg, (HMENU)(INT_PTR)id, InstOf(st->dlg), nullptr);
    if (btn) SendMessageW(btn, WM_SETFONT, (WPARAM)st->font, TRUE);
    return btn;
}

HWND CreateEdit(McState* st, const wchar_t* text, int x, int y, int w, int h,
                int id) {
    HWND e = CreateWindowExW(0, L"EDIT", text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_CENTER | ES_AUTOHSCROLL,
        x, y, w, h, st->dlg, (HMENU)(INT_PTR)id, InstOf(st->dlg), nullptr);
    if (e) SendMessageW(e, WM_SETFONT, (WPARAM)st->font, TRUE);
    return e;
}

HWND CreateCombo(McState* st, int x, int y, int w, int h, int id,
                 const std::vector<std::wstring>& items, int selected) {
    // 下拉列表高度由窗口高度决定：额外留出列表区域，否则展开只能看到一行
    int listExtent = h + st->S(170);
    HWND combo = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        x, y, w, listExtent, st->dlg, (HMENU)(INT_PTR)id,
        InstOf(st->dlg), nullptr);
    if (!combo) return nullptr;

    SetWindowTheme(combo, L"", L""); // 关闭视觉样式，避免列表白底
    SendMessageW(combo, WM_SETFONT, (WPARAM)st->font, TRUE);
    SendMessageW(combo, CB_SETITEMHEIGHT, (WPARAM)-1, (LPARAM)st->S(24));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, (LPARAM)st->S(24));
    for (const auto& item : items) {
        SendMessageW(combo, CB_ADDSTRING, 0, (LPARAM)item.c_str());
    }
    if (selected >= 0 && selected < static_cast<int>(items.size())) {
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
    } else if (!items.empty()) {
        SendMessageW(combo, CB_SETCURSEL, 0, 0);
    }
    return combo;
}

// ---------------------------------------------------------------------------
// schema / 配置解析
// ---------------------------------------------------------------------------
int ParseIntValue(const mini_json::Value& v, int fallback) {
    return static_cast<int>(v.as_number(static_cast<double>(fallback)));
}

std::vector<std::pair<int, int>> ParseRangeArray(const mini_json::Value& value) {
    std::vector<std::pair<int, int>> out;
    if (!value.is_array()) return out;
    for (const auto& row : value.as_array()) {
        const mini_json::Value* s = row.find("start");
        const mini_json::Value* e = row.find("end");
        if (!s || !e) continue;
        int start = ParseIntValue(*s, 1);
        int end = ParseIntValue(*e, 1);
        if (start < 1) start = 1;
        if (start > 7) start = 7;
        if (end < 1) end = 1;
        if (end > 7) end = 7;
        out.emplace_back(start, end);
    }
    return out;
}

void ParseSchema(McState* st, const std::string& schemaJson) {
    st->items.clear();
    mini_json::Value root;
    try {
        root = mini_json::Value::parse(schemaJson);
    } catch (...) {
        return;
    }
    const mini_json::Value* arr = root.find("items");
    if (!arr || !arr->is_array()) return;

    for (const auto& raw : arr->as_array()) {
        if (!raw.is_object()) continue;
        if (st->items.size() >= 16) break;

        ModuleItem item;
        const mini_json::Value* key = raw.find("key");
        const mini_json::Value* type = raw.find("type");
        if (!key || !type) continue;
        item.key = key->as_string();
        if (item.key.empty()) continue;
        std::string t = type->as_string();
        if (t == "bool") item.type = ItemType::Bool;
        else if (t == "int") item.type = ItemType::Int;
        else if (t == "choice") item.type = ItemType::Choice;
        else if (t == "dayRangeList") item.type = ItemType::DayRangeList;
        else continue;

        if (const mini_json::Value* v = raw.find("label")) {
            item.label = utils::Utf8ToWide(v->as_string());
        }
        if (item.label.empty()) item.label = utils::Utf8ToWide(item.key);
        if (const mini_json::Value* v = raw.find("hint")) {
            item.hint = utils::Utf8ToWide(v->as_string());
        }

        if (item.type == ItemType::Bool) {
            if (const mini_json::Value* v = raw.find("default")) {
                item.defBool = v->as_bool(false);
            }
            item.valueBool = item.defBool;
        } else if (item.type == ItemType::Int) {
            if (const mini_json::Value* v = raw.find("min")) item.min = ParseIntValue(*v, 0);
            if (const mini_json::Value* v = raw.find("max")) item.max = ParseIntValue(*v, 100);
            if (const mini_json::Value* v = raw.find("step")) item.step = ParseIntValue(*v, 1);
            if (item.min > item.max) std::swap(item.min, item.max);
            if (item.step <= 0) item.step = 1;
            if (const mini_json::Value* v = raw.find("default")) {
                item.defInt = ParseIntValue(*v, item.min);
            }
            item.defInt = std::max(item.min, std::min(item.max, item.defInt));
            item.valueInt = item.defInt;
        } else if (item.type == ItemType::Choice) {
            if (const mini_json::Value* opts = raw.find("options")) {
                if (opts->is_array()) {
                    for (const auto& opt : opts->as_array()) {
                        const mini_json::Value* val = opt.find("value");
                        const mini_json::Value* lab = opt.find("label");
                        if (!val) continue;
                        std::wstring label = lab
                            ? utils::Utf8ToWide(lab->as_string())
                            : utils::Utf8ToWide(val->as_string());
                        item.options.emplace_back(val->as_string(), label);
                    }
                }
            }
            if (const mini_json::Value* v = raw.find("default")) {
                item.defChoice = v->as_string();
            }
            item.valueChoice = item.defChoice;
        } else { // DayRangeList
            if (const mini_json::Value* v = raw.find("max")) {
                item.maxRows = ParseIntValue(*v, 7);
                item.maxRows = std::max(0, std::min(7, item.maxRows));
            }
            if (const mini_json::Value* v = raw.find("default")) {
                item.defRanges = ParseRangeArray(*v);
            }
            item.ranges = item.defRanges;
        }

        st->items.push_back(std::move(item));
    }
}

void ParseCurrentConfig(McState* st, const std::string& currentJson) {
    mini_json::Value root;
    try {
        root = mini_json::Value::parse(currentJson);
    } catch (...) {
        return;
    }
    if (!root.is_object()) return;

    for (auto& item : st->items) {
        const mini_json::Value* v = root.find(item.key);
        if (!v) continue;
        switch (item.type) {
            case ItemType::Bool:
                item.valueBool = v->as_bool(item.defBool);
                break;
            case ItemType::Int: {
                int value = ParseIntValue(*v, item.defInt);
                item.valueInt = std::max(item.min, std::min(item.max, value));
                break;
            }
            case ItemType::Choice: {
                std::string value = v->as_string();
                bool found = false;
                for (const auto& opt : item.options) {
                    if (opt.first == value) { found = true; break; }
                }
                item.valueChoice = found ? value : item.defChoice;
                break;
            }
            case ItemType::DayRangeList:
                item.ranges = ParseRangeArray(*v);
                if (static_cast<int>(item.ranges.size()) > item.maxRows) {
                    item.ranges.resize(static_cast<size_t>(item.maxRows));
                }
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// 布局
// ---------------------------------------------------------------------------
int ContentHeight(McState* st) {
    int width = st->S(kWinW) - 2 * st->S(kPad);
    int y = st->S(kPad);
    y += st->S(26);                      // 模块名
    if (!st->moduleDescription.empty()) {
        y += WrapTextHeight(st, st->moduleDescription, width);
    }
    y += st->S(10);
    for (const auto& item : st->items) {
        y += st->S(26);                  // 标签
        if (!item.hint.empty()) {
            y += WrapTextHeight(st, item.hint, width);
        }
        if (item.type == ItemType::DayRangeList) {
            int rows = static_cast<int>(item.ranges.size());
            // 行高 + 与“添加时间段”按钮之间的 8px 间距 + 按钮高 + 余量
            y += rows * st->S(kRangeRowH) + st->S(8) + st->S(kBtnH) + st->S(4);
        } else {
            y += st->S(40);
        }
        y += st->S(4);
    }
    y += st->S(kPad);
    y += st->S(kBtnH) + st->S(kPad) + st->S(12); // 底部按钮与分隔线

    // 星期浮层已是独立窗口，这里不再需要为其预留高度
    if (y < st->S(240)) y = st->S(240);
    return y;
}

int FooterTop(McState* st, int clientH) {
    return clientH - st->S(kPad) - st->S(kBtnH) - st->S(12);
}

void CloseDayPopup(McState* st) {
    if (!st) return;
    HWND popup = st->hDayPopup;
    st->hDayPopup = nullptr;
    st->popupTrigger = nullptr;
    st->popupItem = nullptr;
    st->popupRow = -1;
    st->popupIsEnd = false;
    st->dayPopupHover = -1;
    st->dayPopupTracking = false;
    if (popup && IsWindow(popup)) DestroyWindow(popup);
}

int DayPopupIndexAt(McState* st, int y) {
    int itemH = st->S(22);
    if (itemH <= 0) return 0;
    int idx = (y - st->S(2)) / itemH;
    if (idx < 0 || idx > 6) return 0;
    return idx + 1;
}

void PaintDayPopup(HWND hwnd, McState* st) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    if (!dc) return;
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (!st || cr.right <= 0 || cr.bottom <= 0) {
        EndPaint(hwnd, &ps);
        return;
    }

    int itemH = st->S(22);
    int pad = st->S(2);

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(kChipBg);
    FillRect(mem, &cr, bg);
    DeleteObject(bg);

    for (int d = 0; d < 7; ++d) {
        RECT rc{0, pad + d * itemH, cr.right, pad + (d + 1) * itemH};
        if (rc.top >= cr.bottom) break;
        if (rc.bottom > cr.bottom) rc.bottom = cr.bottom;
        bool sel = st->dayPopupSel == d + 1;
        bool hov = st->dayPopupHover == d + 1;
        HBRUSH br = CreateSolidBrush(sel ? kAccent
                                         : (hov ? RGB(48, 48, 60) : kChipBg));
        FillRect(mem, &rc, br);
        DeleteObject(br);

        RECT trc = rc;
        trc.left += st->S(12);
        HFONT oldFont = static_cast<HFONT>(SelectObject(mem, st->font));
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, sel ? RGB(255, 255, 255) : kValueText);
        DrawTextW(mem, kDayNames[d], -1, &trc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if (oldFont) SelectObject(mem, oldFont);
    }

    HBRUSH border = CreateSolidBrush(kChipBorder);
    FrameRect(mem, &cr, border);
    DeleteObject(border);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK DayPopupWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    McState* st = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        st = reinterpret_cast<McState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
    } else {
        st = reinterpret_cast<McState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_MOUSEACTIVATE:
            // 不抢占对话框的激活状态，否则会触发 WM_ACTIVATE(WA_INACTIVE) 立即收起
            return MA_NOACTIVATE;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintDayPopup(hwnd, st);
            return 0;

        case WM_MOUSEMOVE: {
            if (!st) break;
            int idx = DayPopupIndexAt(st, GET_Y_LPARAM(lp));
            if (idx != st->dayPopupHover) {
                st->dayPopupHover = idx;
                InvalidateRect(hwnd, nullptr, FALSE);
            }
            if (!st->dayPopupTracking) {
                TRACKMOUSEEVENT tme{};
                tme.cbSize = sizeof(tme);
                tme.dwFlags = TME_LEAVE;
                tme.hwndTrack = hwnd;
                TrackMouseEvent(&tme);
                st->dayPopupTracking = true;
            }
            return 0;
        }

        case WM_MOUSELEAVE:
            if (st) {
                st->dayPopupTracking = false;
                if (st->dayPopupHover != 0) {
                    st->dayPopupHover = 0;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;

        case WM_LBUTTONDOWN: {
            if (!st || !st->dlg) break;
            int idx = DayPopupIndexAt(st, GET_Y_LPARAM(lp));
            // 交回对话框统一处理：避免在浮层自身的消息处理中销毁窗口
            PostMessageW(st->dlg, WM_APP_CLOSE_DAY_POPUP,
                         static_cast<WPARAM>(idx), 0);
            return 0;
        }

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void OpenDayPopup(McState* st, HWND trigger, ModuleItem& item, int row,
                  bool isEnd) {
    CloseDayPopup(st);
    if (!st->dlg || !trigger) return;
    if (row < 0 || row >= static_cast<int>(item.ranges.size())) return;

    RECT tr{};
    GetWindowRect(trigger, &tr); // 屏幕坐标：浮层是独立顶层窗口

    int itemH = st->S(22);
    int pad = st->S(2);
    int w = tr.right - tr.left;
    int h = 7 * itemH + 2 * pad;
    int x = tr.left;
    int y = tr.bottom + st->S(2);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    RECT work{};
    HMONITOR mon = MonitorFromWindow(trigger, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }

    // 下方放不下时向上展开，避免覆盖触发按钮
    if (y + h > work.bottom) {
        int above = tr.top - st->S(2) - h;
        if (above >= work.top) {
            y = above;
        } else if (y + h > work.bottom) {
            h = work.bottom - y;
        }
    }
    if (h < itemH) h = itemH;
    if (x + w > work.right) x = work.right - w;
    if (x < work.left) x = work.left;

    int current = isEnd ? item.ranges[row].second : item.ranges[row].first;
    st->popupTrigger = trigger;
    st->popupItem = &item;
    st->popupRow = row;
    st->popupIsEnd = isEnd;
    st->dayPopupSel = (current >= 1 && current <= 7) ? current : 1;
    st->dayPopupHover = 0;
    st->dayPopupTracking = false;

    HWND popup = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kDayPopupClass, L"", WS_POPUP, x, y, w, h,
        st->dlg, nullptr, InstOf(st->dlg), st);
    if (!popup) {
        CloseDayPopup(st);
        return;
    }
    st->hDayPopup = popup;
    ShowWindow(popup, SW_SHOWNOACTIVATE);
    UpdateWindow(popup);
}

void RebuildRangeRows(McState* st, ModuleItem& item) {
    CloseDayPopup(st);
    for (HWND h : item.rowStart) if (h) DestroyWindow(h);
    for (HWND h : item.rowEnd) if (h) DestroyWindow(h);
    for (HWND h : item.rowDelete) if (h) DestroyWindow(h);
    item.rowStart.clear();
    item.rowEnd.clear();
    item.rowDelete.clear();
    if (item.addBtn) {
        DestroyWindow(item.addBtn);
        item.addBtn = nullptr;
    }

    int startX = st->S(kPad);
    RECT cr{};
    GetClientRect(st->dlg, &cr);
    int width = cr.right;
    int comboW = st->S(120);
    int toX = startX + comboW + st->S(8);
    int endX = toX + st->S(20);
    int delW = st->S(30);
    int delX = width - st->S(kPad) - delW;

    for (size_t i = 0; i < item.ranges.size(); ++i) {
        int y = item.rowsTop + static_cast<int>(i) * st->S(kRangeRowH);
        int startDay = std::max(1, std::min(7, item.ranges[i].first));
        int endDay   = std::max(1, std::min(7, item.ranges[i].second));

        HWND start = CreateOwnerButton(st, kDayNames[startDay - 1],
                                       startX, y, comboW, st->S(kRowH),
                                       IDC_MC_RANGE_START_BASE + static_cast<int>(i));
        HWND end = CreateOwnerButton(st, kDayNames[endDay - 1],
                                     endX, y, comboW, st->S(kRowH),
                                     IDC_MC_RANGE_END_BASE + static_cast<int>(i));
        HWND del = CreateOwnerButton(st, L"\u00d7", delX, y, delW, st->S(kRowH),
                                     IDC_MC_RANGE_DELETE_BASE + static_cast<int>(i));
        if (start) item.rowStart.push_back(start);
        if (end) item.rowEnd.push_back(end);
        if (del) item.rowDelete.push_back(del);
    }

    int rows = static_cast<int>(item.ranges.size());
    int addY = item.rowsTop + rows * st->S(kRangeRowH) + st->S(8);
    item.addBtn = CreateOwnerButton(st, L"\u6dfb\u52a0\u65f6\u95f4\u6bb5",
                                    st->S(kPad), addY,
                                    width - 2 * st->S(kPad), st->S(kBtnH),
                                    IDC_MC_RANGES_ADD);
    if (item.ranges.size() >= static_cast<size_t>(std::max(0, item.maxRows)) &&
        item.addBtn) {
        EnableWindow(item.addBtn, FALSE);
    }
}

void BuildControls(McState* st, HWND hwnd) {
    RECT cr{};
    GetClientRect(hwnd, &cr);
    int width = cr.right;
    int y = st->S(kPad);

    CreateLabel(st, st->moduleName.c_str(), st->S(kPad), y, width - 2 * st->S(kPad),
                st->S(26), true);
    y += st->S(26);
    if (!st->moduleDescription.empty()) {
        int descH = WrapTextHeight(st, st->moduleDescription,
                                   width - 2 * st->S(kPad));
        CreateLabel(st, st->moduleDescription.c_str(),
                    st->S(kPad), y, width - 2 * st->S(kPad), descH,
                    false, true, true);
        y += descH;
    }
    y += st->S(10);

    for (auto& item : st->items) {
        CreateLabel(st, item.label.c_str(), st->S(kPad), y,
                    width - 2 * st->S(kPad), st->S(24));
        y += st->S(26);
        if (!item.hint.empty()) {
            int hintH = WrapTextHeight(st, item.hint, width - 2 * st->S(kPad));
            CreateLabel(st, item.hint.c_str(), st->S(kPad), y,
                        width - 2 * st->S(kPad), hintH, false, true, true);
            y += hintH;
        }

        if (item.type == ItemType::Bool) {
            int w = st->S(64);
            item.boolBtn = CreateOwnerButton(st,
                item.valueBool ? L"\u5f00" : L"\u5173",
                width - st->S(kPad) - w, y, w, st->S(kRowH),
                kItemBoolBase + static_cast<int>(&item - st->items.data()));
            y += st->S(40);
        } else if (item.type == ItemType::Int) {
            wchar_t buf[32];
            swprintf_s(buf, L"%d", item.valueInt);
            int w = st->S(140);
            item.intEdit = CreateEdit(st, buf, width - st->S(kPad) - w, y,
                                      w, st->S(kRowH),
                                      kItemIntBase + static_cast<int>(&item - st->items.data()));
            y += st->S(40);
        } else if (item.type == ItemType::Choice) {
            std::vector<std::wstring> labels;
            int selected = 0;
            for (size_t i = 0; i < item.options.size(); ++i) {
                labels.push_back(item.options[i].second);
                if (item.options[i].first == item.valueChoice) {
                    selected = static_cast<int>(i);
                }
            }
            if (labels.empty()) labels.push_back(L"");
            int w = st->S(180);
            item.choiceCombo = CreateCombo(st, width - st->S(kPad) - w, y,
                                           w, st->S(kRowH),
                                           kItemChoiceBase + static_cast<int>(&item - st->items.data()),
                                           labels, selected);
            y += st->S(40);
        } else { // DayRangeList
            item.rowsTop = y;
            RebuildRangeRows(st, item);
            int rows = static_cast<int>(item.ranges.size());
            y += rows * st->S(kRangeRowH) + st->S(8) + st->S(kBtnH) + st->S(4);
        }
        y += st->S(4);
    }

    int footerY = FooterTop(st, cr.bottom);
    st->footerY = footerY;
    int bx = width - st->S(kPad) - st->S(88);
    st->okBtn = CreateOwnerButton(st, L"\u4fdd\u5b58", bx, footerY,
                                  st->S(88), st->S(kBtnH), IDC_MC_OK);
    bx -= st->S(88) + st->S(12);
    st->cancelBtn = CreateOwnerButton(st, L"\u53d6\u6d88", bx, footerY,
                                      st->S(88), st->S(kBtnH), IDC_MC_CANCEL);
}

// ---------------------------------------------------------------------------
// 自绘
// ---------------------------------------------------------------------------
void PaintDialog(McState* st, HWND hwnd) {
    PAINTSTRUCT ps{};
    HDC dc = BeginPaint(hwnd, &ps);
    if (!dc) return;
    RECT cr{};
    GetClientRect(hwnd, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) { EndPaint(hwnd, &ps); return; }

    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    FillRect(mem, &cr, st->brBg);
    FillRound(mem, RECT{0, 0, cr.right - 1, cr.bottom - 1}, st->S(18),
              kBg, kChipBorder);

    int divY = FooterTop(st, cr.bottom) - st->S(10);
    HPEN pen = CreatePen(PS_SOLID, 1, kDivider);
    HGDIOBJ op = SelectObject(mem, pen);
    MoveToEx(mem, st->S(kPad), divY, nullptr);
    LineTo(mem, cr.right - st->S(kPad), divY);
    SelectObject(mem, op);
    DeleteObject(pen);

    BitBlt(dc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

void DrawButton(McState* st, DRAWITEMSTRUCT* dis) {
    RECT rc = dis->rcItem;
    bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    bool focused = (dis->itemState & ODS_FOCUS) != 0;
    int radius = std::min(st->S(8), static_cast<int>(rc.bottom - rc.top) / 2);
    HDC dc = dis->hDC;

    if (dis->CtlID == IDC_MC_OK) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 74, 196) : kAccent, kAccentHi);
        DrawCentered(dc, rc, L"\u4fdd\u5b58", st->bold, RGB(255, 255, 255));
        return;
    }
    if (dis->CtlID == IDC_MC_CANCEL) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 48, 60) : kChipBg, kChipBorder);
        DrawCentered(dc, rc, L"\u53d6\u6d88", st->font, kLabel);
        return;
    }
    if (dis->CtlID == IDC_MC_RANGES_ADD) {
        bool enabled = IsWindowEnabled(dis->hwndItem) != FALSE;
        FillRound(dc, rc, radius,
                  pressed ? RGB(46, 46, 57) : kChipBg,
                  focused ? kAccent : kChipBorder);
        DrawCentered(dc, rc, L"\u6dfb\u52a0\u65f6\u95f4\u6bb5", st->font,
                     enabled ? kValueText : kMuted);
        return;
    }

    bool dayStart = dis->CtlID >= IDC_MC_RANGE_START_BASE &&
                    dis->CtlID < IDC_MC_RANGE_START_BASE + 7;
    bool dayEnd = dis->CtlID >= IDC_MC_RANGE_END_BASE &&
                  dis->CtlID < IDC_MC_RANGE_END_BASE + 7;
    if (dayStart || dayEnd) {
        bool open = (st->popupTrigger == dis->hwndItem);
        FillRound(dc, rc, radius, pressed ? RGB(46, 46, 57) : kChipBg,
                  open ? kAccentHi : (focused ? kAccent : kChipBorder));
        wchar_t text[64] = {0};
        GetWindowTextW(dis->hwndItem, text, 64);
        RECT trc = rc;
        trc.left += st->S(10);
        trc.right -= st->S(20);
        HFONT old = (HFONT)SelectObject(dc, st->font);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, kValueText);
        DrawTextW(dc, text, -1, &trc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX
                      | DT_END_ELLIPSIS);
        if (old) SelectObject(dc, old);

        int cx = rc.right - st->S(12);
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

    // 删除按钮 / 布尔开关
    wchar_t text[64] = {0};
    GetWindowTextW(dis->hwndItem, text, 64);
    bool on = false;
    if (dis->CtlID >= kItemBoolBase) {
        int index = dis->CtlID - kItemBoolBase;
        if (index >= 0 && index < static_cast<int>(st->items.size())) {
            on = st->items[index].valueBool;
        }
    }
    if (on) {
        FillRound(dc, rc, radius,
                  pressed ? RGB(48, 60, 110) : kOnBg, kOnBorder);
        DrawCentered(dc, rc, L"\u5f00", st->font, kValueText);
    } else {
        FillRound(dc, rc, radius,
                  pressed ? RGB(46, 46, 57) : kChipBg,
                  focused ? kAccent : kChipBorder);
        DrawCentered(dc, rc, text, st->font,
                     dis->CtlID >= kItemBoolBase ? kMuted : kLabel);
    }
}

void DrawComboItem(McState* st, DRAWITEMSTRUCT* dis) {
    UINT itemID = dis->itemID;
    if (itemID == static_cast<UINT>(-1)) {
        int sel = static_cast<int>(
            SendMessageW(dis->hwndItem, CB_GETCURSEL, 0, 0));
        if (sel >= 0) itemID = static_cast<UINT>(sel);
    }

    // 即使 itemID 无效也先填深色底，避免收起状态露出系统白底
    bool selected = (dis->itemState & ODS_SELECTED) != 0 &&
                    (dis->itemState & ODS_COMBOBOXEDIT) == 0;
    COLORREF bg = selected ? kAccent : kChipBg;
    HBRUSH br = CreateSolidBrush(bg);
    FillRect(dis->hDC, &dis->rcItem, br);
    DeleteObject(br);
    if (itemID == static_cast<UINT>(-1)) return;

    wchar_t text[256] = {0};
    SendMessageW(dis->hwndItem, CB_GETLBTEXT, itemID, (LPARAM)text);

    RECT trc = dis->rcItem;
    trc.left += st->S(10);
    trc.right -= st->S(8);
    HFONT old = (HFONT)SelectObject(dis->hDC, st->font);
    SetBkMode(dis->hDC, TRANSPARENT);
    SetTextColor(dis->hDC, selected ? RGB(255, 255, 255) : kValueText);
    DrawTextW(dis->hDC, text, -1, &trc,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    if (old) SelectObject(dis->hDC, old);
}

// ---------------------------------------------------------------------------
// 保存
// ---------------------------------------------------------------------------
bool CollectResult(McState* st, std::string& outJson) {
    using J = mini_json::Value;
    mini_json::Object root;

    for (auto& item : st->items) {
        switch (item.type) {
            case ItemType::Bool:
                root.emplace_back(item.key, J(item.valueBool));
                break;
            case ItemType::Int: {
                wchar_t buf[64] = {0};
                GetWindowTextW(item.intEdit, buf, 64);
                int value = 0;
                if (!ParseIntText(buf, value)) {
                    MessageBoxW(st->dlg,
                        (L"\u201c" + item.label +
                         L"\u201d\u5fc5\u987b\u662f\u6574\u6570").c_str(),
                        L"\u63d0\u793a", MB_ICONWARNING);
                    return false;
                }
                if (value < item.min || value > item.max) {
                    MessageBoxW(st->dlg,
                        (L"\u201c" + item.label + L"\u201d\u5e94\u5728 " +
                         std::to_wstring(item.min) + L"\u2013" +
                         std::to_wstring(item.max) + L" \u4e4b\u95f4").c_str(),
                        L"\u63d0\u793a", MB_ICONWARNING);
                    return false;
                }
                root.emplace_back(item.key, J(value));
                break;
            }
            case ItemType::Choice: {
                int sel = static_cast<int>(
                    SendMessageW(item.choiceCombo, CB_GETCURSEL, 0, 0));
                if (sel < 0 || sel >= static_cast<int>(item.options.size())) sel = 0;
                std::string value = item.options.empty()
                                        ? item.defChoice
                                        : item.options[sel].first;
                root.emplace_back(item.key, J(value));
                break;
            }
            case ItemType::DayRangeList: {
                std::vector<std::pair<int, int>> ranges = item.ranges;
                for (const auto& r : ranges) {
                    if (r.first < 1 || r.first > 7 || r.second < 1 ||
                        r.second > 7 || r.first > r.second) {
                        MessageBoxW(st->dlg,
                            L"\u65f6\u95f4\u6bb5\u661f\u671f\u8303\u56f4\u65e0\u6548",
                            L"\u63d0\u793a", MB_ICONWARNING);
                        return false;
                    }
                }
                // 不重叠校验
                for (size_t i = 0; i < ranges.size(); ++i) {
                    for (size_t j = i + 1; j < ranges.size(); ++j) {
                        if (ranges[i].first <= ranges[j].second &&
                            ranges[j].first <= ranges[i].second) {
                            MessageBoxW(st->dlg,
                                L"\u65f6\u95f4\u6bb5\u4e0d\u80fd\u91cd\u53e0",
                                L"\u63d0\u793a", MB_ICONWARNING);
                            return false;
                        }
                    }
                }
                if (static_cast<int>(ranges.size()) > item.maxRows) {
                    MessageBoxW(st->dlg,
                        L"\u65f6\u95f4\u6bb5\u6570\u91cf\u8d85\u8fc7\u4e0a\u9650",
                        L"\u63d0\u793a", MB_ICONWARNING);
                    return false;
                }

                mini_json::Array arr;
                for (const auto& r : ranges) {
                    mini_json::Object row;
                    row.emplace_back("start", J(r.first));
                    row.emplace_back("end", J(r.second));
                    arr.emplace_back(J(std::move(row)));
                }
                root.emplace_back(item.key, J(std::move(arr)));
                break;
            }
        }
    }

    outJson = J::serialize(J(std::move(root)), false);
    return true;
}

// ---------------------------------------------------------------------------
// 重建全部控件（增删时间段后按实际行数调整窗口高度）
// ---------------------------------------------------------------------------
void RebuildAll(McState* st) {
    if (!st || !st->dlg) return;
    CloseDayPopup(st);

    HWND child = GetWindow(st->dlg, GW_CHILD);
    while (child) {
        HWND next = GetWindow(child, GW_HWNDNEXT);
        DestroyWindow(child);
        child = next;
    }

    st->mutedLabels.clear();
    st->okBtn = nullptr;
    st->cancelBtn = nullptr;
    for (auto& item : st->items) {
        item.boolBtn = nullptr;
        item.intEdit = nullptr;
        item.choiceCombo = nullptr;
        item.addBtn = nullptr;
        item.rowStart.clear();
        item.rowEnd.clear();
        item.rowDelete.clear();
    }

    RECT wr{};
    GetWindowRect(st->dlg, &wr);
    int width = wr.right - wr.left;
    int height = ContentHeight(st);

    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = MonitorFromWindow(st->dlg, MONITOR_DEFAULTTONEAREST);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        int maxH = mi.rcWork.bottom - wr.top - st->S(8);
        if (height > maxH) height = maxH;
    }
    if (height < st->S(160)) height = st->S(160);

    SetWindowPos(st->dlg, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowRgn(st->dlg,
        CreateRoundRectRgn(0, 0, width + 1, height + 1,
                           st->S(18), st->S(18)), TRUE);
    BuildControls(st, st->dlg);
    if (st->okBtn) SetFocus(st->okBtn);
    InvalidateRect(st->dlg, nullptr, TRUE);
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK McWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    McState* st = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        st = reinterpret_cast<McState*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        st->dlg = hwnd;
    } else {
        st = reinterpret_cast<McState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    switch (msg) {
        case WM_CREATE:
            BuildControls(st, hwnd);
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
            PaintDialog(st, hwnd);
            return 0;

        case WM_DRAWITEM: {
            auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
            if (!st || !dis) break;
            if (dis->CtlType == ODT_BUTTON) {
                DrawButton(st, dis);
                return TRUE;
            }
            if (dis->CtlType == ODT_COMBOBOX) {
                DrawComboItem(st, dis);
                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORSTATIC: {
            if (!st || !st->brBg) break;
            HDC dc = reinterpret_cast<HDC>(wp);
            SetBkMode(dc, TRANSPARENT);
            SetBkColor(dc, kBg);
            SetTextColor(dc, kLabel);
            HWND ctl = reinterpret_cast<HWND>(lp);
            if (std::find(st->mutedLabels.begin(), st->mutedLabels.end(), ctl) !=
                st->mutedLabels.end()) {
                SetTextColor(dc, kMuted);
            }
            return reinterpret_cast<LRESULT>(st->brBg);
        }

        case WM_CTLCOLOREDIT: {
            if (!st || !st->brChip) break;
            HDC dc = reinterpret_cast<HDC>(wp);
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

        case WM_ACTIVATE:
            if (st && LOWORD(wp) == WA_INACTIVE) {
                CloseDayPopup(st);
            } else if (st && !st->initialFocusDone) {
                // 首次激活时把焦点放到“保存”，避免默认落在“取消”上
                st->initialFocusDone = true;
                if (st->okBtn) SetFocus(st->okBtn);
            }
            break;

        case WM_LBUTTONDOWN:
            if (st) CloseDayPopup(st);
            return 0;

        case WM_APP_REBUILD_MODULE:
            RebuildAll(st);
            return 0;

        case WM_APP_CLOSE_DAY_POPUP: {
            if (!st) return 0;
            int day = static_cast<int>(wp);
            HWND trigger = st->popupTrigger;
            ModuleItem* item = st->popupItem;
            int row = st->popupRow;
            bool isEnd = st->popupIsEnd;
            if (day >= 1 && day <= 7 && item && row >= 0 &&
                row < static_cast<int>(item->ranges.size())) {
                if (isEnd) {
                    item->ranges[row].second = day;
                } else {
                    item->ranges[row].first = day;
                }
                if (trigger && IsWindow(trigger)) {
                    SetWindowTextW(trigger, kDayNames[day - 1]);
                    InvalidateRect(trigger, nullptr, TRUE);
                }
            }
            CloseDayPopup(st);
            if (trigger && IsWindow(trigger)) SetFocus(trigger);
            return 0;
        }

        case WM_COMMAND: {
            if (!st) break;
            WORD id = LOWORD(wp);
            WORD code = HIWORD(wp);
            HWND ctl = reinterpret_cast<HWND>(lp);

            // 点击其它控件时收起星期浮层（与触发按钮同一切换行为）
            if ((code == BN_SETFOCUS || code == EN_SETFOCUS) &&
                st->hDayPopup && ctl != st->popupTrigger) {
                CloseDayPopup(st);
            }

            bool dayStart = id >= IDC_MC_RANGE_START_BASE &&
                            id < IDC_MC_RANGE_START_BASE + 7;
            bool dayEnd = id >= IDC_MC_RANGE_END_BASE &&
                          id < IDC_MC_RANGE_END_BASE + 7;
            if ((dayStart || dayEnd) && code == BN_CLICKED) {
                bool isEnd = dayEnd;
                int row = isEnd ? id - IDC_MC_RANGE_END_BASE
                                : id - IDC_MC_RANGE_START_BASE;
                for (auto& item : st->items) {
                    if (item.type != ItemType::DayRangeList) continue;
                    if (st->hDayPopup && st->popupTrigger == ctl) {
                        CloseDayPopup(st);
                        return 0;
                    }
                    if (row >= 0 && row < static_cast<int>(item.ranges.size())) {
                        OpenDayPopup(st, ctl, item, row, isEnd);
                    }
                    break;
                }
                return 0;
            }

            if (id == IDC_MC_RANGES_ADD && code == BN_CLICKED) {
                for (auto& item : st->items) {
                    if (item.type == ItemType::DayRangeList &&
                        static_cast<int>(item.ranges.size()) < item.maxRows) {
                        CloseDayPopup(st);
                        item.ranges.emplace_back(1, 1);
                        PostMessageW(hwnd, WM_APP_REBUILD_MODULE, 0, 0);
                        break;
                    }
                }
                return 0;
            }
            if (id >= IDC_MC_RANGE_DELETE_BASE &&
                id < IDC_MC_RANGE_DELETE_BASE + 7 && code == BN_CLICKED) {
                int index = id - IDC_MC_RANGE_DELETE_BASE;
                for (auto& item : st->items) {
                    if (item.type == ItemType::DayRangeList) {
                        if (index >= 0 &&
                            index < static_cast<int>(item.ranges.size())) {
                            CloseDayPopup(st);
                            item.ranges.erase(item.ranges.begin() + index);
                            PostMessageW(hwnd, WM_APP_REBUILD_MODULE, 0, 0);
                        }
                        break;
                    }
                }
                return 0;
            }
            if (id >= kItemBoolBase &&
                id < kItemBoolBase + static_cast<int>(st->items.size()) &&
                code == BN_CLICKED) {
                int index = id - kItemBoolBase;
                ModuleItem& item = st->items[index];
                if (item.type == ItemType::Bool) {
                    item.valueBool = !item.valueBool;
                    SetWindowTextW(item.boolBtn, item.valueBool ? L"\u5f00" : L"\u5173");
                    InvalidateRect(item.boolBtn, nullptr, TRUE);
                }
                return 0;
            }
            if (id == IDC_MC_OK || id == IDOK) {
                std::string json;
                if (CollectResult(st, json)) {
                    st->lastJson = std::move(json);
                    st->saved = true;
                    DestroyWindow(hwnd);
                }
                return 0;
            }
            if (id == IDC_MC_CANCEL || id == IDCANCEL) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        }

        case DM_GETDEFID:
            return MAKELRESULT(IDC_MC_OK, DC_HASDEFID);

        case WM_CLOSE:
            if (st) CloseDayPopup(st);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            if (st) {
                CloseDayPopup(st);
                st->dlg = nullptr;
                st->running = false;
            }
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool RegisterMcClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kClass, &exist) != 0) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = 0;
    wc.lpfnWndProc   = McWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kClass;
    return RegisterClassExW(&wc) != 0;
}

bool RegisterDayPopupClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, kDayPopupClass, &exist) != 0) return true;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_DROPSHADOW;
    wc.lpfnWndProc   = DayPopupWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kDayPopupClass;
    return RegisterClassExW(&wc) != 0;
}

} // namespace

// ---------------------------------------------------------------------------
bool OpenArticleModuleConfigDialog(HWND parent,
                                   const ArticleModuleEntry& entry,
                                   const std::string& currentJson,
                                   std::string& outJson) {
    HINSTANCE hInst = parent ? InstOf(parent)
                             : GetModuleHandleW(nullptr);
    if (!RegisterMcClass(hInst)) return false;
    RegisterDayPopupClass(hInst);

    McState st;
    st.parent = parent;
    st.moduleId = entry.id;
    st.moduleName = entry.name;
    st.moduleDescription = entry.description;
    st.dpi = static_cast<UINT>(utils::GetWindowDpi(parent));

    ParseSchema(&st, entry.schemaJson);
    ParseCurrentConfig(&st, currentJson);

    st.font = CreateFontW(-st.S(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        utils::GetSystemDefaultFontName().c_str());
    st.bold = CreateFontW(-st.S(12), 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        utils::GetSystemDefaultFontName().c_str());
    st.brBg = CreateSolidBrush(kBg);
    st.brChip = CreateSolidBrush(kChipBg);

    int contentH = ContentHeight(&st);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    HMONITOR mon = parent ? MonitorFromWindow(parent, MONITOR_DEFAULTTONEAREST)
                          : nullptr;
    if (!mon || !GetMonitorInfoW(mon, &mi)) {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &mi.rcWork, 0);
    }
    int workW = mi.rcWork.right - mi.rcWork.left;
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    int winW = std::min(st.S(kWinW), workW - st.S(16));
    int winH = std::min(contentH, workH - st.S(16));
    int winX = mi.rcWork.left + (workW - winW) / 2;
    int winY = mi.rcWork.top + (workH - winH) / 2;

    std::wstring title = L"\u663e\u793a\u65b9\u5f0f\u8bbe\u7f6e - " + entry.name;
    st.dlg = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, kClass,
        title.c_str(),
        WS_POPUP | WS_CLIPCHILDREN,
        winX, winY, winW, winH,
        parent, nullptr, hInst, &st);
    if (!st.dlg) {
        if (st.font) DeleteObject(st.font);
        if (st.bold) DeleteObject(st.bold);
        if (st.brBg) DeleteObject(st.brBg);
        if (st.brChip) DeleteObject(st.brChip);
        return false;
    }

    SetWindowRgn(st.dlg,
        CreateRoundRectRgn(0, 0, winW + 1, winH + 1, st.S(18), st.S(18)), TRUE);
    if (parent) EnableWindow(parent, FALSE);
    ShowWindow(st.dlg, SW_SHOW);
    SetActiveWindow(st.dlg);
    UpdateWindow(st.dlg);
    SetFocus(st.okBtn);

    MSG msg;
    while (st.running) {
        if (st.parent && !IsWindow(st.parent)) break;
        BOOL got = GetMessage(&msg, nullptr, 0, 0);
        if (got <= 0) {
            if (got == 0) PostQuitMessage(static_cast<int>(msg.wParam));
            break;
        }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE && st.hDayPopup) {
            CloseDayPopup(&st);
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
    if (parent && IsWindow(parent)) EnableWindow(parent, TRUE);

    if (st.font) DeleteObject(st.font);
    if (st.bold) DeleteObject(st.bold);
    if (st.brBg) DeleteObject(st.brBg);
    if (st.brChip) DeleteObject(st.brChip);

    if (st.saved) {
        outJson = st.lastJson;
        return true;
    }
    return false;
}
