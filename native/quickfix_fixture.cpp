#define _CRT_SECURE_NO_WARNINGS
// Pure fixture for the quick fixes: the fullscreen toggle state machine, the borderless style
// mask, and the GT4_FORCE_480P value parser (race_display). No window or device is touched.
#include "race_display.hpp"
#include "window_mode.hpp"

#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace {

using namespace gt4::window_mode;

void require(bool value, const std::string &label) {
  if (!value)
    throw std::runtime_error(label);
}

bool refused(const char *value) {
  try {
    gt4::display::parse_force_480p(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

void test_toggle_restores_windowed_placement() {
  ToggleState state;
  const Placement windowed{{100, 80, 1060, 620}, 0x16CF0000u};
  require(!state.fullscreen(), "starts windowed");
  Placement restored{};
  require(!state.leave(restored), "leave while windowed is refused");
  require(state.enter(windowed), "enter from windowed");
  require(state.fullscreen(), "fullscreen after enter");
  require(!state.enter({{0, 0, 1920, 1080}, 0x80000000u}),
          "second enter is refused and keeps the first placement");
  require(state.leave(restored), "leave from fullscreen");
  require(!state.fullscreen(), "windowed after leave");
  require(restored.bounds.left == 100 && restored.bounds.top == 80 &&
              restored.bounds.right == 1060 && restored.bounds.bottom == 620,
          "restored bounds equal the saved windowed bounds");
  require(restored.style == 0x16CF0000u, "restored style equals the saved style");
  require(state.enter({{5, 6, 7, 8}, 0x10CF0000u}), "toggle can enter again");
  require(state.leave(restored) && restored.bounds.left == 5 &&
              restored.style == 0x10CF0000u,
          "second cycle restores its own placement");
}

void test_fullscreen_style_mask() {
  const std::uint32_t visible = 0x10000000u;
  const std::uint32_t windowed = overlapped_window_style | visible;
  const std::uint32_t full = fullscreen_style(windowed);
  require((full & overlapped_window_style) == 0, "frame bits are removed");
  require((full & popup_style) != 0, "popup bit is added");
  require((full & visible) != 0, "visibility is kept");
  require(fullscreen_style(full) == full, "mask is idempotent");
  require(fullscreen_style(0) == popup_style, "empty style becomes popup");
}

void test_environment_values_are_strict() {
  require(!gt4::display::parse_force_480p(nullptr), "unset disables the switch");
  require(!gt4::display::parse_force_480p("0"), "0 disables the switch");
  require(gt4::display::parse_force_480p("1"), "1 enables the switch");
  require(refused(""), "empty value is refused");
  require(refused("true"), "true is refused");
  require(refused("01"), "01 is refused");
  require(refused("480p"), "named values are refused");
}

bool start_refused(const char *value) {
  try {
    parse_start_fullscreen(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

// GT4_FULLSCREEN: unset, empty and 0 start windowed; 1 starts fullscreen; anything else stops the start.
void test_start_mode_environment() {
  require(!parse_start_fullscreen(nullptr), "unset starts windowed");
  require(!parse_start_fullscreen(""), "empty starts windowed");
  require(!parse_start_fullscreen("0"), "0 starts windowed");
  require(parse_start_fullscreen("1"), "1 starts fullscreen");
  require(start_refused("true"), "true is refused");
  require(start_refused("on"), "on is refused");
  require(start_refused("2"), "2 is refused");
}

bool monitor_refused(const char *value) {
  try {
    parse_monitor(value);
  } catch (const std::invalid_argument &) {
    return true;
  }
  return false;
}

// GT4_MONITOR and the windowed client placement (TASK-080 addendum).
void test_monitor_and_client_size() {
  require(parse_monitor(nullptr).empty(), "unset = primary monitor");
  require(parse_monitor("").empty(), "empty = primary monitor");
#ifdef __APPLE__
  require(parse_monitor("Built-in Retina Display") == "Built-in Retina Display", "a macOS display name is kept");
  require(parse_monitor("DELL U2723QE (1)") == "DELL U2723QE (1)", "names with digits and brackets are kept");
  require(monitor_refused("Studio\nDisplay"), "control characters are refused");
  require(monitor_refused(std::string(128, 'A').c_str()), "too long");
#else
  require(parse_monitor(R"(\\.\DISPLAY2)") == R"(\\.\DISPLAY2)", "a device name is kept");
  require(monitor_refused("DISPLAY2"), "no device prefix");
  require(monitor_refused(R"(\\.\)"), "too short");
  require(monitor_refused(R"(\\.\DISPLAY 2)"), "spaces");
  require(monitor_refused(R"(\\.\DISPLAY2222222222222222222222222)"), "too long");
#endif
  const ClientSize same = fit_client(1920, 1080, 2560, 1400);
  require(same.width == 1920 && same.height == 1080, "a size that fits is kept");
  const ClientSize wide = fit_client(3440, 1440, 3440, 1380);
  require(wide.height == 1380 && wide.width == 3296, "too tall for the work area: shrunk with its shape");
  const ClientSize big = fit_client(3840, 2160, 1920, 1040);
  require(big.width == 1848 && big.height == 1040, "a 4K output in a 1080p work area keeps 16:9");
  const Rect area{1920, 0, 5360, 1440};
  const Rect box = centred(area, 2560, 1080);
  require(box.left == 2360 && box.top == 180 && box.right == 4920 && box.bottom == 1260, "centred on the second monitor");
}

} // namespace

int main() {
  try {
    test_toggle_restores_windowed_placement();
    test_fullscreen_style_mask();
    test_environment_values_are_strict();
    test_start_mode_environment();
    test_monitor_and_client_size();
    std::puts("quickfix fixture PASS");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "quickfix fixture FAIL: %s\n", error.what());
    return 1;
  }
}
