// =============================================================================
// config.cpp - 配置读写实现
// =============================================================================
#include "config.h"
#include "json.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <ctime>

// config.json 字段名
static const char* kTitle    = "title";
static const char* kTarget   = "targetTime";
static const char* kFormat   = "displayFormat";
static const char* kFontSize = "fontSize";
static const char* kFont     = "font";
static const char* kIncludeStart = "includeStartDay";
static const char* kCycle    = "cycle";
static const char* kHkSource = "hitokotoSource";
static const char* kHkRefresh = "hitokotoRefreshSec";
static const char* kArticleEnabled = "articleEnabled";
static const char* kArticleAnchor  = "articleAnchor";
static const char* kArticleX       = "articlePlaceX";
static const char* kArticleY       = "articlePlaceY";
static const char* kArticleRefresh = "articleRefreshSec";
static const char* kArticleTop     = "articleIsTop";
static const char* kArticleFont    = "articleFontSize";
static const char* kArticleSizeMode = "articleSizeMode";
static const char* kArticleWidth   = "articleWidth";
static const char* kArticleHeight  = "articleHeight";
static const char* kIsTop    = "isTop";
static const char* kAnchor   = "anchor";
static const char* kPlaceX   = "place_x";
static const char* kPlaceY   = "place_y";
static const char* kAutoStart = "autoStart";

// 锚点 <-> 字符串
static const char* AnchorToStr(WindowAnchor a) {
    switch (a) {
        case WindowAnchor::TopLeft:     return "top-left";
        case WindowAnchor::TopCenter:   return "top-center";
        case WindowAnchor::TopRight:    return "top-right";
        case WindowAnchor::CenterLeft:  return "center-left";
        case WindowAnchor::Center:      return "center";
        case WindowAnchor::CenterRight: return "center-right";
        case WindowAnchor::BottomLeft:  return "bottom-left";
        case WindowAnchor::BottomCenter: return "bottom-center";
        case WindowAnchor::BottomRight: return "bottom-right";
    }
    return "top-center";
}

static WindowAnchor StrToAnchor(const std::string& s) {
    if (s == "top-left")     return WindowAnchor::TopLeft;
    if (s == "top-right")    return WindowAnchor::TopRight;
    if (s == "center-left")  return WindowAnchor::CenterLeft;
    if (s == "center")       return WindowAnchor::Center;
    if (s == "center-right") return WindowAnchor::CenterRight;
    if (s == "bottom-left")  return WindowAnchor::BottomLeft;
    if (s == "bottom-center") return WindowAnchor::BottomCenter;
    if (s == "bottom-right") return WindowAnchor::BottomRight;
    return WindowAnchor::TopCenter; // 默认顶部居中（含 top-center）
}

// 循环单位 <-> 字符串
static const char* CycleToStr(CycleUnit c) {
    switch (c) {
        case CycleUnit::Year:   return "year";
        case CycleUnit::Month:  return "month";
        case CycleUnit::Day:    return "day";
        case CycleUnit::Hour:   return "hour";
        case CycleUnit::Minute: return "minute";
        case CycleUnit::Second: return "second";
        default:                return "none";
    }
}

static CycleUnit StrToCycle(const std::string& s) {
    if (s == "year")   return CycleUnit::Year;
    if (s == "month")  return CycleUnit::Month;
    if (s == "day")    return CycleUnit::Day;
    if (s == "hour")   return CycleUnit::Hour;
    if (s == "minute") return CycleUnit::Minute;
    if (s == "second") return CycleUnit::Second;
    return CycleUnit::None;
}

// 一文尺寸模式 <-> 字符串
static const char* ArticleModeToStr(ArticleSizeMode m) {
    switch (m) {
        case ArticleSizeMode::AutoFit: return "auto-fit";
        case ArticleSizeMode::Custom:  return "custom";
        default:                       return "auto-height";
    }
}

static ArticleSizeMode StrToArticleMode(const std::string& s) {
    if (s == "auto-fit") return ArticleSizeMode::AutoFit;
    if (s == "custom")   return ArticleSizeMode::Custom;
    return ArticleSizeMode::FixedWidthAutoHeight;
}

static std::wstring ConfigPath() {
    return utils::GetExeDir() + L"config.json";
}

// 读取整个文件为 UTF-8 字符串；失败返回空
static bool ReadAll(const std::wstring& path, std::string& out) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    // 容错 UTF-8 BOM（记事本等编辑器可能写入），否则 JSON 解析会失败
    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

// 写入文件：先写临时文件再原子替换，避免中途崩溃/断电导致 config.json
// 损坏为半截内容（旧版本先 trunc 后写，一旦中断配置即丢失）
static bool WriteAll(const std::wstring& path, const std::string& content) {
    std::wstring tmp = path + L".tmp";
    {
        std::ofstream ofs(tmp.c_str(), std::ios::binary | std::ios::trunc);
        if (!ofs) return false;
        ofs.write(content.data(), static_cast<std::streamsize>(content.size()));
        ofs.flush();
        if (!ofs) {
            ofs.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    } // ofs 在此析构关闭文件
    // 原子替换原文件（MOVEFILE_REPLACE_EXISTING 覆盖，WRITE_THROUGH 刷盘）
    return MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---------------------------------------------------------------------------
Config Config::Defaults() {
    Config c;
    c.title = L"\u5012\u8ba1\u65f6"; // "倒计时"
    c.displayFormat = utils::DisplayFormat::HMS;
    c.fontSize = 36;
    c.font = utils::GetSystemDefaultFontName();

    // 默认目标时间：2000-06-07 00:00:00
    std::tm target{};
    target.tm_year = 2000 - 1900;
    target.tm_mon  = 6 - 1;
    target.tm_mday = 7;
    target.tm_hour = 0;
    target.tm_min  = 0;
    target.tm_sec  = 0;
    target.tm_isdst = 0;
    c.targetTime = target;
    return c;
}

// ---------------------------------------------------------------------------
Config Config::Load() {
    std::string text;
    if (!ReadAll(ConfigPath(), text)) {
        // 文件不存在：写入默认配置
        Config def = Defaults();
        def.Save();
        return def;
    }

    Config c = Defaults(); // 以默认值兜底

    try {
        mini_json::Value root = mini_json::Value::parse(text);
        if (!root.is_object()) return c;

        if (auto* v = root.find(kTitle)) {
            c.title = utils::Utf8ToWide(v->as_string());
        }
        if (auto* v = root.find(kTarget)) {
            std::tm t{};
            if (utils::ParseTargetTime(utils::Utf8ToWide(v->as_string()), t)) {
                c.targetTime = t;
            }
        }
        if (auto* v = root.find(kFormat)) {
            std::string f = v->as_string();
            if (f == "YYYY-MM-DD HH:MM:SS") {
                c.displayFormat = utils::DisplayFormat::Full;
            } else if (f == "DDDD HH:MM:SS") {
                c.displayFormat = utils::DisplayFormat::Days;
            } else {
                c.displayFormat = utils::DisplayFormat::HMS;
            }
        }
        if (auto* v = root.find(kFontSize)) {
            int fs = static_cast<int>(v->as_number(-1));
            if (fs >= 8 && fs <= 200) c.fontSize = fs;
        }
        if (auto* v = root.find(kFont)) {
            std::wstring f = utils::Utf8ToWide(v->as_string());
            if (!f.empty()) c.font = f;
        }
        if (auto* v = root.find(kIncludeStart)) {
            c.includeStartDay = v->as_bool();
        }
        if (auto* v = root.find(kCycle)) {
            c.cycle = StrToCycle(v->as_string());
        }
        if (auto* v = root.find(kHkSource)) {
            std::string s = v->as_string();
            if (s == "local")      c.hitokotoSource = HitokotoSource::Local;
            else if (s == "network") c.hitokotoSource = HitokotoSource::Network;
            else if (s == "both")  c.hitokotoSource = HitokotoSource::Both;
        }
        if (auto* v = root.find(kHkRefresh)) {
            int sec = static_cast<int>(v->as_number(-1));
            if (sec >= 5 && sec <= 3600) c.hitokotoRefreshSec = sec;
        }
        if (auto* v = root.find(kArticleEnabled)) {
            c.articleEnabled = v->as_bool();
        }
        if (auto* v = root.find(kArticleAnchor)) {
            c.articleAnchor = StrToAnchor(v->as_string());
        }
        if (auto* v = root.find(kArticleX)) {
            int px = static_cast<int>(v->as_number(0));
            if (px >= -10000 && px <= 10000) c.articlePlaceX = px;
        }
        if (auto* v = root.find(kArticleY)) {
            int py = static_cast<int>(v->as_number(0));
            if (py >= -10000 && py <= 10000) c.articlePlaceY = py;
        }
        if (auto* v = root.find(kArticleRefresh)) {
            int sec = static_cast<int>(v->as_number(-1));
            if (sec >= 1 && sec <= 86399) c.articleRefreshSec = sec;
        }
        if (auto* v = root.find(kArticleTop)) {
            c.articleIsTop = v->as_bool();
        }
        if (auto* v = root.find(kArticleFont)) {
            int fs = static_cast<int>(v->as_number(-1));
            if (fs >= 8 && fs <= 96) c.articleFontSize = fs;
        }
        if (auto* v = root.find(kArticleSizeMode)) {
            c.articleSizeMode = StrToArticleMode(v->as_string());
        }
        if (auto* v = root.find(kArticleWidth)) {
            int w = static_cast<int>(v->as_number(-1));
            if (w >= 180 && w <= 1200) c.articleWidth = w;
        }
        if (auto* v = root.find(kArticleHeight)) {
            int h = static_cast<int>(v->as_number(-1));
            if (h >= 120 && h <= 2000) c.articleHeight = h;
        }
        if (auto* v = root.find(kIsTop)) {
            c.isTop = v->as_bool();
        }
        if (auto* v = root.find(kAnchor)) {
            c.anchor = StrToAnchor(v->as_string());
        }
        if (auto* v = root.find(kPlaceX)) {
            int px = static_cast<int>(v->as_number(0));
            if (px >= -10000 && px <= 10000) c.placeX = px;
        }
        if (auto* v = root.find(kPlaceY)) {
            int py = static_cast<int>(v->as_number(0));
            if (py >= -10000 && py <= 10000) c.placeY = py;
        }
        if (auto* v = root.find(kAutoStart)) {
            c.autoStart = v->as_bool();
        }
    } catch (...) {
        // 解析失败，返回默认配置
    }
    return c;
}

// ---------------------------------------------------------------------------
bool Config::Save() const {
    using J = mini_json::Value;
    mini_json::Object obj;

    obj.emplace_back(kTitle,    J(utils::WideToUtf8(title)));
    obj.emplace_back(kTarget,   J(utils::WideToUtf8(utils::FormatTargetTime(targetTime))));
    obj.emplace_back(kFormat,
        J(displayFormat == utils::DisplayFormat::Full
              ? "YYYY-MM-DD HH:MM:SS"
              : displayFormat == utils::DisplayFormat::Days
                    ? "DDDD HH:MM:SS" : "HH:MM:SS"));
    obj.emplace_back(kFontSize, J(fontSize));
    obj.emplace_back(kFont,     J(utils::WideToUtf8(font)));
    obj.emplace_back(kIncludeStart, J(includeStartDay));
    obj.emplace_back(kCycle,    J(CycleToStr(cycle)));

    const char* src =
        hitokotoSource == HitokotoSource::Local  ? "local" :
        hitokotoSource == HitokotoSource::Network ? "network" : "both";
    obj.emplace_back(kHkSource,  J(src));
    obj.emplace_back(kHkRefresh, J(hitokotoRefreshSec));
    obj.emplace_back(kArticleEnabled, J(articleEnabled));
    obj.emplace_back(kArticleAnchor,  J(AnchorToStr(articleAnchor)));
    obj.emplace_back(kArticleX,       J(articlePlaceX));
    obj.emplace_back(kArticleY,       J(articlePlaceY));
    obj.emplace_back(kArticleRefresh, J(articleRefreshSec));
    obj.emplace_back(kArticleTop,     J(articleIsTop));
    obj.emplace_back(kArticleFont,    J(articleFontSize));
    obj.emplace_back(kArticleSizeMode, J(ArticleModeToStr(articleSizeMode)));
    obj.emplace_back(kArticleWidth,   J(articleWidth));
    obj.emplace_back(kArticleHeight,  J(articleHeight));
    obj.emplace_back(kIsTop,    J(isTop));
    obj.emplace_back(kAnchor,   J(AnchorToStr(anchor)));
    obj.emplace_back(kPlaceX,   J(placeX));
    obj.emplace_back(kPlaceY,   J(placeY));
    obj.emplace_back(kAutoStart, J(autoStart));

    std::string json = mini_json::Value::serialize(J(std::move(obj)), /*pretty=*/true);
    return WriteAll(ConfigPath(), json);
}

// ---------------------------------------------------------------------------
// 循环顺延：把已过去的目标时间按单位推进到未来
// ---------------------------------------------------------------------------
static std::time_t TmToTime(const std::tm& t) {
    std::tm v = t;
    v.tm_isdst = -1;
    return std::mktime(&v);
}

static std::tm TimeToTm(std::time_t t) {
    std::tm v{};
    localtime_s(&v, &t);
    return v;
}

std::tm Config::EffectiveTargetTime() const {
    std::time_t now = std::time(nullptr);
    std::time_t target = TmToTime(targetTime);
    if (cycle == CycleUnit::None || target > now) return targetTime;

    switch (cycle) {
        case CycleUnit::Second:
        case CycleUnit::Minute:
        case CycleUnit::Hour: {
            // 固定时长单位：直接按整数倍跳到 now 之后
            long long step = cycle == CycleUnit::Second ? 1
                           : cycle == CycleUnit::Minute ? 60 : 3600;
            long long diff = static_cast<long long>(now)
                           - static_cast<long long>(target);
            long long times = diff / step + 1;
            std::time_t next = target + static_cast<std::time_t>(times * step);
            if (next <= now) next += static_cast<std::time_t>(step);
            return TimeToTm(next);
        }
        case CycleUnit::Day: {
            // 按日历日推进，保持本地钟点；先估算再校正 DST 偏差
            std::tm t = targetTime;
            t.tm_isdst = -1;
            long long diff = static_cast<long long>(now)
                           - static_cast<long long>(target);
            long long days = diff / 86400 + 1;
            if (days < 1) days = 1;
            if (days > 100000) days = 100000; // 防御异常目标时间
            t.tm_mday += static_cast<int>(days);
            std::time_t next = std::mktime(&t);
            for (int i = 0; next != (std::time_t)-1 && next <= now && i < 2; ++i) {
                t.tm_mday += 1;
                next = std::mktime(&t);
            }
            if (next == (std::time_t)-1 || next <= now) return targetTime;
            return t;
        }
        case CycleUnit::Month:
        case CycleUnit::Year: {
            // 按日历月/年推进（mktime 自动处理跨年、月末与闰年）
            std::tm t = targetTime;
            t.tm_isdst = -1;
            std::time_t next = std::mktime(&t);
            // 上限 2400 次（200 年），防止异常配置导致长循环
            for (int i = 0; next != (std::time_t)-1 && next <= now && i < 2400; ++i) {
                if (cycle == CycleUnit::Year) t.tm_year += 1;
                else                          t.tm_mon  += 1;
                next = std::mktime(&t);
            }
            if (next == (std::time_t)-1 || next <= now) return targetTime;
            return t;
        }
        default:
            return targetTime;
    }
}
