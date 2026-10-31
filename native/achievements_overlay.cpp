#include "achievements_overlay.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#ifdef _WIN32
#include <objidl.h>
#include <gdiplus.h>
#endif

namespace gt4::achievements {
#ifdef _WIN32
namespace {

constexpr wchar_t kOverlayClassName[] = L"GT4RecompAchievementsOverlayWindow";

std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, str.data(), static_cast<int>(str.size()), out.data(), needed);
    return out;
}

uint8_t clamp_u8(int v) {
    return static_cast<uint8_t>(std::clamp(v, 0, 255));
}

Gdiplus::Color scale_alpha(uint8_t a, uint8_t r, uint8_t g, uint8_t b, float alpha_scale) {
    const uint8_t final_a = clamp_u8(static_cast<int>(std::lround(static_cast<float>(a) * alpha_scale)));
    return Gdiplus::Color(final_a, r, g, b);
}

void add_rounded_rect(Gdiplus::GraphicsPath& path, Gdiplus::REAL x, Gdiplus::REAL y, Gdiplus::REAL w, Gdiplus::REAL h, Gdiplus::REAL radius) {
    const Gdiplus::REAL d = radius * 2.0f;
    if (d <= 0.0f || d > w || d > h) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    path.AddArc(x, y, d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    path.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();
}

void draw_badge_or_placeholder(
    Gdiplus::Graphics& g,
    const std::wstring& badge_path,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL size,
    float alpha,
    bool hardcore) {
    bool drawn = false;
    if (!badge_path.empty()) {
        const DWORD attrs = GetFileAttributesW(badge_path.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            Gdiplus::Bitmap bmp(badge_path.c_str());
            if (bmp.GetLastStatus() == Gdiplus::Ok && bmp.GetWidth() > 0 && bmp.GetHeight() > 0) {
                Gdiplus::ColorMatrix matrix = {{
                    {1.0f, 0.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 1.0f, 0.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
                    {0.0f, 0.0f, 0.0f, alpha, 0.0f},
                    {0.0f, 0.0f, 0.0f, 0.0f, 1.0f},
                }};
                Gdiplus::ImageAttributes img_attr;
                img_attr.SetColorMatrix(&matrix, Gdiplus::ColorMatrixFlagsDefault, Gdiplus::ColorAdjustTypeBitmap);
                Gdiplus::RectF dst_rect(x, y, size, size);
                g.DrawImage(
                    &bmp,
                    dst_rect,
                    0.0f,
                    0.0f,
                    static_cast<Gdiplus::REAL>(bmp.GetWidth()),
                    static_cast<Gdiplus::REAL>(bmp.GetHeight()),
                    Gdiplus::UnitPixel,
                    &img_attr);
                drawn = true;
            }
        }
    }

    if (!drawn) {
        Gdiplus::GraphicsPath box;
        add_rounded_rect(box, x, y, size, size, 6.0f);
        Gdiplus::SolidBrush fill(scale_alpha(230, 26, 32, 44, alpha));
        g.FillPath(&fill, &box);

        // Draw a stylized trophy emblem inside the placeholder box.
        Gdiplus::SolidBrush trophy_brush(
            hardcore ? scale_alpha(245, 235, 185, 65, alpha) : scale_alpha(245, 92, 148, 245, alpha));
        const Gdiplus::REAL cx = x + size * 0.5f;
        const Gdiplus::REAL cy = y + size * 0.42f;
        const Gdiplus::REAL cup_w = size * 0.42f;
        const Gdiplus::REAL cup_h = size * 0.34f;
        g.FillRectangle(&trophy_brush, cx - cup_w * 0.5f, cy - cup_h * 0.5f, cup_w, cup_h);
        g.FillRectangle(&trophy_brush, cx - size * 0.06f, cy + cup_h * 0.5f, size * 0.12f, size * 0.16f);
        g.FillRectangle(&trophy_brush, cx - size * 0.20f, cy + cup_h * 0.5f + size * 0.14f, size * 0.40f, size * 0.07f);
    }

    Gdiplus::GraphicsPath border_path;
    add_rounded_rect(border_path, x, y, size, size, 4.0f);
    Gdiplus::Pen border_pen(
        hardcore ? scale_alpha(220, 235, 185, 65, alpha) : scale_alpha(150, 255, 255, 255, alpha),
        1.5f);
    g.DrawPath(&border_pen, &border_path);
}

LRESULT CALLBACK overlay_wnd_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
    case WM_NCHITTEST:
        return HTTRANSPARENT;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_ERASEBKGND:
        return 1;
    default:
        return DefWindowProcW(hwnd, msg, wparam, lparam);
    }
}

} // namespace
#endif

AchievementsOverlay::~AchievementsOverlay() {
    shutdown();
}

void AchievementsOverlay::compute_toast_anim(double elapsed_sec, double total_sec, float& out_alpha, float& out_slide_px) {
    constexpr double kEnterSec = 0.22;
    constexpr double kExitSec = 0.35;
    if (elapsed_sec < 0.0 || elapsed_sec >= total_sec) {
        out_alpha = 0.0f;
        out_slide_px = 24.0f;
        return;
    }
    if (elapsed_sec < kEnterSec) {
        const float t = static_cast<float>(elapsed_sec / kEnterSec);
        const float ease = 1.0f - (1.0f - t) * (1.0f - t);
        out_alpha = std::clamp(ease, 0.0f, 1.0f);
        out_slide_px = (1.0f - ease) * 28.0f;
        return;
    }
    if (elapsed_sec > total_sec - kExitSec) {
        const float t = static_cast<float>((total_sec - elapsed_sec) / kExitSec);
        const float ease = std::clamp(t, 0.0f, 1.0f);
        out_alpha = ease * ease;
        out_slide_px = (1.0f - ease) * 16.0f;
        return;
    }
    out_alpha = 1.0f;
    out_slide_px = 0.0f;
}

#ifdef _WIN32
// macOS: initialize, shutdown, ensure_window, update_placement, tick and render are in achievements_overlay_mac.mm.
void AchievementsOverlay::initialize(HostWindow owner_window) {
    owner_hwnd_ = owner_window;
    if (!gdiplus_initialized_) {
        Gdiplus::GdiplusStartupInput startup_input;
        if (Gdiplus::GdiplusStartup(&gdiplus_token_, &startup_input, nullptr) == Gdiplus::Ok) {
            gdiplus_initialized_ = true;
        }
    }
    ensure_window(owner_window);
}

void AchievementsOverlay::shutdown() {
    toast_queue_.clear();
    challenges_.clear();
    trackers_.clear();
    progress_ = {};

    if (mem_dc_) {
        if (old_bitmap_) {
            SelectObject(mem_dc_, old_bitmap_);
            old_bitmap_ = nullptr;
        }
        if (dib_bitmap_) {
            DeleteObject(dib_bitmap_);
            dib_bitmap_ = nullptr;
        }
        DeleteDC(mem_dc_);
        mem_dc_ = nullptr;
    }
    dib_bits_ = nullptr;
    dib_width_ = 0;
    dib_height_ = 0;

    if (overlay_hwnd_) {
        DestroyWindow(overlay_hwnd_);
        overlay_hwnd_ = nullptr;
    }
    owner_hwnd_ = nullptr;

    if (gdiplus_initialized_) {
        Gdiplus::GdiplusShutdown(gdiplus_token_);
        gdiplus_token_ = 0;
        gdiplus_initialized_ = false;
    }
}
#endif

#ifdef _WIN32
void AchievementsOverlay::ensure_window(HostWindow owner_window) {
    if (!owner_window || !IsWindow(owner_window)) return;
    owner_hwnd_ = owner_window;
    if (overlay_hwnd_ && IsWindow(overlay_hwnd_)) return;

    HINSTANCE hinst = GetModuleHandleW(nullptr);
    static bool class_registered = false;
    if (!class_registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlay_wnd_proc;
        wc.hInstance = hinst;
        wc.lpszClassName = kOverlayClassName;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
            class_registered = true;
        }
    }

    const DWORD ex_style = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW;
    overlay_hwnd_ = CreateWindowExW(
        ex_style,
        kOverlayClassName,
        L"GT4Recomp Achievements Overlay",
        WS_POPUP | WS_DISABLED,
        0,
        0,
        1,
        1,
        owner_window,
        nullptr,
        hinst,
        nullptr);
}
#endif

void AchievementsOverlay::enqueue_toast(OverlayToast toast) {
    if (toast.duration_sec <= 0.5) {
        toast.duration_sec = 4.5;
    }
    ActiveToast active;
    active.data = std::move(toast);
    active.start_tick_ms = 0;
    toast_queue_.push_back(std::move(active));
    dirty_ = true;
}

void AchievementsOverlay::set_progress_indicator(const ProgressIndicatorState& state) {
    progress_ = state;
    dirty_ = true;
}

void AchievementsOverlay::add_challenge_indicator(const ChallengeIndicatorItem& item) {
    for (auto& existing : challenges_) {
        if (existing.id == item.id) {
            existing = item;
            dirty_ = true;
            return;
        }
    }
    challenges_.push_back(item);
    dirty_ = true;
}

void AchievementsOverlay::remove_challenge_indicator(uint32_t id) {
    const auto before = challenges_.size();
    challenges_.erase(
        std::remove_if(challenges_.begin(), challenges_.end(), [id](const ChallengeIndicatorItem& it) { return it.id == id; }),
        challenges_.end());
    if (challenges_.size() != before) {
        dirty_ = true;
    }
}

void AchievementsOverlay::set_leaderboard_tracker(const LeaderboardTrackerItem& item) {
    for (auto& existing : trackers_) {
        if (existing.id == item.id) {
            existing.display = item.display;
            dirty_ = true;
            return;
        }
    }
    trackers_.push_back(item);
    dirty_ = true;
}

void AchievementsOverlay::remove_leaderboard_tracker(uint32_t id) {
    const auto before = trackers_.size();
    trackers_.erase(
        std::remove_if(trackers_.begin(), trackers_.end(), [id](const LeaderboardTrackerItem& it) { return it.id == id; }),
        trackers_.end());
    if (trackers_.size() != before) {
        dirty_ = true;
    }
}

void AchievementsOverlay::notify_badge_ready(const std::wstring& badge_path) {
    if (badge_path.empty()) return;
    dirty_ = true;
}

void AchievementsOverlay::clear_session_indicators() {
    progress_ = {};
    challenges_.clear();
    trackers_.clear();
    dirty_ = true;
}

bool AchievementsOverlay::has_visible_content() const {
    return !toast_queue_.empty() || progress_.visible || !challenges_.empty() || !trackers_.empty();
}

void AchievementsOverlay::on_owner_pos_changed(HostWindow owner_window) {
    dirty_ = true;
    update_placement(owner_window);
}

#ifdef _WIN32
void AchievementsOverlay::update_placement(HostWindow owner_window) {
    if (!overlay_hwnd_ || !IsWindow(overlay_hwnd_) || !owner_window || !IsWindow(owner_window)) return;
    if (IsIconic(owner_window) || !IsWindowVisible(owner_window) || !has_visible_content()) {
        if (IsWindowVisible(overlay_hwnd_)) {
            ShowWindow(overlay_hwnd_, SW_HIDE);
        }
        return;
    }

    RECT client_rc{};
    if (!GetClientRect(owner_window, &client_rc)) return;
    POINT top_left{client_rc.left, client_rc.top};
    ClientToScreen(owner_window, &top_left);
    const int w = std::max<int>(1, client_rc.right - client_rc.left);
    const int h = std::max<int>(1, client_rc.bottom - client_rc.top);

    SetWindowPos(
        overlay_hwnd_,
        HWND_TOP,
        top_left.x,
        top_left.y,
        w,
        h,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW);
}

void AchievementsOverlay::tick(HostWindow owner_window) {
    if (!owner_window || !IsWindow(owner_window)) return;
    ensure_window(owner_window);
    if (!overlay_hwnd_ || !gdiplus_initialized_) return;

    const uint64_t now_ms = GetTickCount64();

    // Advance active toast queue (show up to 2 concurrent toasts for snappy feedback).
    bool toast_animating = false;
    const size_t active_limit = std::min<size_t>(2, toast_queue_.size());
    for (size_t i = 0; i < active_limit; ++i) {
        if (toast_queue_[i].start_tick_ms == 0) {
            toast_queue_[i].start_tick_ms = now_ms;
            dirty_ = true;
        }
        toast_animating = true;
    }

    while (!toast_queue_.empty() && toast_queue_.front().start_tick_ms != 0) {
        const double elapsed_sec = static_cast<double>(now_ms - toast_queue_.front().start_tick_ms) / 1000.0;
        if (elapsed_sec >= toast_queue_.front().data.duration_sec) {
            toast_queue_.erase(toast_queue_.begin());
            dirty_ = true;
        } else {
            break;
        }
    }

    if (!has_visible_content()) {
        if (IsWindowVisible(overlay_hwnd_)) {
            ShowWindow(overlay_hwnd_, SW_HIDE);
        }
        dirty_ = false;
        return;
    }

    if (IsIconic(owner_window) || !IsWindowVisible(owner_window)) {
        if (IsWindowVisible(overlay_hwnd_)) {
            ShowWindow(overlay_hwnd_, SW_HIDE);
        }
        return;
    }

    if (toast_animating || dirty_) {
        render(owner_window, now_ms);
        dirty_ = false;
    }
}

void AchievementsOverlay::render(HostWindow owner_window, uint64_t now_ms) {
    RECT client_rc{};
    if (!GetClientRect(owner_window, &client_rc)) return;
    POINT screen_pt{client_rc.left, client_rc.top};
    ClientToScreen(owner_window, &screen_pt);
    const int width = std::max<int>(1, client_rc.right - client_rc.left);
    const int height = std::max<int>(1, client_rc.bottom - client_rc.top);

    HDC screen_dc = GetDC(nullptr);
    if (!screen_dc) return;

    if (!mem_dc_) {
        mem_dc_ = CreateCompatibleDC(screen_dc);
    }

    if (width != dib_width_ || height != dib_height_ || !dib_bitmap_) {
        if (old_bitmap_) {
            SelectObject(mem_dc_, old_bitmap_);
            old_bitmap_ = nullptr;
        }
        if (dib_bitmap_) {
            DeleteObject(dib_bitmap_);
            dib_bitmap_ = nullptr;
        }
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down DIB
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        dib_bits_ = nullptr;
        dib_bitmap_ = CreateDIBSection(screen_dc, &bmi, DIB_RGB_COLORS, &dib_bits_, nullptr, 0);
        if (!dib_bitmap_ || !dib_bits_) {
            ReleaseDC(nullptr, screen_dc);
            return;
        }
        old_bitmap_ = SelectObject(mem_dc_, dib_bitmap_);
        dib_width_ = width;
        dib_height_ = height;
    }

    std::memset(dib_bits_, 0, static_cast<size_t>(width) * static_cast<size_t>(height) * 4);

    {
        Gdiplus::Graphics g(mem_dc_);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);

        const float ui_scale = std::clamp(static_cast<float>(height) / 720.0f, 0.85f, 1.6f);
        Gdiplus::FontFamily font_family(L"Segoe UI");
        Gdiplus::Font header_font(&font_family, 9.5f * ui_scale, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Font title_font(&font_family, 12.0f * ui_scale, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::Font sub_font(&font_family, 10.0f * ui_scale, Gdiplus::FontStyleRegular, Gdiplus::UnitPoint);
        Gdiplus::Font tag_font(&font_family, 9.0f * ui_scale, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);

        Gdiplus::StringFormat ellipsis_fmt;
        ellipsis_fmt.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
        ellipsis_fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        // 1. Render active toasts in top-right corner.
        const Gdiplus::REAL margin = 20.0f * ui_scale;
        const Gdiplus::REAL toast_w = std::min<Gdiplus::REAL>(380.0f * ui_scale, static_cast<Gdiplus::REAL>(width) - margin * 2.0f);
        const Gdiplus::REAL toast_h = 80.0f * ui_scale;
        Gdiplus::REAL cursor_y = margin;

        const size_t visible_toasts = std::min<size_t>(2, toast_queue_.size());
        for (size_t i = 0; i < visible_toasts; ++i) {
            const auto& active = toast_queue_[i];
            if (active.start_tick_ms == 0) continue;
            const double elapsed = static_cast<double>(now_ms - active.start_tick_ms) / 1000.0;
            float alpha = 1.0f;
            float slide_px = 0.0f;
            compute_toast_anim(elapsed, active.data.duration_sec, alpha, slide_px);
            if (alpha <= 0.01f) continue;

            const Gdiplus::REAL x = static_cast<Gdiplus::REAL>(width) - toast_w - margin + slide_px * ui_scale;
            const Gdiplus::REAL y = cursor_y;

            Gdiplus::GraphicsPath card_path;
            add_rounded_rect(card_path, x, y, toast_w, toast_h, 8.0f * ui_scale);
            Gdiplus::SolidBrush bg_brush(scale_alpha(236, 13, 16, 24, alpha));
            g.FillPath(&bg_brush, &card_path);

            // Left accent bar: gold for hardcore/mastery, red for warnings, GT4 blue for standard.
            Gdiplus::Color accent_color = scale_alpha(255, 44, 107, 237, alpha);
            if (active.data.kind == ToastKind::Warning) {
                accent_color = scale_alpha(255, 214, 69, 69, alpha);
            } else if (active.data.kind == ToastKind::Mastery || active.data.hardcore) {
                accent_color = scale_alpha(255, 235, 185, 65, alpha);
            }
            Gdiplus::SolidBrush accent_brush(accent_color);
            g.FillRectangle(&accent_brush, x, y + 6.0f * ui_scale, 4.0f * ui_scale, toast_h - 12.0f * ui_scale);

            Gdiplus::Pen border_pen(scale_alpha(70, 255, 255, 255, alpha), 1.0f);
            g.DrawPath(&border_pen, &card_path);

            const Gdiplus::REAL badge_size = 56.0f * ui_scale;
            const Gdiplus::REAL badge_x = x + 14.0f * ui_scale;
            const Gdiplus::REAL badge_y = y + (toast_h - badge_size) * 0.5f;
            draw_badge_or_placeholder(g, active.data.badge_path, badge_x, badge_y, badge_size, alpha, active.data.hardcore);

            const Gdiplus::REAL text_x = badge_x + badge_size + 12.0f * ui_scale;
            const Gdiplus::REAL text_w = toast_w - (text_x - x) - 12.0f * ui_scale;

            const std::wstring w_header = utf8_to_wide(active.data.header);
            const std::wstring w_title = utf8_to_wide(active.data.title);
            const std::wstring w_sub = utf8_to_wide(active.data.subtitle);

            Gdiplus::SolidBrush header_brush(
                active.data.hardcore ? scale_alpha(255, 235, 185, 65, alpha) : scale_alpha(230, 140, 175, 250, alpha));
            Gdiplus::SolidBrush title_brush(scale_alpha(255, 255, 255, 255, alpha));
            Gdiplus::SolidBrush sub_brush(scale_alpha(225, 170, 176, 186, alpha));

            Gdiplus::RectF header_rc(text_x, y + 10.0f * ui_scale, text_w, 16.0f * ui_scale);
            Gdiplus::RectF title_rc(text_x, y + 27.0f * ui_scale, text_w, 22.0f * ui_scale);
            Gdiplus::RectF sub_rc(text_x, y + 50.0f * ui_scale, text_w, 20.0f * ui_scale);

            g.DrawString(w_header.c_str(), -1, &header_font, header_rc, &ellipsis_fmt, &header_brush);
            g.DrawString(w_title.c_str(), -1, &title_font, title_rc, &ellipsis_fmt, &title_brush);
            g.DrawString(w_sub.c_str(), -1, &sub_font, sub_rc, &ellipsis_fmt, &sub_brush);

            cursor_y += toast_h + 10.0f * ui_scale;
        }

        // 2. Render Progress Indicator in bottom-left corner when visible.
        if (progress_.visible) {
            const Gdiplus::REAL prog_w = 290.0f * ui_scale;
            const Gdiplus::REAL prog_h = 62.0f * ui_scale;
            const Gdiplus::REAL px = margin;
            const Gdiplus::REAL py = static_cast<Gdiplus::REAL>(height) - prog_h - margin;

            Gdiplus::GraphicsPath prog_path;
            add_rounded_rect(prog_path, px, py, prog_w, prog_h, 7.0f * ui_scale);
            Gdiplus::SolidBrush bg(scale_alpha(225, 13, 16, 24, 1.0f));
            g.FillPath(&bg, &prog_path);
            Gdiplus::Pen border(scale_alpha(65, 255, 255, 255, 1.0f), 1.0f);
            g.DrawPath(&border, &prog_path);

            const Gdiplus::REAL bsize = 44.0f * ui_scale;
            draw_badge_or_placeholder(g, progress_.badge_path, px + 10.0f * ui_scale, py + (prog_h - bsize) * 0.5f, bsize, 1.0f, false);

            const Gdiplus::REAL tx = px + 10.0f * ui_scale + bsize + 10.0f * ui_scale;
            const Gdiplus::REAL tw = prog_w - (tx - px) - 10.0f * ui_scale;
            const std::wstring w_title = utf8_to_wide(progress_.title);
            const std::wstring w_prog = utf8_to_wide(progress_.progress_text);

            Gdiplus::SolidBrush white_b(Gdiplus::Color(255, 255, 255, 255));
            Gdiplus::SolidBrush accent_b(Gdiplus::Color(255, 140, 185, 255));
            Gdiplus::RectF r1(tx, py + 11.0f * ui_scale, tw, 20.0f * ui_scale);
            Gdiplus::RectF r2(tx, py + 33.0f * ui_scale, tw, 18.0f * ui_scale);
            g.DrawString(w_title.c_str(), -1, &sub_font, r1, &ellipsis_fmt, &white_b);
            g.DrawString(w_prog.c_str(), -1, &header_font, r2, &ellipsis_fmt, &accent_b);
        }

        // 3. Render Challenge Indicators & Leaderboard Trackers in bottom-right corner.
        Gdiplus::REAL right_bottom_y = static_cast<Gdiplus::REAL>(height) - margin;
        if (!challenges_.empty()) {
            const Gdiplus::REAL icon_size = 38.0f * ui_scale;
            const Gdiplus::REAL spacing = 8.0f * ui_scale;
            right_bottom_y -= icon_size;
            Gdiplus::REAL cx = static_cast<Gdiplus::REAL>(width) - margin - icon_size;
            const size_t max_challenges = std::min<size_t>(6, challenges_.size());
            for (size_t i = 0; i < max_challenges; ++i) {
                draw_badge_or_placeholder(g, challenges_[i].badge_path, cx, right_bottom_y, icon_size, 0.92f, true);
                cx -= (icon_size + spacing);
            }
            right_bottom_y -= 10.0f * ui_scale;
        }

        if (!trackers_.empty()) {
            const Gdiplus::REAL pill_w = 150.0f * ui_scale;
            const Gdiplus::REAL pill_h = 30.0f * ui_scale;
            for (const auto& tracker : trackers_) {
                right_bottom_y -= pill_h;
                const Gdiplus::REAL lx = static_cast<Gdiplus::REAL>(width) - margin - pill_w;
                Gdiplus::GraphicsPath pill_path;
                add_rounded_rect(pill_path, lx, right_bottom_y, pill_w, pill_h, 6.0f * ui_scale);
                Gdiplus::SolidBrush bg(Gdiplus::Color(220, 13, 16, 24));
                g.FillPath(&bg, &pill_path);
                Gdiplus::Pen border(Gdiplus::Color(90, 235, 185, 65), 1.0f);
                g.DrawPath(&border, &pill_path);

                const std::wstring w_disp = utf8_to_wide(tracker.display);
                Gdiplus::SolidBrush text_b(Gdiplus::Color(255, 255, 255, 255));
                Gdiplus::StringFormat center_fmt;
                center_fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                center_fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                Gdiplus::RectF rc(lx + 6.0f * ui_scale, right_bottom_y, pill_w - 12.0f * ui_scale, pill_h);
                g.DrawString(w_disp.c_str(), -1, &tag_font, rc, &center_fmt, &text_b);
                right_bottom_y -= 6.0f * ui_scale;
            }
        }
    }

    POINT pt_src{0, 0};
    SIZE wnd_size{width, height};
    BLENDFUNCTION blend{};
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;

    if (!IsWindowVisible(overlay_hwnd_)) {
        ShowWindow(overlay_hwnd_, SW_SHOWNOACTIVATE);
    }
    SetWindowPos(
        overlay_hwnd_,
        HWND_TOP,
        screen_pt.x,
        screen_pt.y,
        width,
        height,
        SWP_NOACTIVATE | SWP_NOOWNERZORDER);
    UpdateLayeredWindow(overlay_hwnd_, screen_dc, &screen_pt, &wnd_size, mem_dc_, &pt_src, 0, &blend, ULW_ALPHA);
    ReleaseDC(nullptr, screen_dc);
}

#endif

} // namespace gt4::achievements
