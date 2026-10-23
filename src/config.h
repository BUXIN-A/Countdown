// =============================================================================
// config.h - 配置结构体与读写
// =============================================================================
#pragma once
#include <string>
#include <ctime>
#include "utils.h"

// 一言来源
enum class HitokotoSource {
    Local   = 0, // 仅本地
    Network = 1, // 仅网络
    Both    = 2, // 本地和网络
};

// 窗口锚点：9 宫格位置模板（在工作区内定位）
enum class WindowAnchor {
    TopLeft,     TopCenter,     TopRight,
    CenterLeft,  Center,        CenterRight,
    BottomLeft,  BottomCenter,  BottomRight,
};

struct Config {
    std::wstring title;             // 倒计时标题
    std::tm      targetTime{};      // 目标时间
    utils::DisplayFormat displayFormat = utils::DisplayFormat::HMS;
    int          fontSize = 48;    // 字号
    std::wstring font;             // 字体名

    HitokotoSource hitokotoSource = HitokotoSource::Both; // 一言来源
    int hitokotoRefreshSec = 60;                          // 一言刷新间隔（秒）

    bool         isTop    = false;   // 窗口是否始终置顶（true=压过全屏/置顶应用）
    WindowAnchor anchor   = WindowAnchor::TopCenter; // 窗口位置模板（默认顶部居中）
    int          placeX   = 0;       // 相对模板位置的水平偏移（像素，可为负）
    int          placeY   = 0;       // 相对模板位置的垂直偏移（像素，可为负）

    // 返回默认配置（目标 = 当前时间 + 7 天）
    static Config Defaults();

    // 从 exe 同目录 config.json 加载；失败则写入默认配置
    static Config Load();

    // 保存到 exe 同目录 config.json
    bool Save() const;
};
