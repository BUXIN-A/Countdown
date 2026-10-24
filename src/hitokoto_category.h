// =============================================================================
// hitokoto_category.h - 网络一言句子类型（对应 v1.hitokoto.cn 的 c 参数）
//
// 官方文档：https://developer.hitokoto.cn/sentence/
// code 为空表示“全部”（请求时不带 c 参数）。
// =============================================================================
#pragma once
#include <string>

namespace hitokoto_category {

struct Option {
    const wchar_t* name;
    const char*    code;
};

inline const Option kOptions[] = {
    { L"\u5168\u90e8",           ""  }, // 全部
    { L"\u52a8\u753b",           "a" }, // 动画
    { L"\u6f2b\u753b",           "b" }, // 漫画
    { L"\u6e38\u620f",           "c" }, // 游戏
    { L"\u6587\u5b66",           "d" }, // 文学
    { L"\u539f\u521b",           "e" }, // 原创
    { L"\u6765\u81ea\u7f51\u7edc", "f" }, // 来自网络
    { L"\u5176\u4ed6",           "g" }, // 其他
    { L"\u5f71\u89c6",           "h" }, // 影视
    { L"\u8bd7\u8bcd",           "i" }, // 诗词
    { L"\u7f51\u6613\u4e91",     "j" }, // 网易云
    { L"\u54f2\u5b66",           "k" }, // 哲学
    { L"\u6296\u673a\u7075",     "l" }, // 抖机灵
};

inline int Count() {
    return static_cast<int>(sizeof(kOptions) / sizeof(kOptions[0]));
}

// 返回 code 对应的下标；非法 code 返回 -1（“全部”为 0）
inline int IndexOfCode(const std::string& code) {
    for (int i = 0; i < Count(); ++i) {
        if (code == kOptions[i].code) return i;
    }
    return -1;
}

inline bool IsValidCode(const std::string& code) {
    return IndexOfCode(code) >= 0;
}

} // namespace hitokoto_category
