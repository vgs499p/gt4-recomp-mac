<div align="center">

# GT4Recomp for Mac

**Gran Turismo 4, recompiled to run natively on Apple Silicon Macs.**<br>
An unofficial macOS arm64 port of [GT4Recomp](https://github.com/moesuito/gt4-staticrecomp).

![Apple Silicon](https://img.shields.io/badge/macOS-Apple%20Silicon-2ea44f?style=for-the-badge)
![Metal](https://img.shields.io/badge/renderer-Metal-1f6feb?style=for-the-badge)
![MIT](https://img.shields.io/badge/license-MIT-1f6feb?style=for-the-badge)
![Status](https://img.shields.io/badge/status-playable%2C%20build%20it%20yourself-e3b341?style=for-the-badge)

</div>

GT4Recomp is a static recompilation of **Gran Turismo 4 (NTSC-U, SCUS-97328)**: the game's own code is
translated ahead of time into C++ and compiled to native machine code, and the PS2 hardware it talks to is
reproduced in software. Nothing generates code while the game runs.

This fork brings it to **arm64 macOS**. The game code is compiled for Apple Silicon, and the picture comes from the
PCSX2 GS renderer running on **Metal** in its own process.

## Status

| Part | State |
|---|---|
| Host runtime and its tests | Done (35 tests pass) |
| Game library built for arm64 from your own disc | Done: the generated code is byte-identical to the Windows build's |
| Boot, menus, races, saves to a memory card, controller and keyboard | Working |
| PCSX2 GS renderer (Metal), shown inside the game window | Working; races run well on an Apple M4 |
| Launcher and a double-clickable `GT4Recomp.app` | Working: sets the game up from your disc image and plays it |

Tested only on an **Apple M4**. Other Apple Silicon Macs should work but have not been tried.

## Known issues

- The app is not notarized by Apple (see Install for how to open it).
- The built-in renderer (used without `GT4_RENDERER=pcsx2`) runs on MoltenVK. It is slower, and cars from earlier
  frames can flash during races. Use the PCSX2 renderer.
- Frame times, higher resolutions and long sessions have not been measured on macOS yet.
- If the game is force-quit, its renderer process may keep running in the background until it notices.
- The launcher's setup page still says the renderer comes "from GitHub"; on macOS it is already inside the app.
- Some launcher pages still use Windows wording (Recycle Bin, Documents, installer), and the launcher never offers
  updates on macOS: rebuild the app to update.

## Requirements

- An **Apple Silicon Mac** with **macOS 14.4 or later**, and about **6 GB free** (the launcher keeps a copy of
  your disc image).
- **Xcode command line tools**, **CMake 3.24+**, **Ninja**, **Python 3** and **Rust** (for the shader compiler).
- Homebrew packages: `sdl3`, `molten-vk`, `vulkan-loader`, `vulkan-headers`, plus the renderer's libraries
  (see the renderer build notes).
- **Your own disc image of Gran Turismo 4 USA v2.00 (SCUS-97328)** as a plain `.iso`. This repository contains no
  game files.

## Install

1. Download `GT4Recomp-<version>-macos-arm64.zip` from [Releases](https://github.com/vgs499p/gt4-recomp-mac/releases),
   unzip it and move **GT4Recomp.app** to Applications. Or with Homebrew:

   ```sh
   brew tap vgs499p/gt4-recomp-mac https://github.com/vgs499p/gt4-recomp-mac
   brew install --cask gt4recomp
   ```

2. The app is signed but not notarized, so the first time macOS refuses to open it. Open **System Settings > Privacy &
   Security** and click **Open Anyway** next to GT4Recomp (the Homebrew install skips this step).
3. Choose your Gran Turismo 4 (USA) v2.00 disc image once, then press **Play**. Your settings, saves and the copy of
   your disc go in `~/Library/Application Support/GT4Recomp`.

Releases never include the disc image: you always bring your own copy.

## Build and play

The full recipe, with every command and the checks each step makes, is in
[docs/MACOS-PORT.md](docs/MACOS-PORT.md). In short:

1. **Host runtime:** configure and build with CMake, then run `ctest`.
2. **Game library:** `scripts/ee_producer.py prepare` checks your ISO and rebuilds the game executable byte for
   byte; `ports/macos/ee_generate.py` translates it into C++, which is then compiled for arm64.
3. **Release runtime:** `scripts/prepare_perf_runtime.py` and `ports/macos/vu_generate.py` produce the precompiled
   VU1 and VU0 programs and the multithreaded VU1 runtime. All of them are checked against the release pins.
4. **Renderer:** build `gt4-gs` from [vgs499p/gt4recomp-gs](https://github.com/vgs499p/gt4recomp-gs), branch
   `gt4gs/macos`, and package it with `gt4gs/package-macos.sh` into `build/macos/ports/macos/gs`.
5. **App:** build the launcher and put everything into one app:

```sh
cargo build --release --manifest-path launcher/Cargo.toml -p gt4l-tauri
python3 -I ports/macos/package_app.py --out build/macos/GT4Recomp.app
```

6. **Play:** open `build/macos/GT4Recomp.app` (move it to Applications if you like), choose your disc image once,
   then press **Play**. Your settings, saves and the copy of your disc go in `~/Library/Application Support/GT4Recomp`.

The app you build is signed only for your Mac (ad hoc), so other Macs will refuse to open it.

You can also start the game without the launcher:

```sh
GT4_RENDERER=pcsx2 build/macos/ports/macos/gt4vulkan private/macos-ee-producer/SCUS_973.28.elf \
    --disc "<path>/Gran Turismo 4 (USA) (v2.00).iso" --memcard private/<your card>.ps2 --window
```

### Controls

Controllers (Xbox, DualShock 4, DualSense, Switch Pro and others) work through SDL3 with the DualShock 2 layout.
Turn the controller on before you start the game.

| Key | Action (PS2 button) |
|---|---|
| W · Enter | Accelerate, confirm (Cross) |
| S | Brake (Square) |
| Space | Handbrake (Circle) |
| A · D · arrow keys | Steer, navigate (D-pad) |
| Backspace · Esc | Back (Triangle) |
| Shift | Nitro (R1) |
| R · F | Gear up · gear down (R2 · L2) |
| Q | Look back (L1) |
| C · Tab | Camera (Select) |
| P | Start / Pause |
| Option+Return, F11 | Fullscreen |

## How the Mac port works

- **Same generated code as Windows.** The donor translator is built with Apple clang. Its 328 output files match the
  Windows generation byte for byte, and the 326 units compile for arm64 without source changes.
- **Windows code paths stay unchanged.** Mac-specific code is either a new file or an `#ifdef` branch next to the
  original: POSIX shared memory and wait-on-address for the renderer link, SDL3 for the window, audio and input,
  and CommonCrypto for hashes.
- **Picture sharing between processes.** macOS has no cross-process child windows, so `gt4-gs` makes its Metal
  layer the root of a remote Core Animation context. The game shows that context in its window with a `CALayerHost`,
  so the picture is never copied.
- **No GPL code in the game executable.** The PCSX2 GS lives only in the separate `gt4-gs` process.

## Credits

- **[GT4Recomp](https://github.com/moesuito/gt4-staticrecomp)** by moesuito and contributors: the project this port is
  built on. Everything that makes the game run is theirs. Its GitHub contributors include Wise
  ([gustavocanepa](https://github.com/gustavocanepa)), Julio Vitor Rufino ([JulioHeroZ](https://github.com/JulioHeroZ))
  and Luan Antony ([luanantony](https://github.com/luanantony)).
- **gt4-staticrecomp**, the original project by [Kaezzey](https://github.com/Kaezzey) (Niket Shah), where GT4Recomp
  started.
- **[ps2-web-recomp](https://github.com/snuri00/ps2-web-recomp)** (MIT) by snuri00: the runtime foundation.
- **[gt4-decomp](https://github.com/gustavocanepa/gt4-decomp)** (MIT) by gustavocanepa,
  **[Nenkai's GT research](https://github.com/Nenkai)** and **[GT4FS](https://github.com/Razer2015/GT4FS)** by
  Razer2015: the research behind the port.
- **[PCSX2](https://github.com/PCSX2/pcsx2)** (GPL-3.0): the GS renderer in `gt4-gs`.
- macOS port by vgs499p, with [Claude](https://claude.com/claude-code).

Exact revisions and licenses: [THIRD_PARTY.md](native/THIRD_PARTY.md).

## License and legal

- GT4Recomp's own code is [MIT](LICENSE). The game executable does not contain or link GPL code; the PCSX2 GS
  (GPL-3.0-or-later) runs only in the separate `gt4-gs` process, whose source is
  [vgs499p/gt4recomp-gs](https://github.com/vgs499p/gt4recomp-gs).
- You bring your own disc. The project does not include Gran Turismo 4, disc images, BIOS files, keys or saves.
- "Gran Turismo" and "PlayStation" are trademarks of Sony Interactive Entertainment. Gran Turismo 4, its logos and
  artwork belong to Sony Interactive Entertainment and Polyphony Digital. This is an unofficial fan project, not
  affiliated with or endorsed by either, or by the upstream GT4Recomp project.
