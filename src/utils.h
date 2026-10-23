// =============================================================================
// utils.h - 通用工具：字体枚举、剩余时长格式化、字符串编码转换
// =============================================================================
#pragma once
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
std::wstring FormatRemainingTime(long long remaining_seconds, DisplayFormat fmt);

// 把 std::tm 格式化为 "YYYY-MM-DD HH:MM:SS"
std::wstring FormatTargetTime(const std::tm& t);

// 从 "YYYY-MM-DD HH:MM:SS" 解析为 std::tm，失败返回 false
bool ParseTargetTime(const std::wstring& s, std::tm& out);

// 获取 exe 文件所在目录（含末尾反斜杠）
std::wstring GetExeDir();

} // namespace utils
