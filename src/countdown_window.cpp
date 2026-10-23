// =============================================================================
// countdown_window.cpp - 主倒计时窗口实现
// =============================================================================
#include "countdown_window.h"
#include "../resource.h"
#include "utils.h"
#include "settings_window.h"

#include <windows.h>
#include <windowsx.h>
#include <ctime>
#include <chrono>
#include <algorithm>

// Explorer 重启后广播的注册消息：收到后需重新添加托盘图标
static UINT g_taskbarCreatedMsg = 0;

// ---------------------------------------------------------------------------
// 窗口类注册
// ---------------------------------------------------------------------------
bool CountdownWindow::RegisterClass(HINSTANCE hInst) {
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = &CountdownWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // 自绘背景
    wc.lpszClassName = ClassName();
    wc.hIcon         = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                                        IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm       = wc.hIcon;
    return RegisterClassExW(&wc) != 0;
}

// ---------------------------------------------------------------------------
// 创建
// ---------------------------------------------------------------------------
bool CountdownWindow::Create(HINSTANCE hInst) {
    // 一文窗口类（失败不影响倒计时主功能）
    ArticleWindow::RegisterClass(hInst);

    // 置顶/层级由 Reposition 与 WM_WINDOWPOSCHANGING 按配置动态控制：
    // 此处不设 WS_EX_TOPMOST，创建后按 config_.isTop 决定是否置顶。
    DWORD ex_style = WS_EX_LAYERED | WS_EX_TRANSPARENT
                     | WS_EX_TOOLWINDOW; // 不在任务栏显示
    DWORD style   = WS_POPUP;

    hwnd_ = CreateWindowExW(ex_style, ClassName(), L"", style,
                            0, 0, 0, 0, nullptr, nullptr, hInst, this);
    return hwnd_ != nullptr;
}

// ---------------------------------------------------------------------------
// 窗口过程
// ---------------------------------------------------------------------------
LRESULT CALLBACK CountdownWindow::WndProc(HWND hwnd, UINT msg,
                                           WPARAM wp, LPARAM lp) {
    CountdownWindow* self = nullptr;
    if (msg == WM_NCCREATE) {
        auto cs = reinterpret_cast<CREATESTRUCT*>(lp);
        self = reinterpret_cast<CountdownWindow*>(cs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        self->hwnd_ = hwnd;
    } else {
        self = reinterpret_cast<CountdownWindow*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (self) return self->Handle(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT CountdownWindow::Handle(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    // 资源管理器重启后托盘图标会消失，需要重新添加
    if (g_taskbarCreatedMsg != 0 && msg == g_taskbarCreatedMsg) {
        tray_.Add(hwnd_, icon_, config_.title);
        return 0;
    }

    switch (msg) {
        case WM_CREATE:         OnCreate();                       return 0;
        case WM_DPICHANGED:
            dpi_ = LOWORD(wp);
            RebuildFonts();
            Reposition();
            article_.Reposition(); // 一文按新 DPI 重新避让
            return 0;
        case WM_PAINT: {
            // 分层窗口内容由 UpdateLayeredWindow 提供，这里仅验证区域
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            EndPaint(hwnd_, &ps);
            Render();
            return 0;
        }
        case WM_TIMER:          OnTimer();                        return 0;
        case WM_APP_TRAY:       HandleTrayMessage(wp, lp);        return 0;
        case WM_APP_OPEN_SETTINGS: OpenSettings();                return 0;
        case WM_APP_HITOKOTO: {
            // 网络一言结果（后台线程 PostMessage 传递 new std::wstring）
            auto* s = reinterpret_cast<std::wstring*>(lp);
            if (s) {
                hitokoto_.OnNetworkResult(*s);
                delete s;
            }
            return 0;
        }
        case WM_WINDOWPOSCHANGING: {
            // 仅 isTop=true 时强制保持置顶（压过全屏/置顶应用）；
            // isTop=false 时不干预 z 序，窗口可被其他普通窗口正常遮挡
            auto* pwp = reinterpret_cast<WINDOWPOS*>(lp);
            if (config_.isTop && (pwp->flags & SWP_NOZORDER) == 0) {
                pwp->hwndInsertAfter = HWND_TOPMOST;
            }
            return 0;
        }
        case WM_DESTROY:
            OnDestroy();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, msg, wp, lp);
    }
}

// ---------------------------------------------------------------------------
// 创建时的初始化
// ---------------------------------------------------------------------------
void CountdownWindow::OnCreate() {
    // 加载图标
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hwnd_, GWLP_HINSTANCE);
    icon_ = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON),
                              IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);

    // DPI 与托盘重建消息（Explorer 重启后重新添加图标）
    dpi_ = static_cast<UINT>(utils::GetWindowDpi(hwnd_));
    if (g_taskbarCreatedMsg == 0) {
        g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
    }

    // 加载配置
    config_ = Config::Load();
    RebuildFonts();
    hitokoto_.Init(hwnd_, config_);

    // 一文栏目（独立分层窗口，默认屏幕右侧）
    article_.Create(hInst, hwnd_);

    // 定位与尺寸（内部会应用圆角区域，并同步一文的避让位置）
    Reposition();
    article_.ApplyConfig(config_);

    // 添加托盘
    tray_.Add(hwnd_, icon_, config_.title);

    // 启动后立即显示第一句一言
    hitokoto_.FetchNext();

    // 启动定时器（常态 250ms；一言动画期间由 OnTimer 自动提速）
    timer_id_ = SetTimer(hwnd_, 1, 250, nullptr);
}

// ---------------------------------------------------------------------------
// DPI 与字号
// ---------------------------------------------------------------------------
int CountdownWindow::S(int value) const {
    return utils::ScaleDpi(value, static_cast<int>(dpi_));
}

int CountdownWindow::TitleFontPx() const {
    return S(std::max(12, config_.fontSize * 4 / 9));
}

int CountdownWindow::ContentFontPx() const {
    return S(config_.fontSize);
}

int CountdownWindow::TargetFontPx() const {
    return S(12);
}

int CountdownWindow::HkBaseFontPx() const {
    return S(std::max(11, config_.fontSize / 2));
}

// ---------------------------------------------------------------------------
// 字体（缓存在成员中，配置/DPI 变化时重建）
// ---------------------------------------------------------------------------
void CountdownWindow::RebuildFonts() {
    DestroyFonts();
    const wchar_t* family = config_.font.empty()
        ? L"Segoe UI" : config_.font.c_str();

    fTitle_ = CreateFontW(-TitleFontPx(), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                          CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                          DEFAULT_PITCH | FF_DONTCARE, family);
    fContent_ = CreateFontW(-ContentFontPx(), 0, 0, 0, FW_BOLD,
                            FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                            CLEARTYPE_QUALITY,
                            DEFAULT_PITCH | FF_DONTCARE, family);
    fTarget_ = CreateFontW(-TargetFontPx(), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, family);
}

void CountdownWindow::DestroyFonts() {
    if (fTitle_)   { DeleteObject(fTitle_);   fTitle_ = nullptr; }
    if (fContent_) { DeleteObject(fContent_); fContent_ = nullptr; }
    if (fTarget_)  { DeleteObject(fTarget_);  fTarget_ = nullptr; }
    if (fHk_)      { DeleteObject(fHk_);      fHk_ = nullptr; }
    fHkSize_ = 0;
    lastHkText_.clear();
    lastTargetText_.clear();
    contentCacheValid_ = false; // 字体变化，倒计时文本缓存一并失效
    dirty_ = true;
}

// ---------------------------------------------------------------------------
// 圆角区域
// ---------------------------------------------------------------------------
void CountdownWindow::ApplyRoundedRegion() {
    RECT rc;
    GetClientRect(hwnd_, &rc);
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    // 圆角半径 = 高度的一半，得到胶囊形
    int r = h / 2;
    HRGN rgn = CreateRoundRectRgn(0, 0, w, h, r, r);
    SetWindowRgn(hwnd_, rgn, TRUE);
}

// ---------------------------------------------------------------------------
// 重定位：按锚点模板 + 偏移在工作区内定位，并按 isTop 决定是否置顶
// ---------------------------------------------------------------------------
static int MeasureTextWidth(HFONT font, const std::wstring& text) {
    if (!font || text.empty()) return 0;
    HDC dc = GetDC(nullptr);
    if (!dc) return 0;
    HFONT old = (HFONT)SelectObject(dc, font);
    SIZE sz{};
    GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &sz);
    SelectObject(dc, old);
    ReleaseDC(nullptr, dc);
    return sz.cx;
}

void CountdownWindow::Reposition() {
    // 以窗口当前所在显示器为准（Per-Monitor DPI：跨屏后按新 DPI 缩放）
    dpi_ = static_cast<UINT>(utils::GetWindowDpi(hwnd_));

    RECT work{};
    HMONITOR mon = MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoW(mon, &mi)) {
        work = mi.rcWork;
    } else {
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    }
    int work_w = work.right - work.left;
    int work_h = work.bottom - work.top;

    // 窗口宽度按实际文本像素宽度计算，避免中文单位被裁切
    long long rem = ComputeRemainingSeconds();
    std::wstring content = utils::FormatRemainingTime(
        rem, config_.displayFormat, config_.includeStartDay);
    std::wstring target = L"\u76ee\u6807\uff1a"
                        + utils::FormatTargetTime(config_.EffectiveTargetTime());

    int textW = MeasureTextWidth(fContent_, content);
    textW = std::max(textW, MeasureTextWidth(fTitle_, config_.title));
    textW = std::max(textW, MeasureTextWidth(fTarget_, target));
    int w = textW + S(60);
    w = std::max(w, S(560));
    // 工作区过窄时不再收缩，避免 work_w-16 为负导致窗口反向溢出
    if (work_w > S(34)) w = std::min(w, work_w - S(16));

    // 窗口高度：标题 + 倒计时大字 + 目标时间 + 一言区域（最多 2 行）
    int h = S(6) + (TitleFontPx() + S(4)) + (ContentFontPx() + S(4))
            + S(16) + S(2) + (2 * HkBaseFontPx() + S(6)) + S(4);
    h = std::max(h, S(80));

    // 锚点模板：按水平/垂直分量分别取基准位置，再叠加用户偏移
    int x, y;
    switch (config_.anchor) {
        case WindowAnchor::TopLeft:
        case WindowAnchor::CenterLeft:
        case WindowAnchor::BottomLeft:
            x = work.left + config_.placeX;
            break;
        case WindowAnchor::TopCenter:
        case WindowAnchor::Center:
        case WindowAnchor::BottomCenter:
            x = work.left + (work_w - w) / 2 + config_.placeX;
            break;
        default: // 右侧三个锚点
            x = work.right - w + config_.placeX;
            break;
    }
    switch (config_.anchor) {
        case WindowAnchor::TopLeft:
        case WindowAnchor::TopCenter:
        case WindowAnchor::TopRight:
            y = work.top + config_.placeY;
            break;
        case WindowAnchor::CenterLeft:
        case WindowAnchor::Center:
        case WindowAnchor::CenterRight:
            y = work.top + (work_h - h) / 2 + config_.placeY;
            break;
        default: // 底部三个锚点
            y = work.bottom - h + config_.placeY;
            break;
    }

    // isTop=true 置顶（HWND_TOPMOST，压过全屏/置顶应用）；
    // false 用 HWND_NOTOPMOST 同时移除置顶属性并置于普通窗口顶部
    SetWindowPos(hwnd_, config_.isTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                 x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    ApplyRoundedRegion();
    RequestRedraw();
}

// ---------------------------------------------------------------------------
// 剩余秒数计算
// ---------------------------------------------------------------------------
long long CountdownWindow::ComputeRemainingSeconds() const {
    // 循环开启时使用顺延后的生效目标；config 存储的是本地时间
    std::tm local = config_.EffectiveTargetTime();
    local.tm_isdst = -1; // 让运行时决定 DST
    std::time_t target = std::mktime(&local);

    std::time_t now = std::time(nullptr);
    return static_cast<long long>(target - now);
}

void CountdownWindow::RequestRedraw() {
    dirty_ = true;
    InvalidateRect(hwnd_, nullptr, FALSE);
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
namespace {
void DrawTextCentered(HDC dc, const std::wstring& text,
                      HFONT font, COLORREF color, RECT rc) {
    HFONT oldFont = (HFONT)SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    // DT_VCENTER 需要配合 DT_SINGLELINE
    DrawTextW(dc, text.c_str(), -1, &rc,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}
} // namespace

// 一言区域（窗口客户区坐标）：目标时间下方，高度最多 2 行
RECT CountdownWindow::HitokotoRect(int w, int h) const {
    int top = S(6) + (TitleFontPx() + S(4)) + (ContentFontPx() + S(4))
              + S(16) + S(2);
    int bottom = std::min(h - S(4), top + 2 * HkBaseFontPx() + S(6));
    return {S(8), top, w - S(8), bottom};
}

// 一言文本：字号随内容缩放，确保在区域内最多 2 行。
// 字体缓存于成员 fHk_：文本未变化时直接复用，避免每帧 CreateFontW。
// fade<255（切换动画）时，文本先绘制到独立文本层（黑底）再与背景合成，
// 使渐隐渐显只作用于文字本身，背景保持恒定不透明。
void CountdownWindow::DrawHitokoto(HDC mem, int w, int h,
                                   BYTE* base, int stride, int fade) {
    const std::wstring& hk = hitokoto_.Text();
    if (hk.empty()) return;

    RECT area = HitokotoRect(w, h);
    int availW = area.right - area.left;
    int maxH = area.bottom - area.top;
    if (availW <= 0 || maxH <= 0) return;

    const wchar_t* family = config_.font.empty()
        ? L"Segoe UI" : config_.font.c_str();

    // 测量：该字体能否在区域内两行内放下
    auto fits = [&](HFONT font) -> bool {
        HFONT old = (HFONT)SelectObject(mem, font);
        RECT rc = {0, 0, availW, 0};
        DrawTextW(mem, hk.c_str(), -1, &rc,
                  DT_CALCRECT | DT_WORDBREAK | DT_EDITCONTROL);
        SelectObject(mem, old);
        return rc.bottom <= maxH;
    };

    HFONT f = nullptr;
    if (fHk_ && hk == lastHkText_) {
        f = fHk_; // 文本未变：直接复用缓存字体（布局稳定时零 GDI 创建）
    } else {
        // 文本变化：从基准字号向下试探，选择能放下的最大字号并缓存
        int size = std::max(11, config_.fontSize / 2);
        while (size >= 10) {
            // 使用灰度抗锯齿：文本层像素与文本色成线性关系，便于还原 alpha
            HFONT tmp = CreateFontW(-S(size), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                    DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                    CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                                    DEFAULT_PITCH | FF_DONTCARE, family);
            if (!tmp) break;
            if (fits(tmp)) {
                if (fHk_) DeleteObject(fHk_);
                fHk_ = tmp;
                fHkSize_ = size;
                f = tmp;
                break;
            }
            DeleteObject(tmp);
            --size;
        }
        if (f) lastHkText_ = hk;
    }
    if (!f) return; // 最小字号仍放不下：放弃本次绘制

    // 文本色（与下方合成常数一致）
    const int tR = 218, tG = 200, tB = 200;

    if (fade >= 255) {
        // 稳态：直接绘制到主 DIB
        HFONT old = (HFONT)SelectObject(mem, f);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(tR, tG, tB));
        RECT rc = area;
        DrawTextW(mem, hk.c_str(), -1, &rc,
                  DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
        SelectObject(mem, old);
        // GDI 绘制可能清掉目标像素的 alpha 字节，这里恢复该区域为不透明
        int yEnd = std::min<int>(static_cast<int>(area.bottom), h);
        int xEnd = std::min<int>(static_cast<int>(area.right), w);
        for (int yy = std::max<int>(0, static_cast<int>(area.top)); yy < yEnd; ++yy) {
            DWORD* row = reinterpret_cast<DWORD*>(base + (size_t)yy * stride);
            for (int xx = std::max<int>(0, static_cast<int>(area.left)); xx < xEnd; ++xx) {
                row[xx] |= 0xFF000000u;
            }
        }
        return;
    }

    // ---- 动画中：绘制到独立文本层（黑底）再逐像素合成 ----
    HDC screen = GetDC(nullptr);
    HDC tdc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!tdc) return;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth  = availW;
    bi.bmiHeader.biHeight = -maxH; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* tbits = nullptr;
    HBITMAP tex = CreateDIBSection(tdc, &bi, DIB_RGB_COLORS, &tbits, nullptr, 0);
    if (!tex) { DeleteDC(tdc); return; }
    memset(tbits, 0, (size_t)availW * maxH * 4); // 纯黑背景

    HBITMAP oldT = (HBITMAP)SelectObject(tdc, tex);
    HFONT oldF = (HFONT)SelectObject(tdc, f);
    SetBkMode(tdc, TRANSPARENT);
    // 文本层画白色：灰度 AA 下像素值即覆盖度，无需再按文本色通道换算
    SetTextColor(tdc, RGB(255, 255, 255));
    RECT trc = {0, 0, availW, maxH};
    DrawTextW(tdc, hk.c_str(), -1, &trc,
              DT_CENTER | DT_WORDBREAK | DT_NOPREFIX | DT_EDITCONTROL);
    SelectObject(tdc, oldF);

    // 文本层为"黑底 + 白色文字"，像素亮度即覆盖度 cov：
    //   a = cov * fade / 255
    //   合成 = 背景 * (1 - a/255) + 文本色 * a/255；alpha 恒 255（背景不透明）
    int tStride = availW * 4;
    for (int y = 0; y < maxH; ++y) {
        int ay = area.top + y;
        if (ay >= h) break;
        const BYTE* tp = static_cast<const BYTE*>(tbits) + (size_t)y * tStride;
        BYTE* q = base + (size_t)ay * stride + (size_t)area.left * 4;
        for (int x = 0; x < availW; ++x, tp += 4, q += 4) {
            int cov = tp[0]; // 灰度 AA：R=G=B
            int a = cov * fade / 255;
            if (a) {
                q[0] = (BYTE)((q[0] * (255 - a) + tB * a) / 255);
                q[1] = (BYTE)((q[1] * (255 - a) + tG * a) / 255);
                q[2] = (BYTE)((q[2] * (255 - a) + tR * a) / 255);
            }
            q[3] = 255; // 背景恒定不透明
        }
    }

    SelectObject(tdc, oldT);
    DeleteObject(tex);
    DeleteDC(tdc);
}

// ---------------------------------------------------------------------------
// 整窗 DIB：尺寸不变时复用，避免每次重绘都 CreateDIBSection
// ---------------------------------------------------------------------------
bool CountdownWindow::EnsureDib(int w, int h) {
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
    bi.bmiHeader.biHeight = -h; // top-down，便于直接操作像素
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

void CountdownWindow::ReleaseDib() {
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

// 整窗重绘：32 位 DIB + UpdateLayeredWindow，
// 背景/标题/倒计时/目标时间不透明，一言区域 alpha 随动画渐变
void CountdownWindow::Render() {
    RECT wr;
    GetWindowRect(hwnd_, &wr);
    int w = wr.right - wr.left;
    int h = wr.bottom - wr.top;
    if (w <= 0 || h <= 0) return;
    if (!EnsureDib(w, h)) return;

    HDC mem = mem_dc_;
    BYTE* base = static_cast<BYTE*>(dib_bits_);

    // 背景渐变（顶深底更深）
    TRIVERTEX verts[2] = {};
    verts[0].x = 0;  verts[0].y = 0;
    verts[0].Red = 28 << 8; verts[0].Green = 28 << 8;
    verts[0].Blue = 40 << 8; verts[0].Alpha = 255 << 8;
    verts[1].x = w;  verts[1].y = h;
    verts[1].Red = 14 << 8; verts[1].Green = 14 << 8;
    verts[1].Blue = 22 << 8; verts[1].Alpha = 255 << 8;
    GRADIENT_RECT gr = {0, 1};
    GradientFill(mem, verts, 2, &gr, 1, GRADIENT_FILL_RECT_V);

    // 微弱边框光晕
    HPEN pen = CreatePen(PS_SOLID, 1, RGB(70, 70, 95));
    HGDIOBJ oldPen = SelectObject(mem, pen);
    HBRUSH br = CreateSolidBrush(RGB(20, 20, 30));
    HGDIOBJ oldBr = SelectObject(mem, br);
    int rr = h / 2;
    RoundRect(mem, 0, 0, w - 1, h - 1, rr, rr);
    SelectObject(mem, oldPen);
    SelectObject(mem, oldBr);
    DeleteObject(pen);
    DeleteObject(br);

    // ---- 文本内容 ----
    long long rem = ComputeRemainingSeconds();
    // 同一秒内多次重绘（一言动画期间 30ms/帧）不重复做逐月循环的时间换算
    if (!contentCacheValid_ || rem != lastRemSec_) {
        lastRemSec_ = rem;
        lastContent_ = utils::FormatRemainingTime(
            rem, config_.displayFormat, config_.includeStartDay);
        lastTargetText_ = L"\u76ee\u6807\uff1a"
                        + utils::FormatTargetTime(config_.EffectiveTargetTime());
        contentCacheValid_ = true;
    }
    std::wstring content = lastContent_;
    std::wstring target  = lastTargetText_;

    // 布局：标题 / 大字 / 目标时间 / 一言（全部按 DPI 缩放）
    int titleTop = S(6);
    int titleH   = TitleFontPx() + S(4);
    int contentH = ContentFontPx() + S(4);
    int targetH  = S(16);
    RECT rcTitle   = {0, titleTop, w, titleTop + titleH};
    RECT rcContent = {0, titleTop + titleH, w, titleTop + titleH + contentH};
    RECT rcTarget  = {0, titleTop + titleH + contentH, w,
                      titleTop + titleH + contentH + targetH};

    DrawTextCentered(mem, config_.title, fTitle_,   RGB(220, 220, 235), rcTitle);
    DrawTextCentered(mem, content,        fContent_, RGB(255, 255, 255), rcContent);
    DrawTextCentered(mem, target,         fTarget_,  RGB(170, 170, 190), rcTarget);

    // ---- 每像素 alpha：背景与主内容全部不透明 ----
    // 按 4 字节一次位或设置 alpha=255（保留 RGB），比逐字节循环更快
    int stride = w * 4;
    DWORD* px = reinterpret_cast<DWORD*>(base);
    for (int i = 0, n = w * h; i < n; ++i) px[i] |= 0xFF000000u;
    // 一言文本：经独立文本层合成，渐隐渐显只作用于文字，背景恒定不透明
    DrawHitokoto(mem, w, h, base, stride, hitokoto_.FadeAlpha());

    POINT pt = {wr.left, wr.top};
    SIZE  sz = {w, h};
    POINT zero{0, 0};
    BLENDFUNCTION bf{};
    bf.BlendOp = AC_SRC_OVER;
    bf.SourceConstantAlpha = 220; // 整体 86% 不透明度（与旧 SetLayeredWindowAttributes 一致）
    bf.AlphaFormat = AC_SRC_ALPHA;
    UpdateLayeredWindow(hwnd_, nullptr, &pt, &sz, mem, &zero, 0, &bf, ULW_ALPHA);

    lastRenderedRem_ = rem;
    hasRendered_ = true;
    dirty_ = false;
}

// ---------------------------------------------------------------------------
// 定时器：推进一言动画/刷新并重绘
// ---------------------------------------------------------------------------
void CountdownWindow::OnTimer() {
    hitokoto_.Tick();
    article_.Tick();

    // 一言切换动画期间提高刷新率（30ms），常态 250ms；
    // 间隔未变化时不再重复 SetTimer（每帧调用纯属冗余）
    UINT interval = hitokoto_.IsAnimating() ? 30u : 250u;
    if (interval != timer_interval_) {
        timer_interval_ = interval;
        SetTimer(hwnd_, 1, timer_interval_, nullptr);
    }

    // 仅在秒变化、动画进行中或界面变脏时重绘：
    // 静态倒计时每秒一次 UpdateLayeredWindow，而不是每 250ms 一次
    long long rem = ComputeRemainingSeconds();
    bool animating = hitokoto_.IsAnimating();
    if (animating) animWasActive_ = true;
    bool needRender = dirty_ || !hasRendered_
                      || rem != lastRenderedRem_ || animating || animWasActive_;
    if (needRender) {
        Render();
        animWasActive_ = hitokoto_.IsAnimating();
    }
}

void CountdownWindow::OnDestroy() {
    if (timer_id_) KillTimer(hwnd_, timer_id_);
    article_.Destroy();
    tray_.Remove();
    if (icon_) DestroyIcon(icon_);
    DestroyFonts();
    ReleaseDib();
}

// ---------------------------------------------------------------------------
// 应用新配置
// ---------------------------------------------------------------------------
void CountdownWindow::ApplyConfig(const Config& cfg) {
    config_ = cfg;
    RebuildFonts();
    hitokoto_.ApplyConfig(cfg);
    tray_.SetTooltip(cfg.title);
    Reposition(); // 字号/一言区域可能变化，需重新布局
    article_.ApplyConfig(cfg); // 一文开关/位置/刷新配置
}

// ---------------------------------------------------------------------------
// 托盘消息处理
// ---------------------------------------------------------------------------
void CountdownWindow::HandleTrayMessage(WPARAM wp, LPARAM lp) {
    if (LOWORD(lp) == WM_RBUTTONUP) {
        POINT pt;
        GetCursorPos(&pt);
        int cmd = tray_.ShowContextMenu(hwnd_, pt.x, pt.y);
        switch (cmd) {
            case IDM_TRAY_SETTINGS: OpenSettings();    break;
            case IDM_TRAY_EXIT:     Quit();            break;
            default: break;
        }
    } else if (LOWORD(lp) == WM_LBUTTONDBLCLK) {
        OpenSettings();
    }
}

// ---------------------------------------------------------------------------
// 打开设置窗口
// ---------------------------------------------------------------------------
void CountdownWindow::OpenSettings() {
    Config cfg = config_;
    if (OpenSettingsDialog(hwnd_, cfg)) {
        // 已保存到 config.json，应用并广播更新
        ApplyConfig(cfg);
    }
}

// ---------------------------------------------------------------------------
// 退出
// ---------------------------------------------------------------------------
void CountdownWindow::Quit() {
    // 设置窗口是模态的（自带消息循环）：若它开着，必须先关闭，
    // 否则该循环会先消费掉 WM_QUIT 再退出，主消息循环永远收不到退出消息
    CloseSettingsWindow();
    if (hwnd_) DestroyWindow(hwnd_);
}
