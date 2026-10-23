// =============================================================================
// utils.h - 通用工具：字体枚举、剩余时长格式化、字符串编码转换
// =============================================================================
#pragma once
#include <windows.h>
#include <string>
#include <vector>
#include <ctime>

namespace utils {

// UTF-8 <-> UTF-16 转换
std::wstring Utf8ToWide(const std::string& utf8);
std::string  WideToUtf8(const std::wstring& wide);

// 获取系统默认 GUI 字体名称（通常为 "Segoe UI"）
std::wstring GetSystemDefaultFontName();

// 枚举系统已安装字体族名称（去重、按字母序）
std::vector<std::wstring> EnumerateSystemFonts();

// 倒计时显示格式
enum class DisplayFormat {
    HMS,   // HH:MM:SS
    Full,  // N年M月D日 H时M分S秒
    Days,  // N天 HH:MM:SS
};

// 将剩余秒数格式化为字符串
//   HMS : 总小时(可 >24):分:秒
//   Full: N年M月D日 H时M分S秒（按真实日历拆分，逐月累加，避免 365/30 近似误差）
// remaining_seconds < 0 表示已过期，返回 "已结束"
// includeStartDay=true 时把起始日计入倒计时（三种格式统一在剩余时间上加 1 天）
std::wstring FormatRemainingTime(long long remaining_seconds, DisplayFormat fmt,
                                 bool includeStartDay = false);

// 把 std::tm 格式化为 "YYYY-MM-DD HH:MM:SS"
std::wstring FormatTargetTime(const std::tm& t);

// 从 "YYYY-MM-DD HH:MM:SS" 解析为 std::tm，失败返回 false
bool ParseTargetTime(const std::wstring& s, std::tm& out);

// 获取 exe 文件所在目录（含末尾反斜杠）
std::wstring GetExeDir();

// 目录不存在时创建（已存在视为成功）
bool EnsureDirectory(const std::wstring& path);

// 列出目录下所有 .txt 文件（返回完整路径，按文件名排序）
std::vector<std::wstring> ListTxtFiles(const std::wstring& dir);

// 读取 UTF-8 文本文件（自动去除 BOM，超长截断）；失败返回 false
bool ReadTextFile(const std::wstring& path, std::wstring& out,
                  size_t maxBytes = 2 * 1024 * 1024);

// 以 UTF-8 写入文本文件（无 BOM）
bool WriteTextFile(const std::wstring& path, const std::wstring& text);

// 获取窗口所在显示器的 DPI（无法获取时按系统 DPI，失败回退 96）
int GetWindowDpi(HWND hwnd);

// 把 96 DPI 下的逻辑尺寸换算为当前 DPI 的物理像素
int ScaleDpi(int value, int dpi);

} // namespace utils
