#include "gpu.hpp"
#include "gs/raster.hpp"
#include "native_vulkan.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <vector>

#ifndef _WIN32
// macOS port: the presentation window is a hidden SDL window (Metal layer); the Win32 calls below map onto it.
#include <SDL3/SDL.h>
#endif

namespace {
using namespace ps2::gs;

void compare_memory(const std::vector<std::uint8_t> &actual,
                    const char *label) {
  const auto *expected = memory();
  for (std::size_t index = 0; index < actual.size(); ++index) {
    if (actual[index] != expected[index]) {
      throw std::runtime_error(std::string(label) + " differs at VRAM byte " +
                               std::to_string(index) +
                               ": GPU=" + std::to_string(actual[index]) +
                               ", CPU=" + std::to_string(expected[index]));
    }
  }
  std::fprintf(stderr, "[fixture] %s: full 4MiB VRAM identical\n", label);
}

void sprite(State &state, std::uint32_t left, std::uint32_t top,
            std::uint32_t right, std::uint32_t bottom, std::uint32_t color) {
  Vertex vertices[2]{};
  for (auto &vertex : vertices) {
    vertex.r = static_cast<std::uint8_t>(color);
    vertex.g = static_cast<std::uint8_t>(color >> 8);
    vertex.b = static_cast<std::uint8_t>(color >> 16);
    vertex.a = static_cast<std::uint8_t>(color >> 24);
    vertex.z = 1;
  }
  vertices[0].x = left * 16;
  vertices[0].y = top * 16;
  vertices[1].x = right * 16;
  vertices[1].y = bottom * 16;
  raster::draw(state, vertices, 2);
  gpu::draw(state, vertices, 2);
}

void fragmented_upload(std::uint32_t format, unsigned bits,
                       std::vector<std::uint8_t> &downloaded) {
  State transfer{};
  transfer.bitbltbuf = (0x400ull << 32) | (1ull << 48) |
                       (static_cast<std::uint64_t>(format) << 56);
  transfer.trxpos = (3ull << 32) | (7ull << 48);
  transfer.trxreg = 5ull | (3ull << 32);
  std::vector<std::uint8_t> payload((15 * bits + 7) / 8);
  for (unsigned index = 0; index < 15; ++index) {
    const std::uint32_t value = (0x81234567u + index * 137) &
                                (bits == 32 ? UINT32_MAX : (1u << bits) - 1);
    write_pixel(format, 0x400, 1, 3 + index % 5, 7 + index / 5, value);
    for (unsigned bit = 0; bit < bits; ++bit)
      if (value & (1u << bit))
        payload[(index * bits + bit) / 8] |=
            static_cast<std::uint8_t>(1u << ((index * bits + bit) % 8));
  }
  gpu::image_begin(transfer);
  for (std::size_t offset = 0; offset < payload.size(); ++offset)
    gpu::image_data(payload.data() + offset, 1);
  gpu::download(downloaded.data());
  compare_memory(downloaded,
                 ("fragmented upload PSM " + std::to_string(format)).c_str());
}
} // namespace

int main() {
  try {
    using gt4::vulkan::Aspect;
    using gt4::vulkan::presentation_rect;
    const auto wide = presentation_rect(1920, 1080, Aspect::widescreen);
    const auto wide_in_standard =
        presentation_rect(960, 720, Aspect::widescreen);
    const auto wide_in_ultrawide =
        presentation_rect(2560, 1080, Aspect::widescreen);
    const auto empty = presentation_rect(0, 0, Aspect::widescreen);
    const auto tiny = presentation_rect(1, 1, Aspect::widescreen);
    const auto huge =
        presentation_rect(UINT32_MAX, UINT32_MAX, Aspect::widescreen);
    if (wide.left != 0 || wide.top != 0 || wide.width != 1920 ||
        wide.height != 1080 || wide_in_standard.top != 90 ||
        wide_in_standard.width != 960 || wide_in_standard.height != 540 ||
        wide_in_ultrawide.left != 320 || wide_in_ultrawide.width != 1920 ||
        empty.width != 0 || empty.height != 0 || tiny.width != 1 ||
        tiny.height != 1 || huge.width != UINT32_MAX ||
        huge.height != 2415919103u)
      throw std::runtime_error(
          "16:9 presentation rectangle differs from expected values");
    std::fprintf(stderr, "[fixture] fixed 16:9 centered "
                         "rectangles and overflow controls passed\n");
    {
      // TASK-080: race view rectangles of a numeric ratio (fill in auto mode, fitted for a fixed ratio).
      using gt4::vulkan::presentation_rect_ratio;
      const auto fill = presentation_rect_ratio(3440, 1440, 3440.0 / 1440.0);
      const auto ultra_in_wide = presentation_rect_ratio(1920, 1080, 21.0 / 9.0);
      const auto tall_in_wide = presentation_rect_ratio(1920, 1080, 4.0 / 3.0);
      const auto none = presentation_rect_ratio(0, 0, 2.0);
      const auto bad = presentation_rect_ratio(100, 100, 0.0);
      if (fill.left != 0 || fill.top != 0 || fill.width != 3440 || fill.height != 1440 ||
          ultra_in_wide.width != 1920 || ultra_in_wide.height != 823 || ultra_in_wide.top != 128 ||
          tall_in_wide.width != 1440 || tall_in_wide.left != 240 || tall_in_wide.height != 1080 ||
          none.width != 0 || bad.width != 0)
        throw std::runtime_error("ratio presentation rectangle differs from expected values");
      // Owner test 2026-10-21 (P2 letterbox check): a custom ratio wider than a 1920x1080 window is a letterboxed band
      // of that ratio, never the whole 16:9 window (Custom 2.44 and 32:9 = 3.56).
      const auto custom_244 = presentation_rect_ratio(1920, 1080, 2.44);
      const auto custom_329 = presentation_rect_ratio(1920, 1080, 32.0 / 9.0);
      if (custom_244.left != 0 || custom_244.width != 1920 || custom_244.height != 787 || custom_244.top != 146 ||
          custom_329.left != 0 || custom_329.width != 1920 || custom_329.height != 540 || custom_329.top != 270)
        throw std::runtime_error("custom ratio wider than the window is not letterboxed");
      std::fprintf(stderr, "[fixture] TASK-080 ratio rectangles passed (custom 2.44 and 32:9 letterboxed in 16:9)\n");
    }
    {
      gt4::vulkan::Context context;
      context.initialize(GT4_VULKAN_SHADER_DIR);
      std::array<gt4::vulkan::Buffer *, 8> buffers{};
      for (auto &buffer : buffers)
        buffer = context.create_buffer(256);
      context.bind_buffers(buffers);
      const std::uint32_t sentinel = 0x12345678;
      context.write(buffers[0], 0, &sentinel, sizeof(sentinel));
      const gt4::vulkan::Dispatch empty[] = {
          {5, 0, 0, 0}, {5, 0, 1, 0}, {5, 1, 0, 0}};
      context.execute(empty, 3);
      std::uint32_t result = 0;
      context.read(buffers[0], 0, &result, sizeof(result));
      if (result != sentinel)
        throw std::runtime_error("Zero-work dispatch changed VRAM");
      std::vector<gt4::vulkan::Dispatch> invalid = {{9, 0, 0, 0},
                                                    {5, 0, 0, 1}};
      // Oversized workgroup counts are only invalid above the device limit;
      // some drivers report UINT32_MAX as the maximum on an axis.
      const std::uint32_t limit_x = context.max_workgroup_count(0);
      const std::uint32_t limit_y = context.max_workgroup_count(1);
      if (limit_x < UINT32_MAX) {
        invalid.push_back({5, limit_x + 1, 1, 0});
      } else if (limit_y < UINT32_MAX) {
        invalid.push_back({5, 1, limit_y + 1, 0});
      } else {
        std::fprintf(stderr, "[fixture] device accepts UINT32_MAX workgroups "
                             "on x and y; oversized-dispatch case not "
                             "representable\n");
      }
      for (const auto &dispatch : invalid) {
        bool rejected = false;
        try {
          context.execute(&dispatch, 1);
        } catch (const std::runtime_error &) {
          rejected = true;
        }
        if (!rejected)
          throw std::runtime_error("Invalid dispatch escaped bounds checks");
      }
      std::fprintf(stderr, "[fixture] zero-work dispatch preserves VRAM; "
                           "invalid bounds refused\n");
    }
    ps2::gs::reset();
    if (gpu::mode() != gpu::Mode::primary)
      throw std::runtime_error("Fixture requires primary GPU rasterization");
    State state{};
    state.prim = 6;
    state.prmodecont = 1;
    state.colclamp = 1;
    state.ctx[0].frame = 1ull << 16;
    state.ctx[0].zbuf = 1ull << 32;
    state.ctx[0].scissor = (63ull << 16) | (63ull << 48);
    sprite(state, 0, 0, 32, 32, 0x80112233);
    sprite(state, 16, 16, 48, 48, 0x80445566);
    std::vector<std::uint8_t> downloaded(kMemorySize);
    gpu::download(downloaded.data());
    compare_memory(downloaded, "ordered overlapping sprites across tiles");
    state.ctx[0].frame |= 0x00FF0000ull << 32;
    ++g_setup_generation;
    sprite(state, 20, 20, 28, 28, 0x80FFAA99);
    gpu::download(downloaded.data());
    compare_memory(downloaded, "framebuffer channel mask");
    std::vector<std::uint32_t> pixels;
    std::uint32_t width = 0, height = 0;
    gpu::display({0, 1, PSMCT32, 0, 0, 64, 64, false}, pixels, width, height);
    if (width != 64 || height != 64 || pixels.size() != 4096)
      throw std::runtime_error("Invalid compute display shape");
    if ((pixels[0] & 0xFFFFFFu) != 0x112233u ||
        (pixels[40 * 64 + 40] & 0xFFFFFFu) != 0x445566u)
      throw std::runtime_error(
          "Compute display pixels differ from known colors");
    std::vector<std::uint32_t> vram, clut;
    std::uint32_t scale = 0;
    gpu::read_state(vram, scale, clut);
    if (scale != 1 || vram.size() != kMemorySize / 4 || clut.size() != 512)
      throw std::runtime_error("Invalid GPU state readback shape");
    gpu::write_state(vram, scale, clut);
    gpu::download_async(downloaded.data(), 0, kMemorySize, 47);
    if (gpu::finish_download(46) || !gpu::finish_download(47) ||
        gpu::finish_download(47))
      throw std::runtime_error("Readback serial ownership differs");
    compare_memory(downloaded, "state restore and readback serial");
    const struct {
      std::uint32_t format;
      unsigned bits;
    } formats[] = {{PSMCT32, 32},  {PSMCT24, 24}, {PSMCT16, 16},
                   {PSMCT16S, 16}, {PSMT8, 8},    {PSMT4, 4},
                   {PSMT8H, 8},    {PSMT4HL, 4},  {PSMT4HH, 4}};
    for (const auto &format : formats)
      fragmented_upload(format.format, format.bits, downloaded);
    for (std::size_t index = 0; index < vram.size(); ++index)
      vram[index] = static_cast<std::uint32_t>(index * 137u) ^ 0x0055AA33u;
    std::memcpy(memory(), vram.data(), kMemorySize);
    gpu::write_state(vram, 1, clut);
    const gpu::DisplaySource resolutions[] = {
        {0, 10, PSMCT32, 0, 0, 640, 448, true},
        {0, 10, PSMCT32, 0, 0, 640, 480, false},
        {0, 10, PSMCT32, 0, 0, 640, 1080, true},
        {0, 30, PSMCT32, 0, 0, 1920, 1080, false},
        {0, 32, PSMCT32, 0, 0, 2048, 2048, false}};
    for (const auto &source : resolutions) {
      gpu::display(source, pixels, width, height);
      if (width != source.width || height != source.height ||
          pixels.size() != static_cast<std::size_t>(width) * height)
        throw std::runtime_error("Resolution readback shape differs");
      for (std::uint32_t y = 0; y < height; ++y)
        for (std::uint32_t x = 0; x < width; ++x) {
          const auto expected =
              read_pixel(PSMCT32, 0, source.fbw, x, source.halved ? y / 2 : y) |
              0xFF000000u;
          if (pixels[static_cast<std::size_t>(y) * width + x] != expected)
            throw std::runtime_error(
                "Resolution pixels differ from independent CPU swizzle read");
        }
      std::fprintf(
          stderr,
          "[fixture] display %ux%u halved=%u: all pixels match CPU VRAM read\n",
          width, height, source.halved ? 1u : 0u);
    }
    bool oversize_refused = false;
    try {
      gpu::display({0, 32, PSMCT32, 0, 0, 2048, 2049, false}, pixels, width,
                   height);
    } catch (const std::runtime_error &) {
      oversize_refused = true;
    }
    if (!oversize_refused)
      throw std::runtime_error(
          "Oversized display escaped scratch capacity check");
#ifdef _WIN32
    HWND window = CreateWindowExW(0, L"STATIC", L"Vulkan presentation fixture",
                                  WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, nullptr,
                                  nullptr, GetModuleHandleW(nullptr), nullptr);
#else
    if (!SDL_Init(SDL_INIT_VIDEO))
      throw std::runtime_error(std::string("SDL video: ") + SDL_GetError());
    // 160x120 pixels: a high-density window is sized in points, so use half the size on a 2x screen.
    HWND window = SDL_CreateWindow("Vulkan presentation fixture", 160, 120, SDL_WINDOW_METAL | SDL_WINDOW_HIDDEN);
#endif
    if (!window)
      throw std::runtime_error("Cannot create fixture window");
    gpu::attach_surface({gpu::SurfaceTarget::Win32, nullptr,
                         reinterpret_cast<std::uint64_t>(window), nullptr,
                         nullptr, 160, 120});
    const gpu::DisplaySource display_source{0, 1, PSMCT32, 0, 0, 64, 64, false};
    for (int repeat = 0; repeat < 12; ++repeat)
      if (!gpu::present(display_source))
        throw std::runtime_error("Repeated Win32 presentation refused");
    gpu::resize_surface(0, 0);
    if (gpu::present(display_source))
      throw std::runtime_error("Zero-sized surface presented unexpectedly");
#ifdef _WIN32
    SetWindowPos(window, nullptr, 0, 0, 240, 180, SWP_NOACTIVATE | SWP_NOMOVE);
#else
    SDL_SetWindowSize(window, 240, 180);
#endif
    gpu::resize_surface(240, 180);
    if (!gpu::present(display_source))
      throw std::runtime_error("Restored resized surface refused presentation");
    for (const auto &source : resolutions) {
      gpu::set_widescreen(true);
      if (!gpu::present(source))
        throw std::runtime_error("Widescreen resolution presentation refused");
    }
#ifdef _WIN32
    DestroyWindow(window);
#endif
    std::fprintf(stderr,
                 "[fixture] Win32 repeat/zero-size/resize restore passed\n");
    gpu::stats();
    std::fprintf(stderr,
                 "[fixture] PASS: real Vulkan raster/display/readback\n");
    return 0;
  } catch (const std::exception &error) {
    std::fprintf(stderr, "[fixture] FAIL: %s\n", error.what());
    return 1;
  }
}
