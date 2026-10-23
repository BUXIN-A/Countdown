// =============================================================================
// tray.h - 系统托盘图标
// =============================================================================
#pragma once
#include <windows.h>
#include <string>

class Tray {
public:
    // 初始化托盘
    bool Add(HWND owner, HICON icon, const std::wstring& tip);
    // 移除托盘
    void Remove();
    // 更新提示文本
    void SetTooltip(const std::wstring& tip);
    // 显示右键菜单（在屏幕坐标处），返回所选命令 ID，未选返回 0
    int  ShowContextMenu(HWND owner, int x, int y);

private:
    NOTIFYICONDATAW nid_{};
    bool added_ = false;
};
