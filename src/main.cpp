// =============================================================================
// main.cpp - 程序入口
// =============================================================================
#include <windows.h>
#include <commctrl.h>

#include "countdown_window.h"
#include "resource.h"

// 单实例互斥体名称（会话级：同一登录会话内保证唯一）
static const wchar_t kSingleInstanceMutex[] = L"Local\\Countdown_SingleInstance_Mutex";

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // ---- 单实例保护：已有一个实例在运行时，通知它并退出本次启动 ----
    // 互斥体句柄由本函数持有直到进程退出，从而保护整个运行周期
    HANDLE hMutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutex);
    if (hMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // 通知已有实例打开设置窗口，作为"程序已在运行"的可见反馈
        HWND hMain = FindWindowW(CountdownWindow::ClassName(), nullptr);
        if (hMain) PostMessageW(hMain, WM_APP_OPEN_SETTINGS, 0, 0);
        CloseHandle(hMutex);
        return 0;
    }
    // 注册公共控件类
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC   = ICC_STANDARD_CLASSES | ICC_DATE_CLASSES
                | ICC_UPDOWN_CLASS | ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    if (!CountdownWindow::RegisterClass(hInstance)) {
        MessageBoxW(nullptr, L"\u6ce8\u518c\u7a97\u53e3\u7c7b\u5931\u8d25",
                    L"\u9519\u8bef", MB_ICONERROR);
        return 1;
    }

    CountdownWindow win;
    if (!win.Create(hInstance)) {
        MessageBoxW(nullptr, L"\u521b\u5efa\u7a97\u53e3\u5931\u8d25",
                    L"\u9519\u8bef", MB_ICONERROR);
        return 2;
    }

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return static_cast<int>(msg.wParam);
}
