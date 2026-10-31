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

Ported and passing (`ctest`, 25 tests):

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
- `native/posix_compat.hpp`: `fopen_s`, `tmpfile_s`, `_putenv_s`, `GetEnvironmentVariableA`, `GetCurrentProcessId`,
  `GetTickCount64` with Windows return conventions, for code shared with Windows.

Remaining for milestone 1:

- `gs_stream/gs_stream_client.cpp` and its fixture (process launch, file hashing).
- `host_io.cpp`: WinMM audio, XInput and keyboard to SDL3 audio, gamepad and keyboard.
- `achievements_http.cpp` (WinHTTP) and `achievements_overlay.cpp` (GDI+).
- `native_vulkan.cpp`: Vulkan loader and Win32 surface to MoltenVK and a Metal-layer surface (SDL3 window).
- `window.cpp`: Win32 window, VBlank hook and F-keys to SDL3.
