#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
struct SDL_Window;
#endif

namespace gt4::achievements {

// The game window: a Win32 window, or the game's SDL window on macOS (window.cpp).
#ifdef _WIN32
using HostWindow = HWND;
#else
using HostWindow = SDL_Window *;
#endif

// Official RetroAchievements hash for Gran Turismo 4 (USA, SCUS-97328 v2.00).
inline constexpr const char* kGt4NtscUHash = "b7e2e47572f4163a216ea85eed3c6c63";

struct Config {
    bool enabled = false;
    bool hardcore = false;
    bool encore = false;
    std::string username;
    std::string token;
    std::string password;
    std::string game_hash = kGt4NtscUHash;
    std::string host;
    std::wstring cache_dir;
    std::string disc_path;
};

// Parses configuration using a custom environment getter (returns empty string if unset).
Config parse_config(const std::function<std::string(const char*)>& get_env);

// Reads configuration from the current process environment (GT4_RA_*).
Config config_from_environment();

// Maps rcheevos PS2 addresses into the 32 MiB EE RAM buffer and 16 KiB Scratchpad buffer.
uint32_t read_ps2_memory(
    uint32_t address,
    uint8_t* buffer,
    uint32_t num_bytes,
    const uint8_t* ram,
    size_t ram_size,
    const uint8_t* spr,
    size_t spr_size);

// Lifecycle hooks called from native/window.cpp.
void initialize(HostWindow game_window, const uint8_t* ram, size_t ram_size, const uint8_t* spr, size_t spr_size);
void on_vblank(HostWindow game_window, const uint8_t* ram, size_t ram_size, const uint8_t* spr, size_t spr_size);
void on_window_pos_changed(HostWindow game_window);
void reset_session();
void shutdown();
bool is_active();

} // namespace gt4::achievements
