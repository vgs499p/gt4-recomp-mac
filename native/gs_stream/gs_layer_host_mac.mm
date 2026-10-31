// SPDX-License-Identifier: MIT
// macOS: shows the GS process picture in the game window (GT4_RENDERER=pcsx2).
//
// Windows makes the GS process window a child of the game window. macOS has no cross-process child windows, so gt4-gs
// makes its Metal layer the root of a remote Core Animation context and sends the context id back (WINDOW restored,
// WindowPayload.reserved2). This file hosts that context with a CALayerHost in a view laid over the game window's
// content: the window server composites the remote layer directly, with no copy. The view ignores the mouse, so the
// SDL window keeps every event, the focus and the cursor policy.
//
// CALayerHost is a private QuartzCore class (browsers host their GPU process the same way); it is looked up at run time
// and a missing class fails cleanly.
#include "gs_layer_host_mac.hpp"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>

#include <SDL3/SDL.h>

#include <cstdio>

@interface CALayerHost : CALayer
@property uint32_t contextId;
@end

// A transparent view that never takes the mouse: hit testing falls through to the SDL view below.
@interface GT4PictureHostView : NSView
@end

@implementation GT4PictureHostView
- (NSView *)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}
- (BOOL)isOpaque {
  return NO;
}
@end

namespace gt4::gs_stream {
namespace {
GT4PictureHostView *g_host_view = nil;
CALayerHost *g_layer_host = nil;
std::uint64_t g_hosted_id = 0;

NSView *content_view(SDL_Window *window) {
  auto *ns_window = (__bridge NSWindow *)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
                                                                SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
  return ns_window ? ns_window.contentView : nil;
}
} // namespace

bool host_presentation(SDL_Window *window, std::uint64_t context_id) {
  if (context_id == g_hosted_id)
    return true;
  Class host_class = NSClassFromString(@"CALayerHost");
  NSView *content = content_view(window);
  if (!host_class || !content || context_id > UINT32_MAX) {
    std::fprintf(stderr, "[gs-ipc] cannot host the GS picture layer %llu (%s)\n",
                 static_cast<unsigned long long>(context_id), host_class ? "no window view" : "no CALayerHost");
    return false;
  }
  if (!g_host_view) {
    g_host_view = [[GT4PictureHostView alloc] initWithFrame:content.bounds];
    g_host_view.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    g_host_view.wantsLayer = YES;
    // Above the SDL Metal view, so the GS picture covers the built-in renderer's (black) output.
    [content addSubview:g_host_view positioned:NSWindowAbove relativeTo:nil];
  }
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  [g_layer_host removeFromSuperlayer];
  g_layer_host = [host_class layer];
  g_layer_host.contextId = static_cast<uint32_t>(context_id);
  g_layer_host.anchorPoint = CGPointZero;
  g_layer_host.position = CGPointZero;
  // The remote root layer is placed at its own origin in the GS process; flipping keeps its top edge at the top.
  g_host_view.layer.geometryFlipped = YES;
  [g_host_view.layer addSublayer:g_layer_host];
  [CATransaction commit];
  g_hosted_id = context_id;
  std::fprintf(stderr, "[gs-ipc] GS picture layer %llu hosted in the game window\n",
               static_cast<unsigned long long>(context_id));
  return true;
}

void remove_presentation() {
  [g_layer_host removeFromSuperlayer];
  g_layer_host = nil;
  [g_host_view removeFromSuperview];
  g_host_view = nil;
  g_hosted_id = 0;
}
} // namespace gt4::gs_stream
