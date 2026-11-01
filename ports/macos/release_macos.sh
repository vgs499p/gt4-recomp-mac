#!/bin/bash
# Builds a GT4Recomp for Mac release on the owner's Mac: the game for macOS 14.4 and arm64, gt4-gs, the launcher,
# GT4Recomp.app, and a zip with its SHA256SUMS.
#
#   ports/macos/release_macos.sh <release name>          for example 1.2.5-mac.1
#
# Output in build/macos-release/out: GT4Recomp.app, GT4Recomp-<release name>-macos-arm64.zip and SHA256SUMS. Nothing
# is uploaded. The game executable carries the translated game code (the upstream release model, decision 0069); the
# disc image is never part of a release.
#
# Needs the milestone 2 inputs (private/macos-ee-producer, private/macos-runtime, private/macos-vu,
# private/macos-license-guard), the gt4recomp-gs fork in private/gt4recomp-gs on branch gt4gs/macos, Rust, and the
# release libraries (ports/macos/build_release_deps.sh, run here when they are missing).
set -euo pipefail

NAME="${1:?usage: release_macos.sh <release name, for example 1.2.5-mac.1>}"
[[ "$NAME" =~ ^[0-9A-Za-z.+-]+$ ]] || { echo "release name may use letters, digits, '.', '+' and '-' only" >&2; exit 1; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
MIN_MACOS=14.4
WORK="$ROOT/build/macos-release"
OUT="$WORK/out"
GS_FORK="$ROOT/private/gt4recomp-gs"

for input in private/macos-ee-producer/SCUS_973.28.elf private/macos-runtime/runtime.json \
	private/macos-vu/vu1-gen/vu1_generated.cpp private/macos-license-guard/runtime.json "$GS_FORK/pcsx2-gt4gs"; do
	[ -e "$input" ] || { echo "missing release input: $input" >&2; exit 1; }
done
if [ -n "$(git -C "$GS_FORK" status --porcelain --untracked-files=no)" ]; then
	echo "the gt4recomp-gs fork has uncommitted changes; a release names its commit in SOURCE.txt" >&2
	exit 1
fi

# The release libraries (same prefix rule as build_release_deps.sh).
DEPS="${GT4_RELEASE_DEPS:-$ROOT/private/macos-release-deps}"
[[ "$DEPS" != *" "* ]] || DEPS="$HOME/Library/Caches/GT4Recomp/macos-release-deps"
[ -f "$DEPS/lib/libSDL3.0.dylib" ] || GT4_RELEASE_DEPS="$DEPS" ports/macos/build_release_deps.sh

echo "== game"
cmake -S . -B "$WORK/game" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_MACOS -DCMAKE_PREFIX_PATH="$DEPS;/opt/homebrew" > "$WORK-game-configure.log"
grep -q "license demo guard" "$WORK-game-configure.log" || { echo "the license guard is not in the build" >&2; exit 1; }
grep -q "release runtime profile" "$WORK-game-configure.log" || { echo "the release runtime is not in the build" >&2; exit 1; }
cmake --build "$WORK/game"
ctest --test-dir "$WORK/game" --output-on-failure -E native_gs_stream_client

echo "== gt4-gs"
cmake -S "$GS_FORK" -B "$WORK/gs-build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 \
	-DCMAKE_OSX_DEPLOYMENT_TARGET=$MIN_MACOS -DCMAKE_PREFIX_PATH="$DEPS" -DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew \
	-DCMAKE_DISABLE_FIND_PACKAGE_FFMPEG=ON -DENABLE_GSRUNNER=OFF -DENABLE_GT4GS=ON -DENABLE_QT_UI=OFF \
	-DENABLE_TESTS=OFF -DUSE_VULKAN=OFF > "$WORK-gs-configure.log"
cmake --build "$WORK/gs-build" --target pcsx2-gt4gs
rm -rf "$WORK/gs"
"$GS_FORK/gt4gs/package-macos.sh" "$WORK/gs-build" "$WORK/gs"

echo "== launcher"
MACOSX_DEPLOYMENT_TARGET=$MIN_MACOS cargo build --release --locked --manifest-path launcher/Cargo.toml \
	-p gt4l-tauri --target-dir "$WORK/launcher"

echo "== app"
rm -rf "$OUT"
mkdir -p "$OUT"
python3 -I ports/macos/package_app.py --out "$OUT/GT4Recomp.app" --launcher "$WORK/launcher/release/GT4Recomp" \
	--game "$WORK/game/ports/macos/gt4vulkan" --gs "$WORK/gs" --libraries "$DEPS/lib" --min-macos $MIN_MACOS
ZIP="GT4Recomp-$NAME-macos-arm64.zip"
(cd "$OUT" && ditto -c -k --sequesterRsrc --keepParent GT4Recomp.app "$ZIP" && shasum -a 256 "$ZIP" > SHA256SUMS)
cat "$OUT/SHA256SUMS"
echo "release ready: $OUT/$ZIP"
