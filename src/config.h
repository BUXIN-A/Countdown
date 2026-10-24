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

// 循环周期：目标时间到达后按该单位自动顺延到下一次
enum class CycleUnit {
    None = 0,
    Year,
    Month,
    Day,
    Hour,
    Minute,
    Second,
};

// 一文窗口尺寸模式
enum class ArticleSizeMode {
    FixedWidthAutoHeight = 0, // 宽固定、高度随文本
    AutoFit = 1,              // 宽高都随文本计算
    Custom = 2,               // 手动指定宽高
};

struct Config {
    std::wstring title;             // 倒计时标题
    std::tm      targetTime{};      // 目标时间
    utils::DisplayFormat displayFormat = utils::DisplayFormat::HMS;
    int          fontSize = 36;    // 字号（96 DPI 下的逻辑像素）
    std::wstring font;             // 字体名
    COLORREF     backgroundColor = RGB(20, 20, 30);  // 窗口背景色
    COLORREF     textColor = RGB(235, 235, 245);      // 文字颜色
    bool         includeStartDay = false; // 起始日是否计入倒计时（时间 +1 天）
    CycleUnit    cycle = CycleUnit::None; // 循环单位（None=不循环）

    HitokotoSource hitokotoSource = HitokotoSource::Both; // 一言来源
    // 网络一言句子类型代码（对应 hitokoto.cn 的 c 参数；空 = 全部）
    std::string hitokotoCategory;
    int hitokotoRefreshSec = 60;                          // 一言刷新间隔（秒）

    // 一文栏目（右侧长文卡片）
    bool         articleEnabled = true;   // 是否显示一文栏目
    WindowAnchor articleAnchor = WindowAnchor::CenterRight; // 一文位置模板
    int          articlePlaceX = -24;    // 一文水平偏移（默认离右边缘留白）
    int          articlePlaceY = 0;      // 一文垂直偏移
    int          articleRefreshSec = 300; // 一文刷新间隔（秒）
    bool         articleIsTop = false;   // 一文是否始终置顶（独立于倒计时）
    int          articleFontSize = 15;   // 一文正文字号（96 DPI 逻辑像素）
    ArticleSizeMode articleSizeMode = ArticleSizeMode::FixedWidthAutoHeight;
    int          articleWidth = 380;     // 一文宽度（固定宽/自定义模式）
    int          articleHeight = 480;    // 一文高度（自定义模式）

    bool         isTop    = false;   // 窗口是否始终置顶（true=压过全屏/置顶应用）
    WindowAnchor anchor   = WindowAnchor::TopCenter; // 窗口位置模板（默认顶部居中）
    int          placeX   = 0;       // 相对模板位置的水平偏移（像素，可为负）
    int          placeY   = 0;       // 相对模板位置的垂直偏移（像素，可为负）
    bool         autoStart = false;  // 是否开机自启动（HKCU Run）

    // 返回默认配置（目标 = 2000-06-07，按年循环作为周年倒计时）
    static Config Defaults();

    // 从 exe 同目录 config.json 加载；失败则写入默认配置
    static Config Load();

    // 保存到 exe 同目录 config.json
    bool Save() const;

    // 当前生效的目标时间：循环开启且原目标已过时，按单位顺延到未来
    std::tm EffectiveTargetTime() const;
};
