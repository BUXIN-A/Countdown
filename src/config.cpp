// =============================================================================
// config.cpp - 分组配置读写实现（schemaVersion = 2）
//
// 旧版扁平配置不做迁移：版本号不符时直接写入默认配置并覆盖旧文件。
// =============================================================================
#include "config.h"
#include "hitokoto_category.h"
#include "json.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <ctime>
#include <algorithm>
#include <utility>

namespace {

const int kSchemaVersion = 2;

using J = mini_json::Value;

// ---------------------------------------------------------------------------
// 枚举 <-> 字符串
// ---------------------------------------------------------------------------
const char* AnchorToStr(WindowAnchor a) {
    switch (a) {
        case WindowAnchor::TopLeft:      return "top-left";
        case WindowAnchor::TopCenter:    return "top-center";
        case WindowAnchor::TopRight:     return "top-right";
        case WindowAnchor::CenterLeft:   return "center-left";
        case WindowAnchor::Center:       return "center";
        case WindowAnchor::CenterRight:  return "center-right";
        case WindowAnchor::BottomLeft:   return "bottom-left";
        case WindowAnchor::BottomCenter: return "bottom-center";
        case WindowAnchor::BottomRight:  return "bottom-right";
    }
    return "top-center";
}

WindowAnchor StrToAnchor(const std::string& s) {
    if (s == "top-left")      return WindowAnchor::TopLeft;
    if (s == "top-right")     return WindowAnchor::TopRight;
    if (s == "center-left")   return WindowAnchor::CenterLeft;
    if (s == "center")        return WindowAnchor::Center;
    if (s == "center-right")  return WindowAnchor::CenterRight;
    if (s == "bottom-left")   return WindowAnchor::BottomLeft;
    if (s == "bottom-center") return WindowAnchor::BottomCenter;
    if (s == "bottom-right")  return WindowAnchor::BottomRight;
    return WindowAnchor::TopCenter;
}

const char* CycleToStr(CycleUnit c) {
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

CycleUnit StrToCycle(const std::string& s) {
    if (s == "year")   return CycleUnit::Year;
    if (s == "month")  return CycleUnit::Month;
    if (s == "day")    return CycleUnit::Day;
    if (s == "hour")   return CycleUnit::Hour;
    if (s == "minute") return CycleUnit::Minute;
    if (s == "second") return CycleUnit::Second;
    return CycleUnit::None;
}

const char* ArticleModeToStr(ArticleSizeMode m) {
    switch (m) {
        case ArticleSizeMode::AutoFit: return "auto-fit";
        case ArticleSizeMode::Custom:  return "custom";
        default:                       return "auto-height";
    }
}

ArticleSizeMode StrToArticleMode(const std::string& s) {
    if (s == "auto-fit") return ArticleSizeMode::AutoFit;
    if (s == "custom")   return ArticleSizeMode::Custom;
    return ArticleSizeMode::FixedWidthAutoHeight;
}

const char* TempCleanupToStr(TempCleanupMode m) {
    return m == TempCleanupMode::Size ? "size" : "count";
}

TempCleanupMode StrToTempCleanup(const std::string& s) {
    return s == "size" ? TempCleanupMode::Size : TempCleanupMode::Count;
}

const char* TempTrimToStr(TempTrimMode m) {
    return m == TempTrimMode::DropOldest ? "oldest" : "clear";
}

TempTrimMode StrToTempTrim(const std::string& s) {
    return s == "oldest" ? TempTrimMode::DropOldest : TempTrimMode::ClearAll;
}

const char* TempUnitToStr(TempSizeUnit u) {
    return u == TempSizeUnit::GB ? "GB" : "MB";
}

TempSizeUnit StrToTempUnit(const std::string& s) {
    return s == "GB" ? TempSizeUnit::GB : TempSizeUnit::MB;
}

// ---------------------------------------------------------------------------
// JSON 辅助
// ---------------------------------------------------------------------------
const J* SubObject(const J& root, const char* key) {
    const J* v = root.find(key);
    return (v && v->is_object()) ? v : nullptr;
}

int IntValue(const J& obj, const char* key, int fallback) {
    const J* v = obj.find(key);
    return v ? static_cast<int>(v->as_number(static_cast<double>(fallback)))
             : fallback;
}

bool BoolValue(const J& obj, const char* key, bool fallback) {
    const J* v = obj.find(key);
    return v ? v->as_bool(fallback) : fallback;
}

void LoadColor(const J& obj, const char* key, COLORREF& out) {
    const J* v = obj.find(key);
    if (!v || !v->is_string()) return;
    COLORREF color = 0;
    if (utils::ParseHexColor(v->as_string(), color)) out = color;
}

std::wstring ConfigPath() {
    return utils::GetExeDir() + L"config.json";
}

// 读取整个文件为 UTF-8 字符串；失败返回空
bool ReadAll(const std::wstring& path, std::string& out) {
    std::ifstream ifs(path.c_str(), std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    // 容错 UTF-8 BOM
    if (out.size() >= 3 &&
        static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB &&
        static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return true;
}

// 先写临时文件再原子替换，避免中途中断写坏配置
bool WriteAll(const std::wstring& path, const std::string& content) {
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
    }
    return MoveFileExW(tmp.c_str(), path.c_str(),
                       MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}

// ---------------------------------------------------------------------------
// 循环顺延辅助
// ---------------------------------------------------------------------------
std::time_t TmToTime(const std::tm& t) {
    std::tm v = t;
    v.tm_isdst = -1;
    return std::mktime(&v);
}

std::tm TimeToTm(std::time_t t) {
    std::tm v{};
    localtime_s(&v, &t);
    return v;
}

} // namespace

// ---------------------------------------------------------------------------
Config Config::Defaults() {
    Config c;
    c.title = L"\u5012\u8ba1\u65f6"; // "倒计时"
    c.displayFormat = utils::DisplayFormat::HMS;
    c.fontSize = 36;
    c.cycle = CycleUnit::Year;

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

    std::wstring defFont = utils::GetSystemDefaultFontName();
    c.font = defFont;
    c.hitokotoFont = defFont;
    c.articleFont = defFont;
    c.backgroundColor = RGB(20, 20, 30);
    c.textColor = RGB(235, 235, 245);
    c.articleBackgroundColor = RGB(20, 20, 30);
    c.articleTextColor = RGB(235, 235, 245);

    c.articleModule = L"normal";
    return c;
}

// ---------------------------------------------------------------------------
Config Config::Load() {
    std::string text;
    if (!ReadAll(ConfigPath(), text)) {
        Config def = Defaults();
        def.Save();
        return def;
    }

    Config def = Defaults();
    try {
        J root = J::parse(text);
        if (!root.is_object()) {
            def.Save();
            return def;
        }
        const J* version = root.find("schemaVersion");
        if (!version ||
            static_cast<int>(version->as_number(-1)) != kSchemaVersion) {
            // 旧版扁平配置 / 未知版本：按用户选择重置为默认
            def.Save();
            return def;
        }

        Config c = def;

        if (const J* basic = SubObject(root, "basic")) {
            if (const J* v = basic->find("title")) {
                c.title = utils::Utf8ToWide(v->as_string());
            }
            if (const J* v = basic->find("targetTime")) {
                std::tm t{};
                if (utils::ParseTargetTime(utils::Utf8ToWide(v->as_string()), t)) {
                    c.targetTime = t;
                }
            }
            if (const J* v = basic->find("cycle")) {
                c.cycle = StrToCycle(v->as_string());
            }
            c.includeStartDay = BoolValue(*basic, "includeStartDay", c.includeStartDay);
            c.autoStart = BoolValue(*basic, "autoStart", c.autoStart);

            if (const J* win = SubObject(*basic, "window")) {
                if (const J* v = win->find("anchor")) {
                    c.anchor = StrToAnchor(v->as_string());
                }
                int x = IntValue(*win, "offsetX", c.placeX);
                int y = IntValue(*win, "offsetY", c.placeY);
                if (x >= -10000 && x <= 10000) c.placeX = x;
                if (y >= -10000 && y <= 10000) c.placeY = y;
                c.isTop = BoolValue(*win, "topMost", c.isTop);
            }

            if (const J* temp = SubObject(*basic, "temp")) {
                if (const J* v = temp->find("cleanupMode")) {
                    c.tempCleanupMode = StrToTempCleanup(v->as_string());
                }
                if (const J* v = temp->find("sizeUnit")) {
                    c.tempSizeUnit = StrToTempUnit(v->as_string());
                }
                if (const J* v = temp->find("trimMode")) {
                    c.tempTrimMode = StrToTempTrim(v->as_string());
                }
                c.tempArticleLimit = std::max(1, std::min(10000000,
                    IntValue(*temp, "articleLimit", c.tempArticleLimit)));
                c.tempHitokotoLimit = std::max(1, std::min(10000000,
                    IntValue(*temp, "hitokotoLimit", c.tempHitokotoLimit)));
                c.tempTrimCount = std::max(1, std::min(100000,
                    IntValue(*temp, "trimCount", c.tempTrimCount)));
            }
        }

        if (const J* cd = SubObject(root, "countdown")) {
            if (const J* v = cd->find("displayFormat")) {
                std::string f = v->as_string();
                if (f == "YYYY-MM-DD HH:MM:SS") {
                    c.displayFormat = utils::DisplayFormat::Full;
                } else if (f == "DDDD HH:MM:SS") {
                    c.displayFormat = utils::DisplayFormat::Days;
                } else {
                    c.displayFormat = utils::DisplayFormat::HMS;
                }
            }
            int fs = IntValue(*cd, "fontSize", c.fontSize);
            if (fs >= 8 && fs <= 200) c.fontSize = fs;
            if (const J* v = cd->find("font")) {
                std::wstring f = utils::Utf8ToWide(v->as_string());
                if (!f.empty()) c.font = f;
            }
            LoadColor(*cd, "backgroundColor", c.backgroundColor);
            LoadColor(*cd, "textColor", c.textColor);

            int mode = IntValue(*cd, "sizeMode",
                                static_cast<int>(c.countdownSizeMode));
            if (mode < 0 || mode > 2) mode = 1;
            c.countdownSizeMode = static_cast<CountdownSizeMode>(mode);
            int cw = IntValue(*cd, "width", c.countdownWidth);
            if (cw >= 200 && cw <= 4000) c.countdownWidth = cw;
            int ch = IntValue(*cd, "height", c.countdownHeight);
            if (ch >= 60 && ch <= 2000) c.countdownHeight = ch;
            if (const J* v = cd->find("radius")) {
                int r = static_cast<int>(v->as_number(-1));
                if (r >= -1 && r <= 400) c.countdownRadius = r;
            }
        }

        if (const J* hk = SubObject(root, "hitokoto")) {
            if (const J* v = hk->find("source")) {
                std::string s = v->as_string();
                if (s == "local")        c.hitokotoSource = HitokotoSource::Local;
                else if (s == "network") c.hitokotoSource = HitokotoSource::Network;
                else if (s == "both")    c.hitokotoSource = HitokotoSource::Both;
            }
            if (const J* v = hk->find("category")) {
                std::string cat = v->as_string();
                if (hitokoto_category::IsValidCode(cat)) c.hitokotoCategory = cat;
            }
            int sec = IntValue(*hk, "refreshSec", c.hitokotoRefreshSec);
            if (sec >= 5 && sec <= 3600) c.hitokotoRefreshSec = sec;
            if (const J* v = hk->find("font")) {
                std::wstring f = utils::Utf8ToWide(v->as_string());
                if (!f.empty()) c.hitokotoFont = f;
            }
            c.hitokotoBold = BoolValue(*hk, "bold", c.hitokotoBold);
        }

        if (const J* art = SubObject(root, "article")) {
            c.articleEnabled = BoolValue(*art, "enabled", c.articleEnabled);
            // refreshSec 已迁移到各模块自己的配置（normal 模块声明）；
            // 这里读取旧值，稍后写入 normal 模块配置，避免升级后行为突变
            int legacyRefreshSec = 0;
            if (const J* v = art->find("refreshSec")) {
                int sec = static_cast<int>(v->as_number(0));
                if (sec > 0 && sec <= 86400) legacyRefreshSec = sec;
            }
            int fs = IntValue(*art, "fontSize", c.articleFontSize);
            if (fs >= 8 && fs <= 96) c.articleFontSize = fs;
            if (const J* v = art->find("font")) {
                std::wstring f = utils::Utf8ToWide(v->as_string());
                if (!f.empty()) c.articleFont = f;
            }
            LoadColor(*art, "backgroundColor", c.articleBackgroundColor);
            LoadColor(*art, "textColor", c.articleTextColor);
            if (const J* v = art->find("module")) {
                std::wstring id = utils::Utf8ToWide(v->as_string());
                if (!id.empty()) c.articleModule = id;
            }

            if (const J* layout = SubObject(*art, "layout")) {
                if (const J* v = layout->find("anchor")) {
                    c.articleAnchor = StrToAnchor(v->as_string());
                }
                int x = IntValue(*layout, "offsetX", c.articlePlaceX);
                int y = IntValue(*layout, "offsetY", c.articlePlaceY);
                if (x >= -10000 && x <= 10000) c.articlePlaceX = x;
                if (y >= -10000 && y <= 10000) c.articlePlaceY = y;
                if (const J* v = layout->find("sizeMode")) {
                    c.articleSizeMode = StrToArticleMode(v->as_string());
                }
                int w = IntValue(*layout, "width", c.articleWidth);
                int h = IntValue(*layout, "height", c.articleHeight);
                if (w >= 180 && w <= 1200) c.articleWidth = w;
                if (h >= 120 && h <= 2000) c.articleHeight = h;
                c.articleIsTop = BoolValue(*layout, "topMost", c.articleIsTop);
            }

            if (const J* mods = SubObject(*art, "modules")) {
                for (const auto& kv : mods->as_object()) {
                    if (kv.second.is_object()) {
                        c.articleModuleConfigs[kv.first] =
                            J::serialize(kv.second, false);
                    }
                }
            }

            if (legacyRefreshSec > 0) {
                auto it = c.articleModuleConfigs.find("normal");
                std::string normalJson =
                    (it == c.articleModuleConfigs.end()) ? "{}" : it->second;
                try {
                    J module = J::parse(normalJson);
                    if (module.is_object() && !module.find("refreshSec")) {
                        mini_json::Object obj = module.as_object();
                        obj.emplace_back("refreshSec", J(legacyRefreshSec));
                        c.articleModuleConfigs["normal"] =
                            J::serialize(J(std::move(obj)), false);
                    }
                } catch (...) {
                    // 旧配置无法解析时忽略迁移，模块会使用自身默认值
                }
            }
        }

        if (c.title.empty()) c.title = def.title;
        return c;
    } catch (...) {
        def.Save();
        return def;
    }
}

// ---------------------------------------------------------------------------
bool Config::Save() const {
    mini_json::Object root;
    root.emplace_back("schemaVersion", J(kSchemaVersion));

    {
        mini_json::Object basic;
        basic.emplace_back("title", J(utils::WideToUtf8(title)));
        basic.emplace_back("targetTime",
            J(utils::WideToUtf8(utils::FormatTargetTime(targetTime))));
        basic.emplace_back("cycle", J(CycleToStr(cycle)));
        basic.emplace_back("includeStartDay", J(includeStartDay));
        basic.emplace_back("autoStart", J(autoStart));

        mini_json::Object win;
        win.emplace_back("anchor", J(AnchorToStr(anchor)));
        win.emplace_back("offsetX", J(placeX));
        win.emplace_back("offsetY", J(placeY));
        win.emplace_back("topMost", J(isTop));
        basic.emplace_back("window", J(std::move(win)));

        mini_json::Object temp;
        temp.emplace_back("cleanupMode", J(TempCleanupToStr(tempCleanupMode)));
        temp.emplace_back("sizeUnit", J(TempUnitToStr(tempSizeUnit)));
        temp.emplace_back("articleLimit", J(tempArticleLimit));
        temp.emplace_back("hitokotoLimit", J(tempHitokotoLimit));
        temp.emplace_back("trimMode", J(TempTrimToStr(tempTrimMode)));
        temp.emplace_back("trimCount", J(tempTrimCount));
        basic.emplace_back("temp", J(std::move(temp)));

        root.emplace_back("basic", J(std::move(basic)));
    }

    {
        mini_json::Object cd;
        cd.emplace_back("displayFormat",
            J(displayFormat == utils::DisplayFormat::Full
                  ? "YYYY-MM-DD HH:MM:SS"
                  : displayFormat == utils::DisplayFormat::Days
                        ? "DDDD HH:MM:SS" : "HH:MM:SS"));
        cd.emplace_back("font", J(utils::WideToUtf8(font)));
        cd.emplace_back("fontSize", J(fontSize));
        cd.emplace_back("backgroundColor", J(utils::ColorToHex(backgroundColor)));
        cd.emplace_back("textColor", J(utils::ColorToHex(textColor)));
        cd.emplace_back("sizeMode", J(static_cast<int>(countdownSizeMode)));
        cd.emplace_back("width", J(countdownWidth));
        cd.emplace_back("height", J(countdownHeight));
        cd.emplace_back("radius", J(countdownRadius));
        root.emplace_back("countdown", J(std::move(cd)));
    }

    {
        const char* src =
            hitokotoSource == HitokotoSource::Local  ? "local" :
            hitokotoSource == HitokotoSource::Network ? "network" : "both";
        mini_json::Object hk;
        hk.emplace_back("source", J(src));
        hk.emplace_back("category", J(hitokotoCategory));
        hk.emplace_back("refreshSec", J(hitokotoRefreshSec));
        hk.emplace_back("font", J(utils::WideToUtf8(hitokotoFont)));
        hk.emplace_back("bold", J(hitokotoBold));
        root.emplace_back("hitokoto", J(std::move(hk)));
    }

    {
        mini_json::Object art;
        art.emplace_back("enabled", J(articleEnabled));
        art.emplace_back("fontSize", J(articleFontSize));
        art.emplace_back("font", J(utils::WideToUtf8(articleFont)));
        art.emplace_back("backgroundColor", J(utils::ColorToHex(articleBackgroundColor)));
        art.emplace_back("textColor", J(utils::ColorToHex(articleTextColor)));
        art.emplace_back("module", J(utils::WideToUtf8(articleModule)));

        mini_json::Object layout;
        layout.emplace_back("anchor", J(AnchorToStr(articleAnchor)));
        layout.emplace_back("offsetX", J(articlePlaceX));
        layout.emplace_back("offsetY", J(articlePlaceY));
        layout.emplace_back("sizeMode", J(ArticleModeToStr(articleSizeMode)));
        layout.emplace_back("width", J(articleWidth));
        layout.emplace_back("height", J(articleHeight));
        layout.emplace_back("topMost", J(articleIsTop));
        art.emplace_back("layout", J(std::move(layout)));

        mini_json::Object modules;
        for (const auto& kv : articleModuleConfigs) {
            try {
                J value = J::parse(kv.second);
                if (value.is_object()) modules.emplace_back(kv.first, std::move(value));
            } catch (...) {
                // 非法模块配置不写入，避免污染配置文件
            }
        }
        art.emplace_back("modules", J(std::move(modules)));

        root.emplace_back("article", J(std::move(art)));
    }

    std::string json = J::serialize(J(std::move(root)), /*pretty=*/true);
    return WriteAll(ConfigPath(), json);
}

// ---------------------------------------------------------------------------
std::tm Config::EffectiveTargetTime() const {
    std::time_t now = std::time(nullptr);
    std::time_t target = TmToTime(targetTime);
    if (cycle == CycleUnit::None || target > now) return targetTime;

    switch (cycle) {
        case CycleUnit::Second:
        case CycleUnit::Minute:
        case CycleUnit::Hour: {
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
            std::tm t = targetTime;
            t.tm_isdst = -1;
            long long diff = static_cast<long long>(now)
                           - static_cast<long long>(target);
            long long days = diff / 86400 + 1;
            if (days < 1) days = 1;
            if (days > 100000) days = 100000;
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
            std::tm t = targetTime;
            t.tm_isdst = -1;
            std::time_t next = std::mktime(&t);
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

// ---------------------------------------------------------------------------
std::string Config::ArticleModuleConfig(const std::wstring& moduleId) const {
    std::string key = utils::WideToUtf8(moduleId);
    auto it = articleModuleConfigs.find(key);
    if (it == articleModuleConfigs.end()) return "{}";
    return it->second;
}

void Config::SetArticleModuleConfig(const std::wstring& moduleId,
                                    const std::string& json) {
    try {
        J value = J::parse(json);
        if (!value.is_object()) return;
        articleModuleConfigs[utils::WideToUtf8(moduleId)] =
            J::serialize(value, false);
    } catch (...) {
        // 非法 JSON 直接忽略
    }
}

// ---------------------------------------------------------------------------
static long long UnitToBytes(TempSizeUnit unit, int value) {
    long long v = std::max(1, value);
    long long multiplier = unit == TempSizeUnit::GB
                               ? 1024LL * 1024LL * 1024LL
                               : 1024LL * 1024LL;
    if (v > 10000000LL) v = 10000000LL;
    return v * multiplier;
}

long long Config::TempArticleLimitBytes() const {
    return UnitToBytes(tempSizeUnit, tempArticleLimit);
}

long long Config::TempHitokotoLimitBytes() const {
    return UnitToBytes(tempSizeUnit, tempHitokotoLimit);
}
