# macOS arm64 port

Status: in progress (fork `vgs499p/gt4-recomp-mac`). The Windows-qualified build is unchanged; every macOS change
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

### Donor runtime and Vulkan GS backend

The donor runtime, the derived port and the SPIR-V shaders are not committed (`private/`). Without them CMake skips
these targets and prints why. They are public (MIT donor) or generated:

```sh
git clone https://github.com/snuri00/ps2-web-recomp.git private/references/ps2-web-recomp
git -C private/references/ps2-web-recomp config core.autocrlf true   # the pins are Windows (CRLF) checkouts
git -C private/references/ps2-web-recomp checkout 1b4808793a12c45125262fd2d5f50a9ec9f7c020
python3 -I scripts/prepare_external_vulkan.py private/macos-port --gs-stream-hooks
cargo build --release --locked --manifest-path tools/wgsl_spirv/Cargo.toml --target-dir build/wgsl_spirv
build/wgsl_spirv/release/gt4-wgsl-spirv private/references/ps2-web-recomp/runtime/src/gs/gpu/gs.wgsl private/macos-shaders
```

`prepare_external_vulkan.py` checks the donor against its pins. CMake then checks the port and the nine shaders
against `docs/inputs/slice191-gs-stream-runtime.json`: the shaders built on macOS are byte-identical to the Windows
production shaders, and the port files are identical once their LF line endings are turned into CRLF (Python on
macOS writes LF; that is the only difference).

## Milestones

| # | Milestone | State |
|---|---|---|
| 1 | Host runtime and its controls build and pass on macOS (no game data needed) | Done (35 tests) |
| 2 | EE game library regenerated for arm64 from the owner's ISO, game executable, first boot | Done (boots to the first menu) |
| 3 | GS renderer process (`gt4recomp-gs`, PCSX2 fork) built for macOS | Done (Metal; owner race test passed) |
| 4 | Launcher (Tauri) platform crate and `.app` packaging | Working (`ports/macos/package_app.py`); see milestone 4 |

### Milestone 1 detail

Ported and passing (`ctest`, 35 tests):

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
- Donor runtime (`ps2runtime_vulkan`: every donor runtime source plus the slice191 GS stream port) builds for arm64
  with Apple Clang, unmodified.
- `native_vulkan.cpp`: the Vulkan loader is `libvulkan.1.dylib` (app bundle `Contents/Frameworks` first, then
  Homebrew) with MoltenVK found through its ICD manifest; the instance enables `VK_KHR_portability_enumeration` and the
  device `VK_KHR_portability_subset` when offered; the surface is `VK_EXT_metal_surface` on an SDL Metal view of the
  game window. The pinned port names the host window `HWND` and `SurfaceTarget::Win32`; on macOS that handle is the
  `SDL_Window`, so the derived sources stay byte-identical to the Windows ones. Measured on an Apple M4 (MoltenVK,
  Vulkan 1.4): `native_vulkan_pixels` (GPU raster, uploads in nine pixel formats and display reads match the CPU
  reference over the whole 4 MiB VRAM; Metal swapchain repeat, zero-size and resize), its `GT4_GS_SYNC=1` and forced
  baseline variants, and the async/sync stress with byte-identical VRAM all pass. The fast path is used: swapchain
  maintenance 1 (KHR) with present fences.
- `window_sdl.cpp`: the SDL3 counterpart of `window.cpp`, function for function: an SDL window with a Metal layer and
  high pixel density (sizes in pixels), SDL events for close, F7/F8/F9, F11 and Option+Return fullscreen (desktop
  fullscreen without a separate Space), focus and resize; keyboard bindings keep their Windows virtual-key codes and
  are read through SDL scancodes (letters and digits follow the keyboard layout); `GT4_MONITOR` names an SDL display;
  the memory card lock is `flock`. It links only into the game executable (milestone 2), so `gt4_window` compiles it
  with the GS stream client, perf probe and MTVU measure hooks on. A throwaway smoke run (open_window and run_window
  with 120 empty VBlanks) opened a 960x540-point window with a 1920x1080 Metal swapchain, paced 60 Hz VBlanks, opened
  audio and closed cleanly.
- `native/posix_compat.hpp`: `fopen_s`, `tmpfile_s`, `_putenv_s`, `GetEnvironmentVariableA`, `GetCurrentProcessId`,
  `GetTickCount64` with Windows return conventions, for code shared with Windows.

### Milestone 2 detail

The game library comes from the owner's disc. Python is a private virtual environment with
`scripts/requirements.txt` (pycdlib); `-E -s` keeps the script folder on the import path, unlike `-I`.

```sh
python3 -I -m venv private/tooling-venv
private/tooling-venv/bin/pip install -r scripts/requirements.txt
private/tooling-venv/bin/python -E -s scripts/ee_producer.py prepare --assets-root . \
    --output private/macos-ee-producer --iso "<path>/Gran Turismo 4 (USA) (v2.00).iso"
private/tooling-venv/bin/python -E -s ports/macos/ee_generate.py private/macos-ee-producer
cmake -S . -B build/macos -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/macos
```

- `prepare` is unchanged: it checks the ISO, `SYSTEM.CNF`, the boot file and `CORE.GT4` against
  `docs/inputs/usa-v2.00.json` and rebuilds the ELF byte for byte (`usa-v2.00-native.json`).
- `ports/macos/ee_generate.py` is the macOS counterpart of `ee_producer.py generate`, whose toolchain receipt only
  accepts the Windows clang-cl recipe. It builds the frozen donor translator with the host `clang++`, runs it, and
  requires the output to equal the frozen inventory. **Confirmed** on an Apple M4 with Apple clang: all 328 files
  (86,375,038 bytes, inventory `7a1a0909...d087`) are byte-identical to the Windows generation (the translator writes
  in binary mode). The 326 units compile with Apple clang for arm64 without source changes (about one minute).
- `gt4vulkan` links the derived `main.cpp` (pinned; the POSIX shims are force-included for its `_putenv_s`), the
  `ps2game` library, the donor runtime with the slice191 port, `window_sdl.cpp`, host I/O and achievements.
- Without the capsule below the build uses the plain donor runtime (VU1 and VU0 interpreted, one thread).

**Release runtime profile (slice191: MTVU, VU1 and VU0 AOT, GS stream hooks).** Two more private inputs, both checked
by CMake against the release pins:

```sh
private/tooling-venv/bin/python -E -s scripts/prepare_perf_runtime.py private/macos-runtime --assets-root . --gs-stream
private/tooling-venv/bin/python -E -s ports/macos/vu_generate.py private/macos-ee-producer private/macos-vu
```

- `prepare_perf_runtime.py` is unchanged; on macOS its seven sources equal the `slice191-gs-stream-runtime.json`
  pins byte for byte, and its receipt equals the pin once LF is turned into CRLF.
- `ports/macos/vu_generate.py` rebuilds the VU1 image (decrypted ELF blob plus the 16 pinned tail bytes; the Windows
  recipe reads the tail from recorded snapshots, which a fresh machine lacks, and `vu1_image.py check` then confirms
  the whole image hash), the VU0 image and both entry lists, builds `tools/vu_recomp` with the host clang++ and emits
  the tables. **Confirmed** on the M4: the 2,048-program VU1 table and manifest (`slice157-vu1-all.json`) and the
  14-program VU0 table and manifest (`slice160-vu0-aot.json`) equal the release pins.
- CMake then swaps the capsule sources into the runtime, adds the tables and `mtvu_bridge.cpp`, and defines
  `GT4_MTVU_RUNTIME`, `GT4_PERF_PROBE`, `GT4_VU1_AOT` and `GT4_VU0_AOT`, as the Windows slice191 profile does.
- Still missing from the release set: license guard, memory card flush, aspect patch and the slice217 EE library.

Owner race session on the release profile (Apple M4, MoltenVK, built-in Vulkan GS, `GT4_PERF=1` 5 s intervals):
EE work 1.2-2.2 ms/frame and the MTVU worker 2.2-3.7 ms/frame, so the CPU side has ample headroom. The late frames and
audio underruns line up with GPU waits instead: `gs_fence_wait` up to 11.9 ms/frame and `present_wait` up to
8.4 ms/frame; the worst 5 s interval ran at 52.2 VBlanks/s with 262 late frames. **High confidence:** the remaining
cost is the compute GS on the Apple GPU (milestone 3, a hardware GS renderer, is the expected fix). The owner also saw
images of cars from previous frames flash in races; cause **Unknown** (candidates: asynchronous GS submission, the
interlaced field path). `GT4_GS_SYNC=1` is the first A/B.

First boot (owner's disc, empty memory card):

- Headless (`--frames 900 --frames-dir ... --frame-every 150`): 900 frames in 2.4 s wall time; by frame 300 the
  game shows its "No GT4 game data found on memory card" dialog, drawn correctly by the Vulkan GS on MoltenVK.
- Windowed (`--window --frames 1500`): 1,495 Metal presentations in 26 s including startup (about 60 Hz), 4.3 s of
  user CPU time, audio paced with 1 underrun (ratio 1.005), clean exit.
- **Not yet checked:** gameplay, saving, races, controller input in game, long sessions.

Run it (after the build):

```sh
build/macos/ports/macos/gt4vulkan private/macos-ee-producer/SCUS_973.28.elf \
    --disc "<path>/Gran Turismo 4 (USA) (v2.00).iso" --memcard private/<your card>.ps2 --window
```

Open design points:

- **GS process presentation (milestone 3).** Decided: a remote Core Animation context hosted with `CALayerHost`
  (see below). `attach_window` still sends the game's `SDL_Window` pointer, which gt4-gs only logs.

### Milestone 3 progress (runs end to end)

- `gt4recomp-gs` (tag `gt4gs-v1.2.1`) builds for arm64 macOS with the Metal renderer (`USE_VULKAN=OFF`, Qt off,
  system FFmpeg ignored) on branch `gt4gs/macos` of https://github.com/vgs499p/gt4recomp-gs (a fork of `moesuito/gt4recomp-gs`). Pinned plutovg 1.3.2,
  plutosvg 0.0.7 and rapidyaml 0.12.1 are built from PCSX2's own hashes; the rest comes from Homebrew. The fork's
  `GT4GS_ONLY` (JIT-free) mode is x86-64 only, so this build is the ordinary core; the GS process never runs game code.
- Presentation: gt4-gs makes its Metal layer the root of a remote Core Animation context and returns the context id in
  the WINDOW restored event (`WindowPayload.reserved2`, zero on Windows); the game hosts it with a `CALayerHost` over
  its window (`native/gs_stream/gs_layer_host_mac.mm`). The vendored stream transport now carries the POSIX IPC.
- Fork changes for macOS (branch `gt4gs/macos`): the stream renderer defaults to Metal (Vulkan elsewhere); the data
  root defaults to `~/Library/Application Support/GT4Recomp/gt4-gs` and is created with its parents; READY carries the
  POSIX pid; `gt4gs/package-macos.sh <build> <out>` lays out `gt4-gs`, `resources/` and `resources/Metal23.metallib`
  (compiled with `xcrun metal`), the licence files and `SOURCE.txt`. The game expects that folder at
  `build/macos/ports/macos/gs`.
- **First live runs** (`GT4_RENDERER=pcsx2`, Apple M4, menus and intro only, 1,200 and 1,500 frames): handshake,
  READY (PCSX2 GS Metal x1, protocol 1.3), attach, picture layer hosted, clean shutdown with exit code 0. The GS
  process spent 0.4-0.6 ms per frame on its GS thread and 0.5-1.6 ms of GPU time (8-10 percent); the built-in
  renderer waited up to 12 ms per frame on the GPU in a race.
- **Black picture, fixed.** `GSDeviceMTL::UpdateWindow` replaces the view's layer when it rebuilds its surface, so
  the shared context kept a layer nobody presented to. The live view now overrides `setLayer:` and moves the context to
  every new layer (fork commit "keep the shared context on the Metal device's current layer"). Window captures with
  `screencapture -l` show the intro playing in the game window. One remote context shows in only one `CALayerHost`
  at a time, so hosting the same id twice leaves one host empty.
- **Owner race test** (`GT4_RENDERER=pcsx2`, Apple M4, windowed): "runs well". Not yet measured: frame times in a
  race, resolution scaling, whether the previous-frame car flashes of the built-in renderer are gone, and the
  "0 acquires" counter (Vulkan only; meaningless on Metal).

### Milestone 4 progress (working)

- The launcher workspace builds on arm64 macOS. Off Windows the program names have no `.exe`; the requirement check
  (SSE4.1 and Vulkan) is skipped on macOS, where the build is arm64 only and gt4-gs renders with Metal; update offers
  are off (the releases are Windows packages).
- Folders on macOS: user data in `~/Library/Application Support/GT4Recomp`, large data (disc copy, prepared files,
  sessions, logs, renderer caches) in its `data` subfolder. The app bundle is never written: its
  `Contents/Resources/data` holds gt4-gs as an installed version (`gs/current.txt`, `gs/1.2.1/sha256.txt`) and is read
  beneath the data folder. There are no 1.2.0 legacy folders on macOS.
- `ports/macos/package_app.py` lays out `GT4Recomp.app` (launcher, `MacOS/game/gt4vulkan`, the gt4-gs folder), copies
  every non-system library into `Contents/Frameworks` with `@rpath` references, signs ad hoc and records the renderer
  hash after signing. The bundle holds code translated from the owner's disc: it is for that Mac only.
- gt4-gs on macOS maps a Vulkan or D3D renderer request (the launcher writes `Renderer = 14`) to Metal (fork commit
  "run a Vulkan/D3D renderer request on Metal"); before that the GS failed to open from the launcher.
- The menu bar item opens its menu (Show launcher, Quit) on a click on macOS; on Windows a left click still shows
  the launcher.
- **Checked:** setup from the owner's ISO in the app; the bundled game with the launcher's session INI starts gt4-gs
  on Metal (3x) and shows the picture in the game window. **Not yet checked:** Play pressed in the app after the fix,
  the menu bar menu, saves and replays pages, controller navigation in the launcher.
- The game now carries the slice206 license demo guard (`private/macos-license-guard`, from
  `scripts/prepare_license_guard.py`, byte-identical to `slice206-license-guard-runtime.json`); without it license tests
  crashed ("missing code at 0x00000000"). `GT4_MONITOR` on macOS is the display name AppKit and SDL report.
- Launcher on macOS: Open folder uses `/usr/bin/open`, deletes go to the Trash, screens and modes come from AppKit and
  CoreGraphics, free space from `statvfs`.
- **Owner test (Apple M4, the app, Play):** "game works extremely well". Still to do: Windows wording in the launcher
  pages (Recycle Bin, Documents, installer, "from GitHub").
- Known failures on macOS (Windows path rules, already failing before the port): `gt4l-core`
  `stored_paths_inside_the_install_folder_become_relative`, `system_check` `the_required_list_matches_the_game_source`,
  `gt4l-tauri` `settings_store_no_absolute_path_inside_the_install_folder`.

## Releases

`ports/macos/release_macos.sh <release name>` (for example `1.2.5-mac.1`) builds a release on the owner's Mac and
uploads nothing:

- `ports/macos/build_release_deps.sh` builds the bundled libraries for arm64 and macOS 14.4 (SDL3 3.4.18, zstd, lz4,
  libpng with APNG, libjpeg-turbo, libwebp, freetype, plutovg, plutosvg, rapidyaml) from hash-checked sources: the
  versions and hashes of PCSX2's macOS dependency script, SDL3 at the game's pin (hash equal to the GitHub release
  digest). Homebrew bottles target the running macOS only. The prefix must have no spaces (WebP records it unquoted),
  so a checkout under "Application Support" uses `~/Library/Caches/GT4Recomp/macos-release-deps`.
- The game (`build/macos-release/game`, release runtime and license guard required, ctest without
  `native_gs_stream_client`), gt4-gs (`build/macos-release/gs-build`, packaged by the fork's `package-macos.sh`) and
  the launcher (`MACOSX_DEPLOYMENT_TARGET=14.4`) are built for macOS 14.4.
- `package_app.py --libraries <prefix>/lib --min-macos 14.4` bundles the release libraries and refuses any program or
  library that needs a newer macOS. Output: `build/macos-release/out/GT4Recomp.app`,
  `GT4Recomp-<name>-macos-arm64.zip` (ditto) and `SHA256SUMS`.
- The game executable carries the translated game code, as upstream releases do (decision 0069); the disc image is
  never included. The app is signed ad hoc, not notarized.
- `Casks/gt4recomp.rb` makes this repository a Homebrew tap; update its `version` and `sha256` for each release.
- Checked for 1.2.5-mac.1: every bundled program and library has `minos` 14.4; the unzipped release started gt4-gs on
  Metal and showed the picture in the game window. Not checked: an actual macOS 14 or 15 machine.
