// =============================================================================
// article_window.cpp - 一文栏目实现
// =============================================================================
#include "article_window.h"
#include "article_module_api.h"
#include "history.h"
#include "utils.h"

#include <windows.h>
#include <algorithm>
#include <ctime>
#include <cwctype>
#include <random>

// 默认一文（首次运行且 article 文件夹为空时写入 default.txt）
// 同一文件内可用单独一行 /*/ 分隔多篇文章。
static const wchar_t* kDefaultArticleText = LR"(《破土》
你见过竹子的生长吗？
前四年，它只露出地面三厘米。可它的根，已在黑暗中延伸了数百平方米。第五年，它疯狂生长，六周便能蹿到十五米。
有人以为那是奇迹，却不知那沉默的四年，它从未停止过扎根。
你现在的日子，或许正是那三厘米。背单词时似乎永远记不住的词汇，深夜里反复演算却仍会出错的习题，工作上那些重复又琐碎的积累……看起来毫无长进，其实每一寸努力都在向下延伸。
别急着看见结果。树根从不向天空炫耀深度，竹节也从不催促春天。
所有看起来的“突然逆袭”，都是“蓄谋已久”的厚积薄发。你此刻的每一次咬牙，都在为未来的拔节积蓄力量。
再坚持一下吧。地下的根扎够了，破土而出，就只是时间的事。
/*/
《慢一点，但别停》
学习和工作，都像一场长跑。你不必一开始就冲得最快，但要学会每天向前一点。今天多读十页书，多练一道题，多复盘一次工作，都是在为未来攒底气。
遇到难题，别急着否定自己，先把它拆小；遇到失败，别只顾沮丧，先记录原因。别等状态好了才行动，很多时候，是行动带来了状态。
和昨天的自己比，不和别人的高光比。把目标写下来，把任务做到底。你现在的坚持，也许暂时看不见结果，但会在某一天连成台阶，托你走向更高的地方。慢一点没关系，别停就好。)";

// 正文内边距（96 DPI 逻辑像素）：布局测量与绘制共用同一组值
static const int kArticlePadX = 22;
static const int kArticlePadY = 20;

static void TrimWhitespace(std::wstring& s) {
    size_t begin = 0;
    while (begin < s.size() && std::iswspace(s[begin])) ++begin;
    size_t end = s.size();
    while (end > begin && std::iswspace(s[end - 1])) --end;
    s = s.substr(begin, end - begin);
}

static void SplitArticles(const std::wstring& text,
                          std::vector<std::wstring>& out) {
    static const std::wstring kSep = L"/*/";
    size_t start = 0;
    while (true) {
        size_t pos = text.find(kSep, start);
        std::wstring part = (pos == std::wstring::npos)
            ? text.substr(start)
            : text.substr(start, pos - start);
        TrimWhitespace(part);
        if (!part.empty()) out.push_back(std::move(part));
        if (pos == std::wstring::npos) break;
        start = pos + kSep.size();
    }
}

// ---------------------------------------------------------------------------
// 模块宿主回调
// ---------------------------------------------------------------------------
namespace {

ArticleWindow* HostOf(void* host) {
    return static_cast<ArticleWindow*>(host);
}

int HostArticleCount(void* host) {
    return HostOf(host)->ModuleArticleCount();
}

const char* HostArticleKey(void* host, int index) {
    return HostOf(host)->ModuleArticleKey(index);
}

int HostIsSeen(void* host, const char* key) {
    return HostOf(host)->ModuleIsSeen(key) ? 1 : 0;
}

void HostMarkSeen(void* host, const char* key) {
    HostOf(host)->ModuleMarkSeen(key);
}

void HostClearSeen(void* host) {
    HostOf(host)->ModuleClearSeen();
}

const wchar_t* HostTempDir(void* host) {
    return HostOf(host)->ModuleTempDir();
}

void HostLocalTime(void* host, int* year, int* month, int* day,
                   int* weekday, int* hour, int* minute, int* second) {
    HostOf(host)->ModuleLocalTime(year, month, day, weekday,
                                  hour, minute, second);
}

unsigned int HostRandomU32(void* host) {
    return HostOf(host)->ModuleRandomU32();
}

} // namespace

// ---------------------------------------------------------------------------
bool ArticleWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW exist{};
    if (GetClassInfoExW(hInst, ClassName(), &exist) != 0) return true;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &ArticleWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = ClassName();
    return RegisterClassExW(&wc) != 0;
}

bool ArticleWindow::Create(HINSTANCE hInst, HWND main) {
    main_ = main;

    // 模块宿主 API：函数指针固定，host 指向本窗口
    moduleHost_.abi_version = ARTICLE_MODULE_ABI_VERSION;
    moduleHost_.host = this;
    moduleHost_.article_count = &HostArticleCount;
    moduleHost_.article_key = &HostArticleKey;
    moduleHost_.is_seen = &HostIsSeen;
    moduleHost_.mark_seen = &HostMarkSeen;
    moduleHost_.clear_seen = &HostClearSeen;
    moduleHost_.temp_dir = &HostTempDir;
    moduleHost_.local_time = &HostLocalTime;
    moduleHost_.random_u32 = &HostRandomU32;

    hwnd_ = CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        ClassName(), L"", WS_POPUP, 0, 0, 0, 0,
        nullptr, nullptr, hInst, this);
    return hwnd_ != nullptr;
}

void ArticleWindow::Destroy() {
    StopAnimation();
    module_.Destroy();
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

LRESULT CALLBACK ArticleWindow::WndProc(HWND hwnd, UINT msg,
                                        WPARAM wp, LPARAM lp) {
    ArticleWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<ArticleWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<ArticleWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT ArticleWindow::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
            Render();
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_WINDOWPOSCHANGING: {
            auto* pwp = reinterpret_cast<WINDOWPOS*>(lp);
            if (cfg_.articleIsTop && (pwp->flags & SWP_NOZORDER) == 0) {
                pwp->hwndInsertAfter = HWND_TOPMOST;
            }
            return 0;
        }
        case WM_DPICHANGED:
            dpi_ = LOWORD(wp);
            Reposition();
            return 0;
        case WM_TIMER:
            if (wp == kAnimTimerId) {
                StepAnimation();
                return 0;
            }
            return DefWindowProcW(hwnd, msg, wp, lp);
        case WM_DESTROY:
            StopAnimation();
            ReleaseDib();
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---------------------------------------------------------------------------
// 模块宿主实现
// ---------------------------------------------------------------------------
int ArticleWindow::ModuleArticleCount() const {
    return static_cast<int>(articles_.size());
}

const char* ArticleWindow::ModuleArticleKey(int index) const {
    if (index < 0 || index >= static_cast<int>(articleKeys_.size())) return "";
    return articleKeys_[index].c_str();
}

bool ArticleWindow::ModuleIsSeen(const char* key) const {
    if (!key || !key[0]) return false;
    return history::IsSeenKey(history::Kind::Article, key);
}

void ArticleWindow::ModuleMarkSeen(const char* key) {
    if (!key || !key[0]) return;
    history::MarkSeenKey(history::Kind::Article, key);
}

void ArticleWindow::ModuleClearSeen() {
    history::Clear(history::Kind::Article);
}

const wchar_t* ArticleWindow::ModuleTempDir() const {
    return history::TempDir().c_str();
}

void ArticleWindow::ModuleLocalTime(int* year, int* month, int* day,
                                    int* weekday, int* hour, int* minute,
                                    int* second) {
    std::time_t now = std::time(nullptr);
    std::tm t{};
    localtime_s(&t, &now);
    if (year)    *year = t.tm_year + 1900;
    if (month)   *month = t.tm_mon + 1;
    if (day)     *day = t.tm_mday;
    if (weekday) *weekday = (t.tm_wday == 0) ? 7 : t.tm_wday; // 1=周一
    if (hour)    *hour = t.tm_hour;
    if (minute)  *minute = t.tm_min;
    if (second)  *second = t.tm_sec;
}

unsigned int ArticleWindow::ModuleRandomU32() {
    static std::mt19937 gen(
        static_cast<unsigned>(std::time(nullptr)) ^
        static_cast<unsigned>(GetTickCount()));
    return gen();
}

// ---------------------------------------------------------------------------
// 配置与文章加载
// ---------------------------------------------------------------------------
void ArticleWindow::ApplyConfig(const Config& cfg) {
    cfg_ = cfg;
    if (!hwnd_) return;

    if (!cfg_.articleEnabled) {
        StopAnimation();
        ShowWindow(hwnd_, SW_HIDE);
        hasArticle_ = false;
        return;
    }

    LoadArticles();
    if (articles_.empty()) {
        StopAnimation();
        ShowWindow(hwnd_, SW_HIDE);
        hasArticle_ = false;
        return;
    }
    if (hasArticle_ && index_ >= articles_.size()) hasArticle_ = false;

    bool changed = SelectArticle(hasArticle_ ? ARTICLE_SELECT_CONFIG
                                             : ARTICLE_SELECT_STARTUP);
    if (!hasArticle_) {
        StopAnimation();
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }
    // 配置变化（字体/颜色/尺寸/模块）都需要重新布局
    (void)changed;
    Reposition();
}

void ArticleWindow::LoadArticles() {
    articles_.clear();
    articleKeys_.clear();

    std::wstring dir = utils::GetExeDir() + L"article";
    utils::EnsureDirectory(dir);

    std::vector<std::wstring> files = utils::ListTxtFiles(dir);
    if (files.empty()) {
        CreateDefaultFile();
        files = utils::ListTxtFiles(dir);
    }

    for (const auto& file : files) {
        std::wstring text;
        if (!utils::ReadTextFile(file, text, 1024 * 1024)) continue;
        text.erase(std::remove(text.begin(), text.end(), L'\r'), text.end());
        SplitArticles(text, articles_);
    }

    articleKeys_.reserve(articles_.size());
    for (const auto& article : articles_) {
        articleKeys_.push_back(history::KeyOfText(article));
    }
}

void ArticleWindow::CreateDefaultFile() {
    std::wstring dir = utils::GetExeDir() + L"article";
    utils::EnsureDirectory(dir);
    utils::WriteTextFile(dir + L"\\default.txt", kDefaultArticleText);
}

// ---------------------------------------------------------------------------
// 选择文章
// ---------------------------------------------------------------------------
bool ArticleWindow::SelectArticle(int reason) {
    if (articles_.empty()) {
        hasArticle_ = false;
        return false;
    }

    bool oldHas = hasArticle_;
    size_t oldIndex = index_;

    const ArticleModuleEntry* entry =
        ArticleModuleRegistry::Instance().Find(cfg_.articleModule);

    if (entry) {
        if (!module_.Valid() || moduleId_ != entry->id) {
            module_.Destroy();
            moduleId_.clear();
            if (module_.Create(*entry, &moduleHost_)) {
                moduleId_ = entry->id;
            }
        }
        if (module_.Valid()) {
            module_.ApplyConfig(cfg_.ArticleModuleConfig(entry->id));
            int picked = module_.Select(hasArticle_ ? static_cast<int>(index_) : -1,
                                        reason);
            if (picked >= 0 && picked < static_cast<int>(articles_.size())) {
                index_ = static_cast<size_t>(picked);
                hasArticle_ = true;
                MarkCurrentSeen();
            } else {
                // week 模块在某天不属于任何时间段时返回 -1：隐藏一文
                hasArticle_ = false;
            }
            return oldHas != hasArticle_ || (hasArticle_ && oldIndex != index_);
        }
    }

    // ---- 内置回退：模块缺失/加载失败 ----
    module_.Destroy();
    moduleId_.clear();

    if (reason != ARTICLE_SELECT_REFRESH && hasArticle_) {
        MarkCurrentSeen();
        return false;
    }
    if (reason != ARTICLE_SELECT_REFRESH) {
        index_ = PickRandomUnseen();
    } else {
        size_t next = AdvanceRandom();
        if (next == index_ && articles_.size() > 1) next = PickRandomUnseen();
        index_ = next;
    }
    hasArticle_ = true;
    MarkCurrentSeen();
    return oldHas != hasArticle_ || oldIndex != index_;
}

void ArticleWindow::MarkCurrentSeen() {
    if (!hasArticle_ || index_ >= articleKeys_.size()) return;
    history::MarkSeenKey(history::Kind::Article, articleKeys_[index_]);
}

void ArticleWindow::RefreshNow() {
    if (!hwnd_ || !cfg_.articleEnabled || articles_.empty()) return;
    bool changed = SelectArticle(ARTICLE_SELECT_REFRESH);
    if (changed) {
        if (hasArticle_) {
            Reposition();
        } else {
            StopAnimation();
            ShowWindow(hwnd_, SW_HIDE);
        }
    }
}

void ArticleWindow::RefreshForDayChange() {
    if (!hwnd_ || !cfg_.articleEnabled || articles_.empty()) return;
    bool changed = SelectArticle(ARTICLE_SELECT_CONFIG);
    if (changed) {
        if (hasArticle_) Reposition();
        else {
            StopAnimation();
            ShowWindow(hwnd_, SW_HIDE);
        }
    }
}

int ArticleWindow::RefreshSeconds() const {
    // 模块自行声明刷新节奏；week 等不导出的模块视为“只在跨天刷新”
    if (module_.Valid()) return module_.RefreshSeconds();
    return 300; // 模块缺失时的内置随机回退仍按默认间隔换文
}

// 随机选一篇未显示过的文章；全部显示过则清空记录开始新一轮
size_t ArticleWindow::PickRandomUnseen() {
    if (articles_.empty()) return 0;

    std::vector<size_t> candidates;
    history::CollectUnseen(history::Kind::Article, articles_, candidates);
    if (candidates.empty()) {
        history::Clear(history::Kind::Article);
        candidates.resize(articles_.size());
        for (size_t i = 0; i < articles_.size(); ++i) candidates[i] = i;
    }

    static std::mt19937 gen(
        static_cast<unsigned>(std::time(nullptr)) ^
        static_cast<unsigned>(GetTickCount()));
    std::uniform_int_distribution<size_t> dist(0, candidates.size() - 1);
    return candidates[dist(gen)];
}

size_t ArticleWindow::AdvanceRandom() {
    if (articles_.size() <= 1) return index_;
    size_t next = index_;
    for (int i = 0; i < 8 && next == index_; ++i) {
        next = PickRandomUnseen();
    }
    return next;
}

// ---------------------------------------------------------------------------
// DPI / 定位 / 避让
// ---------------------------------------------------------------------------
int ArticleWindow::S(int value) const {
    return utils::ScaleDpi(value, static_cast<int>(dpi_));
}

void ArticleWindow::Reposition() {
    if (!hwnd_) return;
    if (!cfg_.articleEnabled || !hasArticle_ || articles_.empty()) {
        StopAnimation();
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }

    dpi_ = static_cast<UINT>(utils::GetWindowDpi(main_ ? main_ : hwnd_));

    RECT work{};
    HMONITOR mon = MonitorFromWindow(main_ ? main_ : hwnd_,
                                     MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    int workW = work.right - work.left;
    int workH = work.bottom - work.top;

    int padX = S(kArticlePadX);
    int padY = S(kArticlePadY);
    const std::wstring& text = articles_[index_];
    const wchar_t* family = cfg_.articleFont.empty()
        ? L"Segoe UI" : cfg_.articleFont.c_str();

    int baseLogical = std::max(8, std::min(96, cfg_.articleFontSize));
    int basePx = S(baseLogical);

    HDC dc = GetDC(nullptr);
    auto makeFont = [&](int px) -> HFONT {
        return CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, family);
    };

    int w;
    if (cfg_.articleSizeMode == ArticleSizeMode::AutoFit) {
        int maxLineW = 0;
        if (dc) {
            HFONT f = makeFont(basePx);
            if (f) {
                HFONT old = (HFONT)SelectObject(dc, f);
                size_t pos = 0;
                while (pos <= text.size()) {
                    size_t nl = text.find(L'\n', pos);
                    size_t len = (nl == std::wstring::npos ? text.size() : nl) - pos;
                    if (len > 0) {
                        SIZE sz{};
                        GetTextExtentPoint32W(dc, text.c_str() + pos,
                                              static_cast<int>(len), &sz);
                        maxLineW = std::max(maxLineW, static_cast<int>(sz.cx));
                    }
                    if (nl == std::wstring::npos) break;
                    pos = nl + 1;
                }
                SelectObject(dc, old);
                DeleteObject(f);
            }
        }
        w = maxLineW + 2 * padX;
        w = std::max(w, S(220));
        w = std::min(w, std::min(S(600), workW - S(24)));
    } else {
        w = S(std::max(180, std::min(1200, cfg_.articleWidth)));
        w = std::min(w, workW - S(24));
    }

    int innerW = w - 2 * padX;
    bool custom = cfg_.articleSizeMode == ArticleSizeMode::Custom;
    int targetH = std::min(S(std::max(120, std::min(2000, cfg_.articleHeight))),
                           workH - S(24));
    int maxInnerH = custom ? (targetH - 2 * padY)
                           : (workH - S(24) - 2 * padY);
    if (innerW < S(80) || maxInnerH < S(40)) {
        if (dc) ReleaseDC(nullptr, dc);
        ShowWindow(hwnd_, SW_HIDE);
        return;
    }

    int bestPx = S(8);
    int bestH  = 0;
    if (dc) {
        for (int logical = baseLogical; logical >= 8; --logical) {
            int px = S(logical);
            HFONT f = makeFont(px);
            if (!f) continue;
            HFONT old = (HFONT)SelectObject(dc, f);
            RECT rc = {0, 0, innerW, 0};
            DrawTextW(dc, text.c_str(), -1, &rc,
                      DT_CALCRECT | DT_WORDBREAK | DT_NOPREFIX
                      | DT_EXPANDTABS | DT_EDITCONTROL);
            SelectObject(dc, old);
            DeleteObject(f);

            bestPx = px;
            bestH  = rc.bottom;
            if (bestH <= maxInnerH) break;
        }
        ReleaseDC(nullptr, dc);
    }

    layoutFontPx_ = bestPx;

    int h;
    if (custom) {
        h = targetH;
    } else {
        h = bestH + 2 * padY;
        h = std::min(h, workH - S(24));
        h = std::max(h, S(160));
    }

    int x, y;
    switch (cfg_.articleAnchor) {
        case WindowAnchor::TopLeft:
        case WindowAnchor::CenterLeft:
        case WindowAnchor::BottomLeft:
            x = work.left + cfg_.articlePlaceX;
            break;
        case WindowAnchor::TopCenter:
        case WindowAnchor::Center:
        case WindowAnchor::BottomCenter:
            x = work.left + (workW - w) / 2 + cfg_.articlePlaceX;
            break;
        default:
            x = work.right - w + cfg_.articlePlaceX;
            break;
    }
    switch (cfg_.articleAnchor) {
        case WindowAnchor::TopLeft:
        case WindowAnchor::TopCenter:
        case WindowAnchor::TopRight:
            y = work.top + cfg_.articlePlaceY;
            break;
        case WindowAnchor::CenterLeft:
        case WindowAnchor::Center:
        case WindowAnchor::CenterRight:
            y = work.top + (workH - h) / 2 + cfg_.articlePlaceY;
            break;
        default:
            y = work.bottom - h + cfg_.articlePlaceY;
            break;
    }

    if (main_) {
        RECT mr{};
        if (GetWindowRect(main_, &mr)) {
            RECT ar = {x, y, x + w, y + h};
            RECT tmp{};
            if (IntersectRect(&tmp, &ar, &mr)) {
                int gap = S(14);
                if (y + h / 2 >= mr.top + (mr.bottom - mr.top) / 2) {
                    y = mr.bottom + gap;
                } else {
                    y = mr.top - gap - h;
                }
                if (y + h > work.bottom) y = work.bottom - h;
                if (y < work.top) y = work.top;

                ar = {x, y, x + w, y + h};
                if (IntersectRect(&tmp, &ar, &mr)) {
                    if (x + w / 2 < mr.left + (mr.right - mr.left) / 2) {
                        x = mr.left - gap - w;
                    } else {
                        x = mr.right + gap;
                    }
                    if (x + w > work.right) x = work.right - w;
                    if (x < work.left) x = work.left;
                }
            }
        }
    }

    bool animate = IsWindowVisible(hwnd_) != FALSE;
    ApplyTargetRect(x, y, w, h, cfg_.articleIsTop, animate);
}

// ---------------------------------------------------------------------------
// 尺寸/位置过渡动画
// ---------------------------------------------------------------------------
void ArticleWindow::StopAnimation() {
    if (animating_) {
        animating_ = false;
        KillTimer(hwnd_, kAnimTimerId);
    }
}

void ArticleWindow::ApplyTargetRect(int x, int y, int w, int h,
                                    bool topmost, bool animate) {
    if (!hwnd_) return;

    RECT cur{};
    GetWindowRect(hwnd_, &cur);
    int cw = cur.right - cur.left;
    int ch = cur.bottom - cur.top;

    if (!animate || cw <= 0 || ch <= 0) {
        StopAnimation();
        SetWindowPos(hwnd_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ApplyRoundedRegion();
        Render();
        return;
    }

    if (cur.left == x && cur.top == y && cw == w && ch == h) {
        SetWindowPos(hwnd_, topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ApplyRoundedRegion();
        Render();
        return;
    }

    animFrom_ = cur;
    animTo_ = RECT{x, y, x + w, y + h};
    animTopmost_ = topmost;
    animStart_ = GetTickCount();
    if (!animating_) {
        animating_ = true;
        SetTimer(hwnd_, kAnimTimerId, 16, nullptr);
    }
}

void ArticleWindow::StepAnimation() {
    if (!animating_) {
        KillTimer(hwnd_, kAnimTimerId);
        return;
    }

    DWORD elapsed = GetTickCount() - animStart_;
    float t = static_cast<float>(elapsed) / 180.0f;
    bool done = false;
    if (t >= 1.0f) {
        t = 1.0f;
        done = true;
    }
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    auto lerp = [e](int a, int b) {
        return a + static_cast<int>((b - a) * e);
    };

    int fw = animFrom_.right - animFrom_.left;
    int fh = animFrom_.bottom - animFrom_.top;
    int tw = animTo_.right - animTo_.left;
    int th = animTo_.bottom - animTo_.top;
    int x = lerp(animFrom_.left, animTo_.left);
    int y = lerp(animFrom_.top, animTo_.top);
    int w = lerp(fw, tw);
    int h = lerp(fh, th);
    if (w < 1) w = 1;
    if (h < 1) h = 1;

    SetWindowPos(hwnd_, nullptr, x, y, w, h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ApplyRoundedRegion();
    Render();

    if (done) {
        animating_ = false;
        KillTimer(hwnd_, kAnimTimerId);
        SetWindowPos(hwnd_, animTopmost_ ? HWND_TOPMOST : HWND_NOTOPMOST,
                     0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
}

void ArticleWindow::ApplyRoundedRegion() {
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    int r = S(18);
    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, r, r);
    SetWindowRgn(hwnd_, rgn, TRUE);
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------
bool ArticleWindow::EnsureDib(int w, int h) {
    if (dib_ && dib_w_ == w && dib_h_ == h) return true;

    if (!mem_dc_) {
        HDC screen = GetDC(nullptr);
        mem_dc_ = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!mem_dc_) return false;
    }
    if (dib_) {
        SelectObject(mem_dc_, old_dib_);
        DeleteObject(dib_);
        dib_ = nullptr;
        dib_bits_ = nullptr;
        old_dib_ = nullptr;
    }

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth  = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screen = GetDC(nullptr);
    HBITMAP dib = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, screen);
    if (!dib) return false;

    old_dib_  = SelectObject(mem_dc_, dib);
    dib_      = dib;
    dib_bits_ = bits;
    dib_w_    = w;
    dib_h_    = h;
    return true;
}

void ArticleWindow::DestroyTextFont() {
    if (textFont_) {
        DeleteObject(textFont_);
        textFont_ = nullptr;
    }
    textFontPx_ = 0;
    textFontFamily_.clear();
}

void ArticleWindow::ReleaseDib() {
    DestroyTextFont();
    if (mem_dc_) {
        if (dib_) {
            SelectObject(mem_dc_, old_dib_);
            DeleteObject(dib_);
        }
        DeleteDC(mem_dc_);
    }
    mem_dc_   = nullptr;
    dib_      = nullptr;
    old_dib_  = nullptr;
    dib_bits_ = nullptr;
    dib_w_    = 0;
    dib_h_    = 0;
}

void ArticleWindow::Render() {
    if (!hwnd_ || !cfg_.articleEnabled || !hasArticle_ || articles_.empty()) return;

    RECT wr{};
    GetWindowRect(hwnd_, &wr);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return;
    if (!EnsureDib(w, h)) return;

    HDC mem = mem_dc_;
    BYTE* base = static_cast<BYTE*>(dib_bits_);

    // 一文独立背景色
    COLORREF bg = cfg_.articleBackgroundColor;
    COLORREF bgTop = utils::BlendColor(bg, RGB(255, 255, 255), 0.10f);
    COLORREF bgBottom = utils::BlendColor(bg, RGB(0, 0, 0), 0.14f);
    TRIVERTEX verts[2] = {};
    verts[0].x = 0;  verts[0].y = 0;
    verts[0].Red   = static_cast<COLOR16>(GetRValue(bgTop) << 8);
    verts[0].Green = static_cast<COLOR16>(GetGValue(bgTop) << 8);
    verts[0].Blue  = static_cast<COLOR16>(GetBValue(bgTop) << 8);
    verts[0].Alpha = static_cast<COLOR16>(255 << 8);
    verts[1].x = w;  verts[1].y = h;
    verts[1].Red   = static_cast<COLOR16>(GetRValue(bgBottom) << 8);
    verts[1].Green = static_cast<COLOR16>(GetGValue(bgBottom) << 8);
    verts[1].Blue  = static_cast<COLOR16>(GetBValue(bgBottom) << 8);
    verts[1].Alpha = static_cast<COLOR16>(255 << 8);
    GRADIENT_RECT gr = {0, 1};
    GradientFill(mem, verts, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    int r = S(18);
    int bgLum = (GetRValue(bg) * 299 + GetGValue(bg) * 587 +
                 GetBValue(bg) * 114) / 1000;
    COLORREF borderColor = (bgLum > 140)
                               ? utils::BlendColor(bg, RGB(0, 0, 0), 0.24f)
                               : utils::BlendColor(bg, RGB(255, 255, 255), 0.24f);
    HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
    HGDIOBJ oldPen = SelectObject(mem, pen);
    HGDIOBJ oldBr = SelectObject(mem, GetStockObject(NULL_BRUSH));
    RoundRect(mem, 0, 0, w - 1, h - 1, r, r);
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBr);
    DeleteObject(pen);

    const wchar_t* family = cfg_.articleFont.empty()
        ? L"Segoe UI" : cfg_.articleFont.c_str();
    int fontPx = layoutFontPx_ > 0 ? layoutFontPx_ : S(14);
    if (!textFont_ || textFontPx_ != fontPx ||
        lstrcmpiW(textFontFamily_.c_str(), family) != 0) {
        DestroyTextFont();
        textFont_ = CreateFontW(-fontPx, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                DEFAULT_PITCH | FF_DONTCARE, family);
        textFontPx_ = fontPx;
        textFontFamily_ = family;
    }
    if (textFont_) {
        HFONT old = (HFONT)SelectObject(mem, textFont_);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, cfg_.articleTextColor);
        RECT rc = {S(kArticlePadX), S(kArticlePadY),
                   w - S(kArticlePadX), h - S(kArticlePadY)};
        DrawTextW(mem, articles_[index_].c_str(), -1, &rc,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX
                  | DT_EXPANDTABS | DT_EDITCONTROL);
        SelectObject(mem, old);
    }

    DWORD* px = reinterpret_cast<DWORD*>(base);
    for (int i = 0, n = w * h; i < n; ++i) px[i] |= 0xFF000000u;

    POINT pt = {wr.left, wr.top};
    SIZE  sz = {w, h};
    POINT zero{0, 0};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 228;
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd_, nullptr, &pt, &sz, mem, &zero, 0, &bf, ULW_ALPHA);
}
