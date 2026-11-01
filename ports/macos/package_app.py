"""Lays out GT4Recomp.app: the launcher, the game and the gt4-gs renderer in one double-clickable bundle.

    python3 -I ports/macos/package_app.py --out build/macos/GT4Recomp.app

Inputs (defaults are the paths the macOS build writes):
  --launcher  launcher/target/release/GT4Recomp      (cargo build --release -p gt4l-tauri)
  --game      build/macos/ports/macos/gt4vulkan      (milestone 2, release profile)
  --gs        build/macos/ports/macos/gs             (gt4gs/package-macos.sh of the gt4recomp-gs fork)

Bundle layout:
  Contents/MacOS/GT4Recomp                       the launcher (CFBundleExecutable)
  Contents/MacOS/game/gt4vulkan                  the game (paths::game_directory = <program folder>/game)
  Contents/Resources/data/gs/current.txt         the renderer as an installed gt4-gs version, read beneath the
  Contents/Resources/data/gs/<version>/...       player's data folder (paths::resolve_layout on macOS)
  Contents/Frameworks/*.dylib                    every non-system library the three programs load

Libraries outside /usr/lib and /System are copied into Frameworks and every reference to them becomes
@rpath/<name>, so the bundle runs without Homebrew or the private build folders. Each program is then signed ad hoc
(arm64 code must be signed to run), the renderer's hash is recorded after signing (gs_release checks it against
sha256.txt), and the bundle is sealed last.

The bundle holds a game built from the player's own disc: it is for this Mac only and must never be distributed.
"""

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
SYSTEM_PREFIXES = ("/usr/lib/", "/System/")
# The lowest renderer version the launcher accepts (releases::RENDERER_MINIMUM); the fork is tagged gt4gs-v1.2.1.
DEFAULT_GS_VERSION = "1.2.1"


def run(*command):
    subprocess.run(command, check=True)


def dependencies(binary):
    """Non-system libraries `binary` links, as written in its load commands (absolute or @rpath)."""
    output = subprocess.run(["otool", "-L", str(binary)], check=True, capture_output=True, text=True).stdout
    names = []
    for line in output.splitlines()[1:]:
        name = line.strip().split(" (compatibility")[0]
        if name and not name.startswith(SYSTEM_PREFIXES):
            names.append(name)
    return names


def own_install_name(binary):
    output = subprocess.run(["otool", "-D", str(binary)], check=True, capture_output=True, text=True).stdout
    lines = output.splitlines()
    return lines[1].strip() if len(lines) > 1 else None


def run_paths(binary):
    """The LC_RPATH entries of `binary`, with @loader_path resolved against its folder."""
    output = subprocess.run(["otool", "-l", str(binary)], check=True, capture_output=True, text=True).stdout
    lines = output.splitlines()
    paths = []
    for index, line in enumerate(lines):
        if line.strip() == "cmd LC_RPATH":
            path = lines[index + 2].strip().split(" (offset")[0].removeprefix("path ")
            paths.append(Path(path.replace("@loader_path", str(binary.parent))))
    return paths


def locate(name, referrer):
    """The file a load command of `referrer` (an original, unbundled file) names."""
    if not name.startswith("@"):
        return Path(name).resolve()
    leaf = name.split("/", 1)[1]
    folders = [referrer.parent] if name.startswith("@loader_path/") else run_paths(referrer) + [referrer.parent]
    for folder in folders:
        if (folder / leaf).is_file():
            return (folder / leaf).resolve()
    raise SystemExit(f"cannot resolve library {name} of {referrer}")


def bundle_libraries(programs, frameworks):
    """Copies the libraries of `programs` (and theirs, recursively) into `frameworks`. Returns the copied files by
    file name; every reference is later rewritten to @rpath/<file name>."""
    copied = {}
    pending = [(program, name) for program in programs for name in dependencies(program)]
    while pending:
        referrer, name = pending.pop()
        leaf = Path(name).name
        if leaf in copied:
            continue
        source = locate(name, referrer)
        target = frameworks / leaf
        shutil.copy2(source, target)
        target.chmod(0o755)
        copied[leaf] = target
        pending.extend((source, dependency) for dependency in dependencies(source))
    return copied


def rewrite_references(binary, copied):
    for name in dependencies(binary):
        if Path(name).name in copied and name != f"@rpath/{Path(name).name}":
            run("install_name_tool", "-change", name, f"@rpath/{Path(name).name}", str(binary))


def sign(path):
    run("codesign", "--force", "--sign", "-", "--timestamp=none", str(path))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--launcher", type=Path, default=REPO / "launcher/target/release/GT4Recomp")
    parser.add_argument("--game", type=Path, default=REPO / "build/macos/ports/macos/gt4vulkan")
    parser.add_argument("--gs", type=Path, default=REPO / "build/macos/ports/macos/gs")
    parser.add_argument("--gs-version", default=DEFAULT_GS_VERSION)
    args = parser.parse_args()

    for required in (args.launcher, args.game, args.gs / "gt4-gs"):
        if not required.is_file():
            raise SystemExit(f"missing input: {required}")
    app = args.out.resolve()
    if app.exists():
        raise SystemExit(f"output already exists: {app}")
    contents = app / "Contents"
    macos, resources, frameworks = contents / "MacOS", contents / "Resources", contents / "Frameworks"
    gs_root = resources / "data" / "gs"
    gs_dir = gs_root / args.gs_version
    for folder in (macos / "game", frameworks, gs_root):
        folder.mkdir(parents=True)

    launcher = macos / "GT4Recomp"
    game = macos / "game" / "gt4vulkan"
    shutil.copy2(args.launcher, launcher)
    shutil.copy2(args.game, game)
    shutil.copytree(args.gs, gs_dir)
    renderer = gs_dir / "gt4-gs"
    (gs_root / "current.txt").write_text(args.gs_version + "\n")

    # Libraries, then the search path of each program relative to where it sits in the bundle.
    programs = {launcher: "@executable_path/../Frameworks", game: "@executable_path/../../Frameworks",
                renderer: "@executable_path/../../../../Frameworks"}
    copied = bundle_libraries(list(programs), frameworks)
    for library in copied.values():
        run("install_name_tool", "-id", f"@rpath/{library.name}", str(library))
        rewrite_references(library, copied)
        run("install_name_tool", "-add_rpath", "@loader_path", str(library))
    for program, rpath in programs.items():
        rewrite_references(program, copied)
        run("install_name_tool", "-add_rpath", rpath, str(program))
    for path in list(copied.values()) + list(programs):
        sign(path)
        left = [name for name in dependencies(path) if not name.startswith("@rpath/")]
        if left:
            raise SystemExit(f"{path.name} still links outside the bundle: {left}")

    (gs_dir / "sha256.txt").write_text(hashlib.sha256(renderer.read_bytes()).hexdigest() + "\n")

    # Icon: the launcher icon at the sizes iconutil needs.
    iconset = app.parent / (app.stem + ".iconset")
    iconset.mkdir()
    source_icon = REPO / "launcher/assets/brand/launcher.png"
    for size in (16, 32, 64, 128, 256):
        run("sips", "-z", str(size), str(size), str(source_icon), "--out", str(iconset / f"icon_{size}x{size}.png"))
        if size <= 128:
            run("sips", "-z", str(size * 2), str(size * 2), str(source_icon),
                "--out", str(iconset / f"icon_{size}x{size}@2x.png"))
    run("iconutil", "-c", "icns", str(iconset), "-o", str(resources / "GT4Recomp.icns"))
    shutil.rmtree(iconset)

    version = next(line.split('"')[1] for line in (REPO / "launcher/Cargo.toml").read_text().splitlines()
                   if line.startswith("version = "))
    (contents / "Info.plist").write_text(f"""<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleName</key><string>GT4Recomp</string>
  <key>CFBundleDisplayName</key><string>Gran Turismo 4</string>
  <key>CFBundleIdentifier</key><string>io.github.moesuito.gt4recomp</string>
  <key>CFBundleExecutable</key><string>GT4Recomp</string>
  <key>CFBundleIconFile</key><string>GT4Recomp</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>{version}</string>
  <key>CFBundleVersion</key><string>{version}</string>
  <key>LSMinimumSystemVersion</key><string>14.4</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.racing-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>GCSupportsControllerUserInteraction</key><true/>
</dict>
</plist>
""")
    sign(app)
    run("codesign", "--verify", "--strict", str(app))
    print(f"GT4Recomp.app ready: {app}")
    print(f"renderer {args.gs_version}, {len(copied)} bundled libraries")


if __name__ == "__main__":
    sys.exit(main())
