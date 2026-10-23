// =============================================================================
// autostart.cpp - 开机自启动实现
// =============================================================================
#include "autostart.h"

#include <windows.h>
#include <string>

static const wchar_t* kRunKey =
    L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const wchar_t* kValueName = L"Countdown";

bool autostart::SetEnabled(bool enabled) {
    HKEY key = nullptr;
    LONG r = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                             KEY_SET_VALUE, nullptr, &key, nullptr);
    if (r != ERROR_SUCCESS) return false;

    bool ok = true;
    if (enabled) {
        wchar_t path[MAX_PATH] = {0};
        if (GetModuleFileNameW(nullptr, path, MAX_PATH) == 0) {
            RegCloseKey(key);
            return false;
        }
        // 带引号，防止安装路径含空格时启动失败
        std::wstring cmd = L"\"" + std::wstring(path) + L"\"";
        r = RegSetValueExW(key, kValueName, 0, REG_SZ,
                           reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
        ok = (r == ERROR_SUCCESS);
    } else {
        r = RegDeleteValueW(key, kValueName);
        ok = (r == ERROR_SUCCESS || r == ERROR_FILE_NOT_FOUND);
    }

    RegCloseKey(key);
    return ok;
}
