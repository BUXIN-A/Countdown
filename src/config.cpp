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
static const char* kHkSource = "hitokotoSource";
static const char* kHkRefresh = "hitokotoRefreshSec";
static const char* kIsTop    = "isTop";
static const char* kAnchor   = "anchor";
static const char* kPlaceX   = "place_x";
static const char* kPlaceY   = "place_y";

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
    c.fontSize = 48;
    c.font = utils::GetSystemDefaultFontName();

    std::time_t now = std::time(nullptr);
    std::time_t target = now + 7LL * 86400; // +7 天
    std::tm local{};
    localtime_s(&local, &target);
    c.targetTime = local;
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

    const char* src =
        hitokotoSource == HitokotoSource::Local  ? "local" :
        hitokotoSource == HitokotoSource::Network ? "network" : "both";
    obj.emplace_back(kHkSource,  J(src));
    obj.emplace_back(kHkRefresh, J(hitokotoRefreshSec));
    obj.emplace_back(kIsTop,    J(isTop));
    obj.emplace_back(kAnchor,   J(AnchorToStr(anchor)));
    obj.emplace_back(kPlaceX,   J(placeX));
    obj.emplace_back(kPlaceY,   J(placeY));

    std::string json = mini_json::Value::serialize(J(std::move(obj)), /*pretty=*/true);
    return WriteAll(ConfigPath(), json);
}
