// macOS port of window.cpp: the game window on SDL3. The structure follows window.cpp function for function, so a
// change there maps to the same place here; only the window system differs:
//   - the window is an SDL window with a Metal layer (native_vulkan.cpp presents on it, VK_EXT_metal_surface);
//   - SDL events replace the Win32 message loop (close, F7/F8/F9, F11 and Alt+Enter, focus, move and resize);
//   - keyboard bindings keep their Windows virtual-key codes (controls.cpp) and are read through SDL scancodes;
//   - GT4_MONITOR names an SDL display (its name as macOS reports it) instead of a GDI device;
//   - sizes are in pixels (high pixel density window); the memory card lock is flock() on the .lock file.
// SDL requires its video and event calls on the main thread: run_window() and the guest (whose VBlank calls
// on_vblank) run on the main thread, as on Windows.
#include "window.hpp"
#include "race_display.hpp"
#include "race_widescreen.hpp"
#include "display_aspect.hpp"
#include "ps2/memory.hpp"
#include "gpu.hpp"
#include "gs/gs.hpp"
#include "host_io.hpp"
#include "native_vulkan.hpp"
#include "ps2/system.hpp"
#include "perf_probe.hpp"
#include "frame_clock.hpp"
#include "pacing.hpp"
#include "window_mode.hpp"
#include "cursor_autohide.hpp"
#include "achievements.hpp"
#ifdef GT4_GS_STREAM_CLIENT
#include "gs_stream/gs_stream_client.hpp"
#endif
#ifdef GT4_MTVU_MEASURE
#include "mtvu_measure.hpp"
#endif
#ifdef GT4_SHAPE_CAPTURE
#include "shape_capture_control.hpp"
#include "shape_observer_adapter.hpp"
#endif

#include <SDL3/SDL.h>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr const char window_title[] = "Gran Turismo 4";
SDL_Window *game_window = nullptr;
bool surface_attached = false;
bool closing = false;
ControllerInput *controller = nullptr;
gt4::window_mode::ToggleState fullscreen_state;
#ifdef GT4_SHAPE_CAPTURE
std::unique_ptr<gt4::observation::CaptureControl> capture;
#endif
// Pacing state (see pacing.hpp). The clock and histogram run in both modes;
// the release grid only in paced mode.
gt4::pacing::Mode pacing_mode = gt4::pacing::Mode::paced;
std::unique_ptr<FrameClock> frame_clock;
std::unique_ptr<gt4::pacing::FramePacer> frame_pacer;
gt4::pacing::Diagnostics pacing_diagnostics;
// Session totals at the last F7 start or F8 report; the next report prints the difference.
gt4::pacing::Counters counters_at_last_report;
// GT4_FORCE_480P (slice164): set at window start; counts guest VBlank writes of the race display index.
bool force_480p_enabled = false;
gt4::display::Force480pState force_480p_state;
AudioOutput *audio_output = nullptr;
// Input bindings (controls.hpp). open_window loads them before the window exists.
gt4::controls::ControlMap active_controls;
// TASK-075: gamepad backend (GT4_INPUT=sdl; xinput is Windows only). open_window loads it.
gt4::pad::Backend input_backend = gt4::pad::Backend::sdl;
// A keyboard binding resolved to the SDL scancodes that hold it (two for Shift, Ctrl and Alt: left and right).
struct ScancodeBinding {
  SDL_Scancode first = SDL_SCANCODE_UNKNOWN, second = SDL_SCANCODE_UNKNOWN;
  std::uint8_t virtual_key = 0;
  std::uint16_t button_mask = 0;
};
std::vector<ScancodeBinding> keyboard_keys;
// TASK-060: cursor auto-hide over this window's picture (GT4_CURSOR_AUTOHIDE, cursor_autohide.hpp).
gt4::cursor::AutoHide cursor_autohide;

// Windows virtual-key codes of controls.cpp (the binding file format) used below.
constexpr std::uint8_t vk_back = 0x08, vk_tab = 0x09, vk_return = 0x0D, vk_shift = 0x10, vk_control = 0x11,
                       vk_menu = 0x12, vk_escape = 0x1B, vk_space = 0x20, vk_prior = 0x21, vk_next = 0x22,
                       vk_end = 0x23, vk_home = 0x24, vk_left = 0x25, vk_up = 0x26, vk_right = 0x27, vk_down = 0x28,
                       vk_insert = 0x2D, vk_delete = 0x2E, vk_numpad0 = 0x60, vk_f1 = 0x70;

// The SDL scancodes of a virtual key. Letters and digits follow the keyboard layout (as GetAsyncKeyState with a VK
// does); the other keys are fixed positions.
ScancodeBinding resolve_scancodes(const gt4::controls::KeyboardBinding &binding) {
  ScancodeBinding resolved;
  resolved.virtual_key = binding.virtual_key;
  resolved.button_mask = binding.button_mask;
  const std::uint8_t vk = binding.virtual_key;
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
    const SDL_Keycode key = vk >= 'A' && vk <= 'Z' ? static_cast<SDL_Keycode>(SDLK_A + (vk - 'A'))
                                                     : static_cast<SDL_Keycode>(SDLK_0 + (vk - '0'));
    resolved.first = SDL_GetScancodeFromKey(key, nullptr);
  } else if (vk >= vk_f1 && vk < vk_f1 + 12) {
    resolved.first = static_cast<SDL_Scancode>(SDL_SCANCODE_F1 + (vk - vk_f1));
  } else if (vk >= vk_numpad0 && vk <= vk_numpad0 + 9) {
    resolved.first = vk == vk_numpad0 ? SDL_SCANCODE_KP_0
                                      : static_cast<SDL_Scancode>(SDL_SCANCODE_KP_1 + (vk - vk_numpad0 - 1));
  } else {
    switch (vk) {
    case vk_return: resolved.first = SDL_SCANCODE_RETURN; resolved.second = SDL_SCANCODE_KP_ENTER; break;
    case vk_back: resolved.first = SDL_SCANCODE_BACKSPACE; break;
    case vk_tab: resolved.first = SDL_SCANCODE_TAB; break;
    case vk_shift: resolved.first = SDL_SCANCODE_LSHIFT; resolved.second = SDL_SCANCODE_RSHIFT; break;
    case vk_control: resolved.first = SDL_SCANCODE_LCTRL; resolved.second = SDL_SCANCODE_RCTRL; break;
    case vk_menu: resolved.first = SDL_SCANCODE_LALT; resolved.second = SDL_SCANCODE_RALT; break;
    case vk_escape: resolved.first = SDL_SCANCODE_ESCAPE; break;
    case vk_space: resolved.first = SDL_SCANCODE_SPACE; break;
    case vk_prior: resolved.first = SDL_SCANCODE_PAGEUP; break;
    case vk_next: resolved.first = SDL_SCANCODE_PAGEDOWN; break;
    case vk_end: resolved.first = SDL_SCANCODE_END; break;
    case vk_home: resolved.first = SDL_SCANCODE_HOME; break;
    case vk_left: resolved.first = SDL_SCANCODE_LEFT; break;
    case vk_up: resolved.first = SDL_SCANCODE_UP; break;
    case vk_right: resolved.first = SDL_SCANCODE_RIGHT; break;
    case vk_down: resolved.first = SDL_SCANCODE_DOWN; break;
    case vk_insert: resolved.first = SDL_SCANCODE_INSERT; break;
    case vk_delete: resolved.first = SDL_SCANCODE_DELETE; break;
    default: break;
    }
  }
  if (resolved.first == SDL_SCANCODE_UNKNOWN)
    std::fprintf(stderr, "[controls] key 0x%02X has no macOS key; binding ignored\n", vk);
  return resolved;
}

// The window's drawable size in pixels (the swapchain size).
void pixel_size(int &width, int &height) {
  width = height = 0;
  SDL_GetWindowSizeInPixels(game_window, &width, &height);
}

// One cursor sample per VBlank. The cursor is changed only while it is over this window's content and this window
// has the mouse focus.
void update_cursor() {
  if (!game_window || !cursor_autohide.policy().auto_hide)
    return;
  float x = 0, y = 0;
  SDL_GetGlobalMouseState(&x, &y);
  bool over_picture = SDL_GetMouseFocus() == game_window;
  if (over_picture) {
    float local_x = 0, local_y = 0;
    SDL_GetMouseState(&local_x, &local_y);
    int width = 0, height = 0;
    SDL_GetWindowSize(game_window, &width, &height);
    over_picture = local_x >= 0 && local_y >= 0 && local_x < width && local_y < height;
  }
  const bool was_hidden = cursor_autohide.hidden();
  const bool hide = cursor_autohide.update(static_cast<std::int32_t>(std::lround(x)), static_cast<std::int32_t>(std::lround(y)),
                                           SDL_GetTicks(), over_picture);
  if (hide && !was_hidden)
    SDL_HideCursor();
  else if (!hide && was_hidden)
    SDL_ShowCursor();
}

// TASK-080 addendum: the display named by GT4_MONITOR (its SDL name), or the primary display when none is named or
// the named one is not connected.
SDL_DisplayID chosen_monitor(const std::string &device) {
  const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
  if (device.empty())
    return primary;
  int count = 0;
  SDL_DisplayID *displays = SDL_GetDisplays(&count);
  SDL_DisplayID found = 0;
  for (int index = 0; displays && index < count && !found; ++index) {
    const char *name = SDL_GetDisplayName(displays[index]);
    if (name && device == name)
      found = displays[index];
  }
  SDL_free(displays);
  if (found)
    return found;
  std::fprintf(stderr, "[window] GT4_MONITOR %s is not connected; using the primary monitor\n", device.c_str());
  return primary;
}

void apply_pad(const PadState &pad) {
  ps2::set_pad_buttons(pad.buttons);
  for (int axis = 0; axis < 4; ++axis)
    ps2::set_pad_axis(axis, pad.axes[axis]);
  for (int button = 0; button < 16; ++button)
    ps2::set_pad_pressure(button, pad.pressures[button]);
}

gt4::pacing::Counters pacing_counters() {
  gt4::pacing::Counters counters;
  if (frame_pacer)
    counters.late_frames = frame_pacer->late_frames();
  if (audio_output) {
    counters.audio_underruns = audio_output->underruns();
    counters.ring_overflow_frames = audio_output->overflow_frames();
  }
  return counters;
}

// Borderless fullscreen on the display that holds the window (desktop mode, no separate Space, so Alt+Enter and F11
// toggle at once as on Windows). The toggle state keeps the windowed placement; the swapchain follows the new pixel
// size through SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED.
void set_fullscreen(bool enabled) {
  using gt4::window_mode::Placement;
  if (enabled) {
    int x = 0, y = 0, width = 0, height = 0;
    SDL_GetWindowPosition(game_window, &x, &y);
    SDL_GetWindowSize(game_window, &width, &height);
    if (!fullscreen_state.enter(Placement{{x, y, x + width, y + height}, 0}))
      return;
    SDL_SetWindowFullscreenMode(game_window, nullptr);
    SDL_SetWindowFullscreen(game_window, true);
    return;
  }
  Placement restored{};
  if (!fullscreen_state.leave(restored))
    return;
  SDL_SetWindowFullscreen(game_window, false);
  const auto &bounds = restored.bounds;
  SDL_SetWindowSize(game_window, bounds.right - bounds.left, bounds.bottom - bounds.top);
  SDL_SetWindowPosition(game_window, bounds.left, bounds.top);
}

// The client size changed (resize, fullscreen, display move): the aspect setting, the overlay and the swapchain.
void on_pixel_size_changed() {
  int width = 0, height = 0;
  pixel_size(width, height);
  gt4::achievements::on_window_pos_changed(game_window);
  // TASK-080: GT4_DISPLAY_ASPECT=auto follows the client area (windowed, fullscreen, Alt+Enter, resize).
  gt4::display_aspect::set_window_client_size(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
  if (surface_attached)
    ps2::gs::resize_window(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
}

// One SDL event, as window_procedure handles one message. Returns false once the window is closing.
bool handle_event(const SDL_Event &event) {
  if (event.type == SDL_EVENT_KEY_DOWN) {
    const SDL_Scancode key = event.key.scancode;
    const bool repeat = event.key.repeat;
#ifdef GT4_PERF_PROBE
    if (key == SDL_SCANCODE_F7 || key == SDL_SCANCODE_F8 || key == SDL_SCANCODE_F9) {
      if (key == SDL_SCANCODE_F7) {
        // The F7/F8 interval starts here: histogram and counter deltas restart.
        counters_at_last_report = pacing_counters();
        pacing_diagnostics.reset();
#ifdef GT4_MTVU_MEASURE
        if (!repeat)
          gt4::mtvu::request_interval_reset();
#endif
      }
      const auto request = key == SDL_SCANCODE_F7 ? gt4::perf::Request::start :
          key == SDL_SCANCODE_F8 ? gt4::perf::Request::report : gt4::perf::Request::toggle;
      gt4::perf::Probe::shared().request(request, repeat);
      return true;
    }
#endif
#ifdef GT4_SHAPE_CAPTURE
    if (key == SDL_SCANCODE_F8) {
      if (capture) capture->request_toggle(repeat);
      return true;
    }
#endif
    // Alt+Enter (Option+Return) and F11 toggle borderless fullscreen. Auto-repeat is ignored.
    const bool alt = (event.key.mod & SDL_KMOD_ALT) != 0;
    if (!repeat && ((key == SDL_SCANCODE_RETURN && alt) || key == SDL_SCANCODE_F11)) {
      set_fullscreen(!fullscreen_state.fullscreen());
      return true;
    }
    return true;
  }
  if (event.type == SDL_EVENT_QUIT ||
      (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(game_window))) {
#ifdef GT4_GS_STREAM_CLIENT
    // TASK-038: with GT4_RENDERER=pcsx2 halt the guest; the window is destroyed in cleanup(), after
    // gs_stream::finish() has shut the GS process down (or ended it).
    if (gt4::gs_stream::stream_renderer()) {
      closing = true;
      ps2::halt("window closed");
      return false;
    }
#endif
    ps2::halt("window closed");
    closing = true;
    return false;
  }
  if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && controller) {
    controller->stop();
    if (ps2::g_options.presses.empty())
      apply_pad({});
    return true;
  }
  if (event.type == SDL_EVENT_WINDOW_MOVED) {
    gt4::achievements::on_window_pos_changed(game_window);
    return true;
  }
  if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
    on_pixel_size_changed();
    return true;
  }
  return true;
}

// TASK-080 presentation sink (native/display_aspect.hpp, EE thread): CoreGS 1.1 fills a rectangle of the race
// projection ratio; the PCSX2 GS process gets CONTROL display_aspect (protocol 1.3; only a 1.3 consumer is sent it).
void present_race_view(bool fill, double ratio) {
#ifdef GT4_GS_STREAM_CLIENT
  if (gt4::gs_stream::stream_renderer()) {
    gt4::gs_stream::set_display_aspect_mode(fill ? gt4::gs_stream::DisplayAspectMode::fill
                                                 : gt4::gs_stream::DisplayAspectMode::game_aspect,
                                            fill ? gt4::gs_stream::aspect_millionths(ratio) : 0);
    return;
  }
#endif
  gt4::vulkan::set_race_view_presentation(fill, ratio);
}

bool pump_messages() {
  SDL_Event event;
  while (SDL_PollEvent(&event)) {
    if (!handle_event(event))
      return false;
  }
  return !closing;
}

// Releases the guest frame. Paced mode waits on the wall-clock grid first, so
// each VBlank comes at the donor's 60 Hz rate and audio never sets the pace.
// The interval histogram is recorded in both modes.
void release_frame() {
  if (pacing_mode == gt4::pacing::Mode::paced) {
    if (!frame_pacer->anchored())
      frame_pacer->anchor(frame_clock->now());
    // TASK-084 step 0: the sleep is its own category, so F8 ee_dispatch and unattributed mean work, not sleep.
    GT4_PERF_SCOPE(gt4::perf::Category::pacing_wait);
    frame_clock->wait_until(frame_pacer->deadline());
  }
  const std::int64_t released = frame_clock->now();
  if (pacing_mode == gt4::pacing::Mode::paced)
    frame_pacer->release(released);
  pacing_diagnostics.record_release(released, frame_clock->frequency());
}

void on_vblank() {
#ifdef GT4_PERF_PROBE
  gt4::perf::Probe::shared().frame_boundary(
      {ps2::now(), ps2::primitives_drawn(), ps2::vu1_interpreted_calls()});
#endif
  release_frame();
  GT4_PERF_SCOPE(gt4::perf::Category::host_vblank);
  if (!pump_messages())
    return;
  update_cursor();
  if (force_480p_enabled)
    gt4::display::force_race_480p_at_vblank(ps2::g_ram, ps2::kRamSize, force_480p_state);
#ifdef GT4_SHAPE_CAPTURE
  if (capture)
    capture->service();
#endif
  std::uint16_t buttons = 0;
  const bool focused = SDL_GetKeyboardFocus() == game_window;
  if (focused) {
    // Keyboard bindings come from the controls table (defaults match the old fixed map).
    // Alt+Enter toggles fullscreen: while Alt is held, Enter is not Start.
    const bool *keys = SDL_GetKeyboardState(nullptr);
    const bool alt_held = (SDL_GetModState() & SDL_KMOD_ALT) != 0;
    for (const auto &binding : keyboard_keys) {
      if (binding.virtual_key == vk_return && alt_held)
        continue;
      if ((binding.first != SDL_SCANCODE_UNKNOWN && keys[binding.first]) ||
          (binding.second != SDL_SCANCODE_UNKNOWN && keys[binding.second]))
        buttons |= binding.button_mask;
    }
  }
  if (ps2::g_options.presses.empty()) {
    apply_pad(controller->poll(focused, buttons));
    if (focused)
      controller->rumble(ps2::pad_rumble());
  }
  if (audio_output) {
    // Paced mode: move ring data to the device without waiting.
    audio_output->pump();
    pacing_diagnostics.record_queue_frames(audio_output->queued_frames());
    if (audio_output->mode() == gt4::pacing::Mode::paced)
      pacing_diagnostics.record_ratio(audio_output->resampler_ratio());
  }
  gt4::achievements::on_vblank(game_window, ps2::g_ram, ps2::kRamSize, ps2::g_spr, ps2::kScratchpadSize);
  ps2::gs::present();
}

// The memory card lock: an exclusive flock on <card>.lock, held while the game runs (Windows: a share-none handle).
class CardLock {
 public:
  explicit CardLock(const std::string &path) : descriptor_(open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644)) {
    if (descriptor_ < 0 || flock(descriptor_, LOCK_EX | LOCK_NB) != 0) {
      const int error = errno;
      if (descriptor_ >= 0)
        close(descriptor_);
      throw std::runtime_error("Cannot exclusively lock game memory card: " + std::string(std::strerror(error)));
    }
  }
  ~CardLock() { close(descriptor_); }
  CardLock(const CardLock &) = delete;
  CardLock &operator=(const CardLock &) = delete;

 private:
  int descriptor_;
};
} // namespace

bool open_window(bool fullscreen) {
  // The launcher passes GT4_FULLSCREEN (its Display setting, on by default). An invalid value stops the start.
  bool start_fullscreen = fullscreen;
  try {
    start_fullscreen = start_fullscreen || gt4::window_mode::parse_start_fullscreen(std::getenv("GT4_FULLSCREEN"));
  } catch (const std::invalid_argument &error) {
    std::fprintf(stderr, "[window] %s, game not started\n", error.what());
    return false;
  }
  // An invalid GT4_CONTROLS_FILE stops the start here, before any window or guest work.
  try {
    active_controls = gt4::controls::load_controls_from_environment();
  } catch (const gt4::controls::ConfigError &error) {
    std::fprintf(stderr, "[controls] invalid controls file, game not started: %s\n",
                 error.what());
    return false;
  }
  // TASK-080: an invalid GT4_DISPLAY_ASPECT, GT4_OUTPUT_SIZE or GT4_ASPECT_FOV_EXTRA stops the start here (prints and
  // exits).
  gt4::display_aspect::configured_setting();
  const gt4::display_aspect::OutputSize output = gt4::display_aspect::configured_output_size();
  std::string monitor_device;
  try {
    monitor_device = gt4::window_mode::parse_monitor(std::getenv("GT4_MONITOR"));
  } catch (const std::invalid_argument &error) {
    std::fprintf(stderr, "[window] %s, game not started\n", error.what());
    return false;
  }
  try {
    input_backend = gt4::pad::parse_backend(std::getenv("GT4_INPUT"));
  } catch (const std::invalid_argument &error) {
    std::fprintf(stderr, "[input] %s, game not started\n", error.what());
    return false;
  }
  if (input_backend != gt4::pad::Backend::sdl) {
    std::fprintf(stderr, "[input] GT4_INPUT=xinput is only available on Windows, game not started\n");
    return false;
  }
  std::fprintf(stderr, "[input] backend %s\n", gt4::pad::backend_name(input_backend));
  const char *controls_file = std::getenv("GT4_CONTROLS_FILE");
  std::fprintf(stderr, "[controls] %s\n",
               controls_file && *controls_file ? controls_file : "default bindings");
  // Borderless fullscreen without a separate Space, as on Windows.
  SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_SPACES, "0");
  if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
    throw std::runtime_error(std::string("SDL_InitSubSystem(SDL_INIT_VIDEO) failed: ") + SDL_GetError());
  keyboard_keys.clear();
  for (const auto &binding : gt4::controls::keyboard_bindings(active_controls))
    keyboard_keys.push_back(resolve_scancodes(binding));
  // Without GT4_MONITOR and GT4_OUTPUT_SIZE (headless tools, older launchers): a 960x540 client area where the system
  // puts it, as before. With them (TASK-080 addendum): a client area of the output size in pixels (shrunk with its
  // shape when it does not fit the usable area) centred on the chosen display; fullscreen covers that display.
  const bool placed = output.known() || !monitor_device.empty();
  const SDL_DisplayID monitor = placed ? chosen_monitor(monitor_device) : SDL_GetPrimaryDisplay();
  // Points to pixels on this display (2 on Retina); window sizes are in points, the output size in pixels.
  float scale = SDL_GetDisplayContentScale(monitor);
  if (!(scale > 0.0f))
    scale = 1.0f;
  int client_width = 960, client_height = 540;  // points
  int left = static_cast<int>(SDL_WINDOWPOS_UNDEFINED_DISPLAY(monitor));
  int top = left;
  SDL_Rect work{};
  if (placed) {
    if (!SDL_GetDisplayUsableBounds(monitor, &work))
      throw std::runtime_error(std::string("Cannot read the chosen monitor: ") + SDL_GetError());
    // The usable area in pixels minus the title bar (about 28 points); the frame has no side borders.
    const auto title_bar = static_cast<std::int32_t>(std::lround(28.0f * scale));
    const auto client = gt4::window_mode::fit_client(
        output.known() ? output.width : static_cast<std::uint32_t>(std::lround(960 * scale)),
        output.known() ? output.height : static_cast<std::uint32_t>(std::lround(540 * scale)),
        static_cast<std::int32_t>(std::lround(work.w * scale)),
        static_cast<std::int32_t>(std::lround(work.h * scale)) - title_bar);
    client_width = std::max(1, static_cast<int>(std::lround(client.width / scale)));
    client_height = std::max(1, static_cast<int>(std::lround(client.height / scale)));
    const auto box = gt4::window_mode::centred({work.x, work.y, work.x + work.w, work.y + work.h}, client_width,
                                               client_height);
    left = box.left;
    top = box.top;
  }
  SDL_PropertiesID properties = SDL_CreateProperties();
  SDL_SetStringProperty(properties, SDL_PROP_WINDOW_CREATE_TITLE_STRING, window_title);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_X_NUMBER, left);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_Y_NUMBER, top);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, client_width);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, client_height);
  SDL_SetNumberProperty(properties, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER,
                        SDL_WINDOW_METAL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  game_window = SDL_CreateWindowWithProperties(properties);
  SDL_DestroyProperties(properties);
  if (!game_window)
    throw std::runtime_error(std::string("Cannot create native game window: ") + SDL_GetError());
  // Start in borderless fullscreen on the window's display: the same path as Alt+Enter and F11.
  if (start_fullscreen) {
    set_fullscreen(true);
    SDL_SyncWindow(game_window);
  }
  int width = 0, height = 0;
  pixel_size(width, height);
  gt4::display_aspect::set_window_client_size(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
  std::fprintf(stderr, "[window] %s start (GT4_FULLSCREEN)\n", start_fullscreen ? "fullscreen" : "windowed");
  if (placed) {
    SDL_Rect area{};
    SDL_GetDisplayBounds(monitor, &area);
    const char *name = SDL_GetDisplayName(monitor);
    std::fprintf(stderr, "[window] monitor %s (%dx%d points at %d,%d, scale %.2f), output size %ux%u, client %dx%d\n",
                 monitor_device.empty() ? (name ? name : "primary") : monitor_device.c_str(), area.w, area.h, area.x,
                 area.y, scale, output.width, output.height, width, height);
  }
  return true;
}

#ifdef GT4_MEMCARD_FLUSH_OVERLAY
namespace ps2::iop::sio2 {
void flush_cards_now(); // memcard capsule sio2.cpp (scripts/prepare_memcard_flush.py)
}
#endif

int run_window(const std::function<int()> &emulate) {
  ControllerInput input(active_controls, input_backend);
  controller = &input;
  const auto cleanup = [&]() {
    gt4::achievements::shutdown();
#ifdef GT4_PERF_PROBE
    gt4::perf::Probe::shared().set_report_section({});
    gt4::perf::Probe::shared().finish();
#endif
    audio_output = nullptr;
    frame_pacer.reset();
    frame_clock.reset();
#ifdef GT4_SHAPE_CAPTURE
    if (capture) {
      capture->finish();
      capture.reset();
    }
#endif
    ps2::set_audio_sink({});
    ps2::g_options.on_vblank = {};
    input.stop();
    controller = nullptr;
    surface_attached = false;
    if (game_window) {
      SDL_ShowCursor();
      SDL_DestroyWindow(game_window);
    }
    game_window = nullptr;
  };
  try {
    force_480p_enabled = gt4::display::parse_force_480p(std::getenv("GT4_FORCE_480P"));
    force_480p_state = {};
    if (force_480p_enabled)
      std::fprintf(stderr, "[display] GT4_FORCE_480P=1: race display index held at 480p (guest VBlank)\n");
    // TASK-079: an invalid GT4_FORCE_WIDESCREEN stops the start here (the hold itself runs at guest VBlank).
    gt4::display::widescreen_hold_enabled();
    pacing_mode = gt4::pacing::parse_mode(std::getenv("GT4_PACING"));
    cursor_autohide = gt4::cursor::AutoHide(gt4::cursor::parse_policy(std::getenv("GT4_CURSOR_AUTOHIDE")));
    std::fprintf(stderr, "[window] cursor auto-hide %s (GT4_CURSOR_AUTOHIDE; %u ms without movement over the picture)\n",
                 cursor_autohide.policy().auto_hide ? "on" : "off", cursor_autohide.policy().hide_delay_ms);
#ifdef GT4_GS_STREAM_CLIENT
    gt4::gs_stream::set_cursor_policy(cursor_autohide.policy().auto_hide, cursor_autohide.policy().hide_delay_ms);
#endif
    frame_clock = std::make_unique<FrameClock>();
    frame_pacer = std::make_unique<gt4::pacing::FramePacer>(
        frame_clock->frequency(), gt4::pacing::guest_vblank_rate);
    pacing_diagnostics.reset();
    counters_at_last_report = {};
    std::fprintf(stderr, "[pacing] mode=%s; guest VBlank %llu Hz; timer=%s\n",
                 gt4::pacing::mode_name(pacing_mode),
                 static_cast<unsigned long long>(gt4::pacing::guest_vblank_rate),
                 frame_clock->high_resolution_timer() ? "high-resolution" : "1 ms");
#ifdef GT4_SHAPE_CAPTURE
    if (const char *output = std::getenv("GT4_OBSERVATION_OUTPUT"); output && *output) {
      capture = std::make_unique<gt4::observation::CaptureControl>(
          gt4::observation::runtime_observer(), std::filesystem::path(output));
    } else {
      std::fputs("[shape-capture] disabled: no explicit GT4_OBSERVATION_OUTPUT\n", stderr);
    }
#endif
    const CardLock card_lock(ps2::g_options.memcard_path + ".lock");
    ps2::init_graphics();
    int width = 0, height = 0;
    pixel_size(width, height);
    if (!width || !height)
      throw std::runtime_error("Cannot inspect native window dimensions");
    // Kind 3 is the host window kind of the pinned adapter (SurfaceTarget::Win32); here it carries the SDL window.
    if (!ps2::gs::attach_window(3, nullptr,
                                reinterpret_cast<std::uint64_t>(game_window),
                                nullptr, nullptr, static_cast<std::uint32_t>(width),
                                static_cast<std::uint32_t>(height)))
      throw std::runtime_error("Native Vulkan presentation refused window");
    surface_attached = true;
    // The presentation is 16:9 pillarbox; TASK-080 fills the screen in the race view at other ratios.
    ps2::gs::gpu::set_widescreen(true);
    gt4::display_aspect::set_presentation_sink(present_race_view);
    std::fprintf(stderr,
                 "[window] real guest GPU output through native Vulkan (MoltenVK)\n");
    // shortcut: one thread and FIFO presentation, qualify pipelining later.
    ps2::g_options.on_vblank = on_vblank;
    gt4::achievements::initialize(game_window, ps2::g_ram, ps2::kRamSize, ps2::g_spr, ps2::kScratchpadSize);
    AudioOutput audio(pump_messages, pacing_mode);
    audio_output = &audio;
#ifdef GT4_PERF_PROBE
    gt4::perf::Probe::shared().set_report_section([](std::FILE *output) {
      // F8 report: counter differences since the last F7 or report, then totals.
      const gt4::pacing::Counters session = pacing_counters();
      pacing_diagnostics.format(output, pacing_mode,
                                session.since(counters_at_last_report), session);
      if (force_480p_enabled)
        std::fprintf(output, "[display] GT4_FORCE_480P: race display index writes %llu\n",
                     static_cast<unsigned long long>(force_480p_state.writes));
      counters_at_last_report = session;
      pacing_diagnostics.reset();
#ifdef GT4_MTVU_MEASURE
      // TASK-011 step 0: interval counters since the last F7 or report, then a new interval.
      gt4::mtvu::report(output, "interval");
      gt4::mtvu::reset_interval();
#endif
    });
#endif
    ps2::set_audio_sink(
        [&audio](const std::int16_t *samples, std::size_t frames) {
          if (!audio.push(samples, frames))
            ps2::halt("window closed");
        });
    int result = 1;
    try {
      result = emulate();
    } catch (...) {
      ps2::set_audio_sink({});
      throw;
    }
    // Exit path (TASK-025 Part A / TASK-058): a save made less than 50 VBlanks before the window closed, including the
    // GS-process loss path, is written now. emulate() returns normally after halt(), so this runs on every exit.
#ifdef GT4_MEMCARD_FLUSH_OVERLAY
    ps2::iop::sio2::flush_cards_now();
#endif
    ps2::set_audio_sink({});
    audio.close(!closing && result == 0);
    audio_output = nullptr;
    cleanup();
    return result;
  } catch (const std::exception &error) {
    cleanup();
    std::fprintf(stderr, "[host] audio/input/window failure: %s\n",
                 error.what());
    return 1;
  }
}
