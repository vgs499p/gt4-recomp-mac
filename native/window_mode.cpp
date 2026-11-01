#include "window_mode.hpp"

#include <cmath>
#include <cstring>
#include <stdexcept>

namespace gt4::window_mode {

bool parse_start_fullscreen(const char *value) {
  if (value == nullptr || std::strlen(value) == 0 || std::strcmp(value, "0") == 0)
    return false;
  if (std::strcmp(value, "1") == 0)
    return true;
  throw std::invalid_argument("GT4_FULLSCREEN must be 0 or 1");
}

#ifdef __APPLE__
// macOS: GT4_MONITOR is the display's name as macOS (and SDL) reports it, such as "Built-in Retina Display" or
// "DELL U2723QE": spaces and UTF-8 are allowed, control characters are not (window_sdl.cpp matches it exactly).
std::string parse_monitor(const char *value) {
  if (value == nullptr || std::strlen(value) == 0)
    return {};
  const std::size_t length = std::strlen(value);
  bool clean = true;
  for (std::size_t i = 0; i < length; ++i)
    clean = clean && static_cast<unsigned char>(value[i]) >= 0x20 && value[i] != 0x7F;
  if (length > 127 || !clean)
    throw std::invalid_argument(std::string("GT4_MONITOR '") + value + "' is not a display name");
  return value;
}
#else
std::string parse_monitor(const char *value) {
  if (value == nullptr || std::strlen(value) == 0)
    return {};
  const std::size_t length = std::strlen(value);
  bool printable = true;
  for (std::size_t i = 0; i < length; ++i)
    printable = printable && value[i] > 0x20 && value[i] < 0x7F;
  if (length < 5 || length > 31 || !printable || std::strncmp(value, "\\\\.\\", 4) != 0)
    throw std::invalid_argument(std::string("GT4_MONITOR '") + value + "' is not a monitor device name (\\\\.\\DISPLAYn)");
  return value;
}
#endif

ClientSize fit_client(std::uint32_t wanted_width, std::uint32_t wanted_height, std::int32_t max_width,
                      std::int32_t max_height) {
  if (wanted_width == 0 || wanted_height == 0 || max_width <= 0 || max_height <= 0)
    return {static_cast<std::int32_t>(wanted_width), static_cast<std::int32_t>(wanted_height)};
  const double scale = std::fmin(1.0, std::fmin(static_cast<double>(max_width) / wanted_width,
                                                static_cast<double>(max_height) / wanted_height));
  const auto width = static_cast<std::int32_t>(std::floor(wanted_width * scale));
  const auto height = static_cast<std::int32_t>(std::floor(wanted_height * scale));
  return {width < 1 ? 1 : width, height < 1 ? 1 : height};
}

Rect centred(const Rect &area, std::int32_t width, std::int32_t height) {
  const std::int32_t left = area.left + ((area.right - area.left) - width) / 2;
  const std::int32_t top = area.top + ((area.bottom - area.top) - height) / 2;
  return {left, top, left + width, top + height};
}

std::uint32_t fullscreen_style(std::uint32_t windowed_style) {
  return (windowed_style & ~overlapped_window_style) | popup_style;
}

bool ToggleState::enter(const Placement &windowed) {
  if (fullscreen_)
    return false;
  saved_ = windowed;
  fullscreen_ = true;
  return true;
}

bool ToggleState::leave(Placement &restored) {
  if (!fullscreen_)
    return false;
  restored = saved_;
  fullscreen_ = false;
  return true;
}

} // namespace gt4::window_mode
