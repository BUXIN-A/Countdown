// =============================================================================
// tray.cpp - 系统托盘实现
// =============================================================================
#include "tray.h"
#include "../resource.h"

bool Tray::Add(HWND owner, HICON icon, const std::wstring& tip) {
    nid_.cbSize = sizeof(nid_);
    nid_.hWnd   = owner;
    nid_.uID    = 1;
    nid_.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = WM_APP_TRAY;
    nid_.hIcon  = icon;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    bool ok = Shell_NotifyIconW(NIM_ADD, &nid_) == TRUE;
    added_ = ok;
    return ok;
}

void Tray::Remove() {
    if (!added_) return;
    Shell_NotifyIconW(NIM_DELETE, &nid_);
    added_ = false;
}

void Tray::SetTooltip(const std::wstring& tip) {
    if (!added_) return;
    wcsncpy_s(nid_.szTip, tip.c_str(), _TRUNCATE);
    nid_.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &nid_);
}

int Tray::ShowContextMenu(HWND owner, int x, int y) {
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, IDM_TRAY_SETTINGS,
                L"\u8bbe\u7f6e");          // "设置"
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_TRAY_EXIT,
                L"\u9000\u51fa");          // "退出"

    // MS 文档要求：弹出前 SetForegroundWindow，否则点击外部不会关闭
    SetForegroundWindow(owner);
    int cmd = static_cast<int>(TrackPopupMenu(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        x, y, 0, owner, nullptr));
    PostMessageW(owner, WM_NULL, 0, 0);
    DestroyMenu(menu);
    return cmd;
}
