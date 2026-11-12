// =============================================================================
// config.h - 分组配置结构（schemaVersion = 2）
//
// 配置文件按功能分组，便于阅读与后续扩展：
//   basic     基础设置（倒计时目标、窗口、启动、临时数据）
//   countdown 倒计时外观（格式、字体、颜色）
//   hitokoto  一言（来源、类型、刷新、字体、加粗）
//   article   一文（内容、外观、布局、显示模块与模块配置）
// =============================================================================
#pragma once
#include <string>
#include <map>
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

// 倒计时窗口尺寸模式
enum class CountdownSizeMode {
    AutoHeight = 0, // 宽度手动指定、高度随内容
    AutoFit    = 1, // 宽高都随文本/内容计算（默认，保持原有外观）
    Custom     = 2, // 宽高都手动指定
};

// 临时数据清理逻辑：按记录项数 / 按文件大小
enum class TempCleanupMode {
    Count = 0,
    Size  = 1,
};

// 达到上限后的处理方式
enum class TempTrimMode {
    ClearAll  = 0, // 清空全部记录
    DropOldest = 1, // 删除最早 N 项
};

// 大小单位
enum class TempSizeUnit {
    MB = 0,
    GB = 1,
};

struct Config {
    // ---- basic：倒计时目标 ----
    std::wstring title = L"";
    std::tm      targetTime{};
    CycleUnit    cycle = CycleUnit::Year;
    bool         includeStartDay = false;
    bool         autoStart = false;

    // ---- basic：主窗口布局 ----
    bool         isTop = false;
    WindowAnchor anchor = WindowAnchor::TopCenter;
    int          placeX = 0;
    int          placeY = 0;

    // ---- basic：临时数据清理 ----
    TempCleanupMode tempCleanupMode = TempCleanupMode::Count;
    TempSizeUnit    tempSizeUnit = TempSizeUnit::MB;
    int             tempArticleLimit = 5000;   // 项数，或 MB/GB 数值
    int             tempHitokotoLimit = 5000;  // 项数，或 MB/GB 数值
    TempTrimMode    tempTrimMode = TempTrimMode::ClearAll;
    int             tempTrimCount = 100;

    // ---- countdown：外观 ----
    utils::DisplayFormat displayFormat = utils::DisplayFormat::HMS;
    int          fontSize = 36;       // 字号（96 DPI 逻辑像素）
    std::wstring font = L"";          // 倒计时信息字体
    COLORREF     backgroundColor = RGB(20, 20, 30);  // 倒计时背景
    COLORREF     textColor = RGB(235, 235, 245);      // 倒计时文字

    // ---- countdown：窗口尺寸与圆角 ----
    CountdownSizeMode countdownSizeMode = CountdownSizeMode::AutoFit;
    int          countdownWidth = 560;   // 96 DPI 逻辑像素
    int          countdownHeight = 110;  // 96 DPI 逻辑像素
    // 圆角半径（96 DPI 逻辑像素）；-1 表示胶囊（自动取高度的一半）
    int          countdownRadius = -1;

    // ---- hitokoto ----
    HitokotoSource hitokotoSource = HitokotoSource::Both;
    std::string hitokotoCategory;     // 网络句子类型代码（空=全部）
    int          hitokotoRefreshSec = 60;
    std::wstring hitokotoFont = L"";  // 一言独立字体
    bool         hitokotoBold = false;

    // ---- article：内容 ----
    bool         articleEnabled = true;
    int          articleFontSize = 15;
    std::wstring articleFont = L"";
    COLORREF     articleBackgroundColor = RGB(20, 20, 30);
    COLORREF     articleTextColor = RGB(235, 235, 245);

    // ---- article：布局 ----
    WindowAnchor articleAnchor = WindowAnchor::CenterRight;
    int          articlePlaceX = -24;
    int          articlePlaceY = 0;
    ArticleSizeMode articleSizeMode = ArticleSizeMode::FixedWidthAutoHeight;
    int          articleWidth = 380;
    int          articleHeight = 480;
    bool         articleIsTop = false;

    // ---- article：显示模块 ----
    std::wstring articleModule = L"normal"; // 当前显示模块 id
    // 各模块配置（UTF-8 JSON 对象字符串），切换模块不会丢失已配置内容
    std::map<std::string, std::string> articleModuleConfigs;

    // 返回默认配置（目标 = 2000-06-07，按年循环作为周年倒计时）
    static Config Defaults();

    // 从 exe 同目录 config.json 加载；版本不符时写入默认配置
    static Config Load();

    // 保存到 exe 同目录 config.json
    bool Save() const;

    // 当前生效的目标时间：循环开启且原目标已过时，按单位顺延到未来
    std::tm EffectiveTargetTime() const;

    // 读取/写入某模块的配置 JSON（始终返回合法 JSON 对象）
    std::string ArticleModuleConfig(const std::wstring& moduleId) const;
    void SetArticleModuleConfig(const std::wstring& moduleId,
                                const std::string& json);

    // 临时上限换算为字节数（size 模式使用）
    long long TempArticleLimitBytes() const;
    long long TempHitokotoLimitBytes() const;
};
