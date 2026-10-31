// macOS port of the window part of achievements_overlay.cpp (initialize, shutdown, ensure_window, update_placement,
// tick, render). The overlay is a borderless, transparent, click-through child window of the game's NSWindow, so it
// moves, hides and orders with the game window as the owned layered window does on Windows. render() draws the same
// layout (toasts top right, progress bottom left, challenges and trackers bottom right) with CoreGraphics and CoreText
// into a premultiplied bitmap shown as the view's layer contents. AppKit calls must run on the main thread; the game
// calls tick() from VBlank on the main (EE) thread.
#include "achievements_overlay.hpp"

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <ImageIO/ImageIO.h>
#import <QuartzCore/QuartzCore.h>

#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace gt4::achievements {
namespace {

// The NSWindow behind the game's SDL window, or nil.
NSWindow* cocoa_window(HostWindow owner) {
    if (!owner) return nil;
    void* pointer = SDL_GetPointerProperty(SDL_GetWindowProperties(owner), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
    return (__bridge NSWindow*)pointer;
}

NSString* utf8(const std::string& text) {
    NSString* value = [[NSString alloc] initWithBytes:text.data() length:text.size() encoding:NSUTF8StringEncoding];
    return value ? value : @"";
}

std::string utf8_path(const std::wstring& path) { return std::filesystem::path(path).string(); }

CGColorRef rgba(uint8_t a, uint8_t r, uint8_t g, uint8_t b, float alpha_scale) {
    const CGFloat final_a = std::clamp(std::round(static_cast<float>(a) * alpha_scale), 0.0f, 255.0f) / 255.0;
    return CGColorCreateSRGB(r / 255.0, g / 255.0, b / 255.0, final_a);
}

void fill(CGContextRef context, CGPathRef path, CGColorRef color) {
    CGContextSetFillColorWithColor(context, color);
    CGContextAddPath(context, path);
    CGContextFillPath(context);
    CGColorRelease(color);
}

void stroke(CGContextRef context, CGPathRef path, CGColorRef color, CGFloat width) {
    CGContextSetStrokeColorWithColor(context, color);
    CGContextSetLineWidth(context, width);
    CGContextAddPath(context, path);
    CGContextStrokePath(context);
    CGColorRelease(color);
}

void fill_rect(CGContextRef context, CGRect rect, CGColorRef color) {
    CGContextSetFillColorWithColor(context, color);
    CGContextFillRect(context, rect);
    CGColorRelease(color);
}

// A rounded rectangle; a radius too large for the rectangle gives a plain one (as add_rounded_rect on Windows).
CGPathRef rounded(CGFloat x, CGFloat y, CGFloat w, CGFloat h, CGFloat radius) {
    if (radius * 2.0 <= 0.0 || radius * 2.0 > w || radius * 2.0 > h) return CGPathCreateWithRect(CGRectMake(x, y, w, h), nullptr);
    return CGPathCreateWithRoundedRect(CGRectMake(x, y, w, h), radius, radius, nullptr);
}

// One line of text in `box`, trimmed with an ellipsis; centred in both directions when `centred`. The context uses
// top-left coordinates (flipped), so the text matrix flips the glyphs back.
void draw_text(CGContextRef context, const std::string& text, CGFloat points, bool bold, CGColorRef color, CGRect box,
               bool centred = false) {
    CTFontRef font = CTFontCreateUIFontForLanguage(bold ? kCTFontUIFontEmphasizedSystem : kCTFontUIFontSystem, points, nullptr);
    NSDictionary* attributes = @{
        (__bridge id)kCTFontAttributeName : (__bridge id)font,
        (__bridge id)kCTForegroundColorAttributeName : (__bridge id)color,
    };
    NSAttributedString* string = [[NSAttributedString alloc] initWithString:utf8(text) attributes:attributes];
    CTLineRef line = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)string);
    NSAttributedString* dots = [[NSAttributedString alloc] initWithString:@"…" attributes:attributes];
    CTLineRef ellipsis = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)dots);
    CTLineRef shown = CTLineCreateTruncatedLine(line, box.size.width, kCTLineTruncationEnd, ellipsis);
    CTLineRef drawn = shown ? shown : line;
    CGFloat ascent = 0, descent = 0, leading = 0;
    const double width = CTLineGetTypographicBounds(drawn, &ascent, &descent, &leading);
    const CGFloat x = centred ? box.origin.x + (box.size.width - width) * 0.5 : box.origin.x;
    const CGFloat baseline = centred ? box.origin.y + (box.size.height + ascent - descent) * 0.5 : box.origin.y + ascent;
    CGContextSaveGState(context);
    CGContextSetTextMatrix(context, CGAffineTransformMakeScale(1.0, -1.0));
    CGContextSetTextPosition(context, x, baseline);
    CTLineDraw(drawn, context);
    CGContextRestoreGState(context);
    if (shown) CFRelease(shown);
    CFRelease(ellipsis);
    CFRelease(line);
    CFRelease(font);
    CGColorRelease(color);
}

void draw_badge_or_placeholder(CGContextRef context, const std::wstring& badge_path, CGFloat x, CGFloat y, CGFloat size,
                               float alpha, bool hardcore) {
    bool drawn = false;
    if (!badge_path.empty()) {
        std::error_code error;
        const std::string path = utf8_path(badge_path);
        if (std::filesystem::is_regular_file(path, error)) {
            NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
            CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
            CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
            if (image) {
                CGContextSaveGState(context);
                CGContextSetAlpha(context, alpha);
                // Images draw bottom-up: flip locally inside the badge rectangle.
                CGContextTranslateCTM(context, x, y + size);
                CGContextScaleCTM(context, 1.0, -1.0);
                CGContextSetInterpolationQuality(context, kCGInterpolationHigh);
                CGContextDrawImage(context, CGRectMake(0, 0, size, size), image);
                CGContextRestoreGState(context);
                CGImageRelease(image);
                drawn = true;
            }
            if (source) CFRelease(source);
        }
    }
    if (!drawn) {
        CGPathRef box = rounded(x, y, size, size, 6.0);
        fill(context, box, rgba(230, 26, 32, 44, alpha));
        CGPathRelease(box);
        // A stylized trophy emblem inside the placeholder box.
        const auto trophy = [&] { return hardcore ? rgba(245, 235, 185, 65, alpha) : rgba(245, 92, 148, 245, alpha); };
        const CGFloat cx = x + size * 0.5, cy = y + size * 0.42, cup_w = size * 0.42, cup_h = size * 0.34;
        fill_rect(context, CGRectMake(cx - cup_w * 0.5, cy - cup_h * 0.5, cup_w, cup_h), trophy());
        fill_rect(context, CGRectMake(cx - size * 0.06, cy + cup_h * 0.5, size * 0.12, size * 0.16), trophy());
        fill_rect(context, CGRectMake(cx - size * 0.20, cy + cup_h * 0.5 + size * 0.14, size * 0.40, size * 0.07), trophy());
    }
    CGPathRef border = rounded(x, y, size, size, 4.0);
    stroke(context, border, hardcore ? rgba(220, 235, 185, 65, alpha) : rgba(150, 255, 255, 255, alpha), 1.5);
    CGPathRelease(border);
}

} // namespace

void AchievementsOverlay::initialize(HostWindow owner_window) {
    owner_hwnd_ = owner_window;
    ensure_window(owner_window);
}

void AchievementsOverlay::shutdown() {
    toast_queue_.clear();
    challenges_.clear();
    trackers_.clear();
    progress_ = {};
    if (overlay_window_) {
        NSWindow* overlay = (__bridge_transfer NSWindow*)overlay_window_;
        [overlay.parentWindow removeChildWindow:overlay];
        [overlay orderOut:nil];
        overlay_window_ = nullptr;
    }
    if (overlay_view_) {
        (void)(__bridge_transfer NSView*)overlay_view_;
        overlay_view_ = nullptr;
    }
    owner_hwnd_ = nullptr;
}

void AchievementsOverlay::ensure_window(HostWindow owner_window) {
    NSWindow* owner = cocoa_window(owner_window);
    if (!owner) return;
    owner_hwnd_ = owner_window;
    if (overlay_window_) return;
    if (![NSThread isMainThread]) {
        static bool reported = false;
        if (!reported) std::fprintf(stderr, "[RA] overlay not created: the game window is not on the main thread\n");
        reported = true;
        return;
    }
    NSWindow* overlay = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1, 1)
                                                    styleMask:NSWindowStyleMaskBorderless
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
    overlay.opaque = NO;
    overlay.backgroundColor = NSColor.clearColor;
    overlay.hasShadow = NO;
    overlay.ignoresMouseEvents = YES;
    overlay.releasedWhenClosed = NO;
    overlay.collectionBehavior = NSWindowCollectionBehaviorFullScreenAuxiliary | NSWindowCollectionBehaviorIgnoresCycle;
    NSView* view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 1, 1)];
    view.wantsLayer = YES;
    view.layer.contentsGravity = kCAGravityResize;
    overlay.contentView = view;
    overlay_window_ = (__bridge_retained void*)overlay;
    overlay_view_ = (__bridge_retained void*)view;
}

void AchievementsOverlay::update_placement(HostWindow owner_window) {
    NSWindow* owner = cocoa_window(owner_window);
    if (!owner || !overlay_window_) return;
    NSWindow* overlay = (__bridge NSWindow*)overlay_window_;
    if (owner.miniaturized || !owner.visible || !has_visible_content()) {
        if (overlay.visible) {
            [owner removeChildWindow:overlay];
            [overlay orderOut:nil];
        }
        return;
    }
    // The overlay covers the owner's content area; as a child window it then follows every move of the owner.
    const NSRect content = [owner contentRectForFrameRect:owner.frame];
    [overlay setFrame:content display:NO];
    if (!overlay.visible || overlay.parentWindow != owner) {
        [owner addChildWindow:overlay ordered:NSWindowAbove];
        [overlay orderFront:nil];
    }
}

void AchievementsOverlay::tick(HostWindow owner_window) {
    if (!owner_window) return;
    ensure_window(owner_window);
    if (!overlay_window_) return;
    NSWindow* owner = cocoa_window(owner_window);
    NSWindow* overlay = (__bridge NSWindow*)overlay_window_;

    const uint64_t now_ms = SDL_GetTicks();

    // Advance the toast queue (up to 2 concurrent toasts), as on Windows.
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

    if (!has_visible_content() || !owner || owner.miniaturized || !owner.visible) {
        if (overlay.visible) {
            [overlay.parentWindow removeChildWindow:overlay];
            [overlay orderOut:nil];
        }
        if (!has_visible_content()) dirty_ = false;
        return;
    }

    if (toast_animating || dirty_) {
        render(owner_window, now_ms);
        dirty_ = false;
    }
}

void AchievementsOverlay::render(HostWindow owner_window, uint64_t now_ms) {
    NSWindow* owner = cocoa_window(owner_window);
    if (!owner || !overlay_window_) return;
    update_placement(owner_window);
    NSView* view = (__bridge NSView*)overlay_view_;
    // Layout in points (the window's coordinates); the bitmap is in pixels of the owner's screen.
    const NSRect content = [owner contentRectForFrameRect:owner.frame];
    const int width = std::max(1, static_cast<int>(content.size.width));
    const int height = std::max(1, static_cast<int>(content.size.height));
    const CGFloat scale = owner.backingScaleFactor > 0 ? owner.backingScaleFactor : 1.0;

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(nullptr, static_cast<size_t>(width * scale),
                                                 static_cast<size_t>(height * scale), 8, 0, space,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);
    CGColorSpaceRelease(space);
    if (!context) return;
    // Top-left origin in points, as the GDI+ layout.
    CGContextTranslateCTM(context, 0, height * scale);
    CGContextScaleCTM(context, scale, -scale);
    CGContextSetShouldAntialias(context, true);

    const float ui_scale = std::clamp(static_cast<float>(height) / 720.0f, 0.85f, 1.6f);
    // GDI+ sizes are in points at 96 DPI; the macOS layout unit is the point at 72 per inch.
    const CGFloat point = 96.0 / 72.0;

    // 1. Active toasts in the top-right corner.
    const CGFloat margin = 20.0f * ui_scale;
    const CGFloat toast_w = std::min<CGFloat>(380.0f * ui_scale, static_cast<CGFloat>(width) - margin * 2.0);
    const CGFloat toast_h = 80.0f * ui_scale;
    CGFloat cursor_y = margin;
    const size_t visible_toasts = std::min<size_t>(2, toast_queue_.size());
    for (size_t i = 0; i < visible_toasts; ++i) {
        const auto& active = toast_queue_[i];
        if (active.start_tick_ms == 0) continue;
        const double elapsed = static_cast<double>(now_ms - active.start_tick_ms) / 1000.0;
        float alpha = 1.0f, slide_px = 0.0f;
        compute_toast_anim(elapsed, active.data.duration_sec, alpha, slide_px);
        if (alpha <= 0.01f) continue;
        const CGFloat x = static_cast<CGFloat>(width) - toast_w - margin + slide_px * ui_scale;
        const CGFloat y = cursor_y;

        CGPathRef card = rounded(x, y, toast_w, toast_h, 8.0f * ui_scale);
        fill(context, card, rgba(236, 13, 16, 24, alpha));
        // Left accent bar: gold for hardcore/mastery, red for warnings, GT4 blue for standard.
        CGColorRef accent = rgba(255, 44, 107, 237, alpha);
        if (active.data.kind == ToastKind::Warning) {
            CGColorRelease(accent);
            accent = rgba(255, 214, 69, 69, alpha);
        } else if (active.data.kind == ToastKind::Mastery || active.data.hardcore) {
            CGColorRelease(accent);
            accent = rgba(255, 235, 185, 65, alpha);
        }
        fill_rect(context, CGRectMake(x, y + 6.0f * ui_scale, 4.0f * ui_scale, toast_h - 12.0f * ui_scale), accent);
        stroke(context, card, rgba(70, 255, 255, 255, alpha), 1.0);
        CGPathRelease(card);

        const CGFloat badge_size = 56.0f * ui_scale;
        const CGFloat badge_x = x + 14.0f * ui_scale;
        const CGFloat badge_y = y + (toast_h - badge_size) * 0.5;
        draw_badge_or_placeholder(context, active.data.badge_path, badge_x, badge_y, badge_size, alpha, active.data.hardcore);

        const CGFloat text_x = badge_x + badge_size + 12.0f * ui_scale;
        const CGFloat text_w = toast_w - (text_x - x) - 12.0f * ui_scale;
        draw_text(context, active.data.header, 9.5f * ui_scale * point, true,
                  active.data.hardcore ? rgba(255, 235, 185, 65, alpha) : rgba(230, 140, 175, 250, alpha),
                  CGRectMake(text_x, y + 10.0f * ui_scale, text_w, 16.0f * ui_scale));
        draw_text(context, active.data.title, 12.0f * ui_scale * point, true, rgba(255, 255, 255, 255, alpha),
                  CGRectMake(text_x, y + 27.0f * ui_scale, text_w, 22.0f * ui_scale));
        draw_text(context, active.data.subtitle, 10.0f * ui_scale * point, false, rgba(225, 170, 176, 186, alpha),
                  CGRectMake(text_x, y + 50.0f * ui_scale, text_w, 20.0f * ui_scale));
        cursor_y += toast_h + 10.0f * ui_scale;
    }

    // 2. Progress indicator in the bottom-left corner.
    if (progress_.visible) {
        const CGFloat prog_w = 290.0f * ui_scale, prog_h = 62.0f * ui_scale;
        const CGFloat px = margin, py = static_cast<CGFloat>(height) - prog_h - margin;
        CGPathRef box = rounded(px, py, prog_w, prog_h, 7.0f * ui_scale);
        fill(context, box, rgba(225, 13, 16, 24, 1.0f));
        stroke(context, box, rgba(65, 255, 255, 255, 1.0f), 1.0);
        CGPathRelease(box);
        const CGFloat bsize = 44.0f * ui_scale;
        draw_badge_or_placeholder(context, progress_.badge_path, px + 10.0f * ui_scale, py + (prog_h - bsize) * 0.5, bsize,
                                  1.0f, false);
        const CGFloat tx = px + 10.0f * ui_scale + bsize + 10.0f * ui_scale;
        const CGFloat tw = prog_w - (tx - px) - 10.0f * ui_scale;
        draw_text(context, progress_.title, 10.0f * ui_scale * point, false, rgba(255, 255, 255, 255, 1.0f),
                  CGRectMake(tx, py + 11.0f * ui_scale, tw, 20.0f * ui_scale));
        draw_text(context, progress_.progress_text, 9.5f * ui_scale * point, true, rgba(255, 140, 185, 255, 1.0f),
                  CGRectMake(tx, py + 33.0f * ui_scale, tw, 18.0f * ui_scale));
    }

    // 3. Challenge indicators and leaderboard trackers in the bottom-right corner.
    CGFloat right_bottom_y = static_cast<CGFloat>(height) - margin;
    if (!challenges_.empty()) {
        const CGFloat icon_size = 38.0f * ui_scale, spacing = 8.0f * ui_scale;
        right_bottom_y -= icon_size;
        CGFloat cx = static_cast<CGFloat>(width) - margin - icon_size;
        const size_t max_challenges = std::min<size_t>(6, challenges_.size());
        for (size_t i = 0; i < max_challenges; ++i) {
            draw_badge_or_placeholder(context, challenges_[i].badge_path, cx, right_bottom_y, icon_size, 0.92f, true);
            cx -= icon_size + spacing;
        }
        right_bottom_y -= 10.0f * ui_scale;
    }
    for (const auto& tracker : trackers_) {
        const CGFloat pill_w = 150.0f * ui_scale, pill_h = 30.0f * ui_scale;
        right_bottom_y -= pill_h;
        const CGFloat lx = static_cast<CGFloat>(width) - margin - pill_w;
        CGPathRef pill = rounded(lx, right_bottom_y, pill_w, pill_h, 6.0f * ui_scale);
        fill(context, pill, rgba(220, 13, 16, 24, 1.0f));
        stroke(context, pill, rgba(90, 235, 185, 65, 1.0f), 1.0);
        CGPathRelease(pill);
        draw_text(context, tracker.display, 9.0f * ui_scale * point, true, rgba(255, 255, 255, 255, 1.0f),
                  CGRectMake(lx + 6.0f * ui_scale, right_bottom_y, pill_w - 12.0f * ui_scale, pill_h), true);
        right_bottom_y -= 6.0f * ui_scale;
    }

    CGImageRef image = CGBitmapContextCreateImage(context);
    CGContextRelease(context);
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    view.layer.contentsScale = scale;
    view.layer.contents = (__bridge id)image;
    [CATransaction commit];
    CGImageRelease(image);
}

} // namespace gt4::achievements
