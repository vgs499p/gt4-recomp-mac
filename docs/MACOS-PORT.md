# macOS arm64 port

Status: in progress (fork `vgs499p/gt4-staticrecomp`). The Windows-qualified build is unchanged; every macOS change
is either a new file or an `#ifdef _WIN32` / `#ifndef _WIN32` branch beside the original code.

## Build

Requirements: Apple Silicon Mac, macOS 14.4 or later (for `os_sync_wait_on_address`), Xcode command line tools,
CMake 3.24+, Ninja, and Homebrew `sdl3` (3.4.18, the version upstream pins), `molten-vk`, `vulkan-loader`,
`vulkan-headers`.

```sh
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos
ctest --test-dir build/macos --output-on-failure
```

On Apple platforms the root `CMakeLists.txt` hands off to `ports/macos/CMakeLists.txt`, which mirrors the targets of
`native/CMakeLists.txt` as each one is ported. No private assets are needed for these targets.

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Host runtime and its controls build and pass on macOS (no game data needed) | In progress |
| 2 | EE game library regenerated for arm64 from the owner's ISO (`tools/ee_producer`) | Not started |
| 3 | GS renderer process (`gt4recomp-gs`, PCSX2 fork) built for macOS | Not started |
| 4 | Launcher (Tauri) platform crate and `.app` packaging | Not started |

### Milestone 1 detail

Ported and passing (`ctest`, 29 tests):

- Pure host libraries: pacing, quickfix (window mode, race display, widescreen, cursor, aspect), Vulkan extension
  selection, memory card store, license guard, MTVU measure/journal/runtime, pad mapping, shape observer/capture.
- `install_paths.cpp`: executable folder through `_NSGetExecutablePath`; the GS process is `gs/gt4-gs` (no `.exe`).
- `perf_probe.cpp`: thread CPU time through `CLOCK_THREAD_CPUTIME_ID`.
- `frame_clock.cpp`: `steady_clock` nanoseconds and `nanosleep`, with the same final spin.
- `shape_capture_export.cpp`: `open(O_EXCL)` / `write` / `fsync`, removing a partial file on failure.
- `gs_stream/gs_stream_ipc.cpp`: POSIX shared memory (`shm_open`) for the mapping; the four auto-reset events are
  four 32-bit wake counters in a second shared object, waited on with `os_sync_wait_on_address_with_timeout` (shared)
  in 100 ms slices that also check the peer pid. The `ControlBlock` layout and the waiter-flag protocol are unchanged.
  Child processes use `posix_spawn`; there is no kill-on-close job, so `ChildProcess` kills a running child on
  destruction and children keep watching their parent pid. macOS limits shared memory names to 31 characters, so
  the objects are named `/gs<last 24 hex digits of the session name>m` and `...w`.
- `gs_stream/gs_stream_client.cpp` (game side) and `tools/gs_stream_record` (test consumer): thread CPU time and the
  OSD timers on `CLOCK_THREAD_CPUTIME_ID` / `CLOCK_MONOTONIC`; the GS process pin through CommonCrypto SHA-256; the
  player's "restart with the built-in renderer" dialog is an SDL3 message box on the game's SDL window. Children still
  running at a normal exit of the game (including every refusal) are killed by an atexit handler. **Difference from
  Windows:** there is no kill-on-close job, so a GS process whose game is killed (SIGKILL) is not ended by the system;
  the GS process must watch its `--parent` pid (`gs_stream_record` does, through the session peer checks). The
  `native_gs_stream_client` control checks this difference instead of the job.
- `host_io.cpp`: gamepads through the SDL3 backend (already the default; `GT4_INPUT=xinput` is refused, and
  `native/xinput_compat.hpp` gives the mapping API its XInput-shaped types). Audio is an SDL3 audio stream on the
  default output device, stereo 16-bit 48 kHz; a buffer counts as done once the device has pulled all of its bytes,
  as `WHDR_DONE` on Windows, so the blocking and paced paths and their rate control are unchanged. Measured on this
  port's control: 3 s of paced audio at 60 Hz, 0 underruns, worst push 0.023 ms, ratio 1.0012.
- RetroAchievements (`achievements.cpp`, vendored rcheevos): web requests through the system libcurl
  (`achievements_http_curl.cpp`; cancel wakes `curl_multi_poll`, so a stalled server is left at once, as closing the
  WinHTTP handle does). The overlay (`achievements_overlay_mac.mm`) is a borderless, click-through child window of
  the game's NSWindow with the same layout, drawn with CoreGraphics/CoreText at the screen's backing scale; the badge
  cache is under the system temporary folder, as on Windows. The overlay drawing was checked visually; the controls
  cover configuration, memory mapping, the rcheevos session, toast animation and loopback cancellation.
- `native/posix_compat.hpp`: `fopen_s`, `tmpfile_s`, `_putenv_s`, `GetEnvironmentVariableA`, `GetCurrentProcessId`,
  `GetTickCount64` with Windows return conventions, for code shared with Windows.

Remaining for milestone 1:

- `native_vulkan.cpp`: Vulkan loader and Win32 surface to MoltenVK and a Metal-layer surface (SDL3 window).
- `window.cpp`: Win32 window, VBlank hook and F-keys to SDL3.

Open design points:

- **GS process presentation (milestone 3).** On Windows the GS process draws into a child of the game window
  (CONTROL attach_window carries the HWND). macOS has no cross-process child windows; candidates are a GS-owned window
  kept on top of the game window, or an IOSurface / `CAMetalLayer` shared through a Mach port. `attach_window` sends the
  game's `SDL_Window` pointer as a placeholder until this is decided.
