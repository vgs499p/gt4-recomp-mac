#!/bin/bash
# Builds the libraries a GT4Recomp.app release bundles, for arm64 and macOS 14.4 (the game's minimum: it waits with
# os_sync_wait_on_address). Homebrew builds its bottles for the running macOS only, so a release linked against them
# would not start on older systems.
#
#   ports/macos/build_release_deps.sh            -> private/macos-release-deps (install prefix; see PREFIX below)
#
# Sources go to private/macos-release-deps-src and must match the hashes below. Versions and hashes are those of
# PCSX2's macOS dependency script (.github/workflows/scripts/macos/build-dependencies.sh in the gt4recomp-gs fork),
# except SDL3, which is 3.4.18 like the game's pin (docs/inputs/slice210-sdl3.json); its hash equals the digest GitHub
# records for the libsdl-org/SDL release asset. SDL keeps its video subsystem (the game opens its window with it).
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
SOURCES="$ROOT/private/macos-release-deps-src"
# Some of these libraries record their install path unquoted in their CMake files (WebP), so the prefix must have no
# spaces: it is private/macos-release-deps, or ~/Library/Caches/GT4Recomp/macos-release-deps when the checkout path has
# spaces (for example under "Application Support"). GT4_RELEASE_DEPS overrides it. release_macos.sh uses the same rule.
PREFIX="${GT4_RELEASE_DEPS:-$ROOT/private/macos-release-deps}"
if [[ "$PREFIX" == *" "* ]]; then
	PREFIX="$HOME/Library/Caches/GT4Recomp/macos-release-deps"
fi
[[ "$PREFIX" != *" "* ]] || { echo "the library prefix must not contain spaces: $PREFIX" >&2; exit 1; }
export MACOSX_DEPLOYMENT_TARGET=14.4
JOBS=$(sysctl -n hw.ncpu)

SDL=3.4.18
FREETYPE=2.14.3
ZSTD=1.5.7
LZ4=1.10.0
LIBPNG=1.6.58
LIBJPEGTURBO=3.2.0
LIBWEBP=1.6.0
PLUTOVG=1.3.2
PLUTOSVG=0.0.7
RAPIDYAML=0.12.1

mkdir -p "$SOURCES"
cd "$SOURCES"
cat > SHASUMS <<EOF
9c75cf16330322c217dedd2e0609f1124f1b54b8633e763467b4684d0f4334a3  SDL3-$SDL.tar.gz
36bc4f1cc413335368ee656c42afca65c5a3987e8768cc28cf11ba775e785a5f  freetype-$FREETYPE.tar.xz
eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3  zstd-$ZSTD.tar.gz
537512904744b35e232912055ccf8ec66d768639ff3abe5788d90d792ec5f48b  lz4-$LZ4.tar.gz
28eb403f51f0f7405249132cecfe82ea5c0ef97f1b32c5a65828814ae0d34775  libpng-$LIBPNG.tar.xz
eee7dea22ed502868017971c86c63c4ed1e6085de0baebfdcc3d3322f00f3eb0  libpng-$LIBPNG-apng.patch.gz
6f30092cef9fb839779646608f4ee14ae3cbac989c47fa05e841b0841f09878e  libjpeg-turbo-$LIBJPEGTURBO.tar.gz
e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564  libwebp-$LIBWEBP.tar.gz
7bd4e79ce18b1d47517e7e91fbb7cf19d4f01942804a519bc7c0bf32b6325dd5  plutovg-$PLUTOVG.tar.gz
78561b571ac224030cdc450ca2986b4de915c2ba7616004a6d71a379bffd15f3  plutosvg-$PLUTOSVG.tar.gz
e9efcdd17f86287748793cf21d106e461fcad8d103a3e5a23632afe93828660d  rapidyaml-$RAPIDYAML-src.tgz
EOF

fetch() { # url file
	[ -f "$2" ] || curl -fsSL -o "$2" "$1"
}
fetch "https://github.com/libsdl-org/SDL/releases/download/release-$SDL/SDL3-$SDL.tar.gz" "SDL3-$SDL.tar.gz"
fetch "https://sourceforge.net/projects/freetype/files/freetype2/$FREETYPE/freetype-$FREETYPE.tar.xz" "freetype-$FREETYPE.tar.xz"
fetch "https://github.com/facebook/zstd/releases/download/v$ZSTD/zstd-$ZSTD.tar.gz" "zstd-$ZSTD.tar.gz"
fetch "https://github.com/lz4/lz4/releases/download/v$LZ4/lz4-$LZ4.tar.gz" "lz4-$LZ4.tar.gz"
fetch "https://downloads.sourceforge.net/project/libpng/libpng16/$LIBPNG/libpng-$LIBPNG.tar.xz" "libpng-$LIBPNG.tar.xz"
fetch "https://download.sourceforge.net/libpng-apng/libpng-$LIBPNG-apng.patch.gz" "libpng-$LIBPNG-apng.patch.gz"
fetch "https://github.com/libjpeg-turbo/libjpeg-turbo/releases/download/$LIBJPEGTURBO/libjpeg-turbo-$LIBJPEGTURBO.tar.gz" "libjpeg-turbo-$LIBJPEGTURBO.tar.gz"
fetch "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-$LIBWEBP.tar.gz" "libwebp-$LIBWEBP.tar.gz"
fetch "https://github.com/sammycage/plutovg/archive/v$PLUTOVG/plutovg-$PLUTOVG.tar.gz" "plutovg-$PLUTOVG.tar.gz"
fetch "https://github.com/sammycage/plutosvg/archive/v$PLUTOSVG/plutosvg-$PLUTOSVG.tar.gz" "plutosvg-$PLUTOSVG.tar.gz"
fetch "https://github.com/biojppm/rapidyaml/releases/download/v$RAPIDYAML/rapidyaml-$RAPIDYAML-src.tgz" "rapidyaml-$RAPIDYAML-src.tgz"
shasum -a 256 --check --strict SHASUMS

rm -rf "$PREFIX" build
mkdir -p "$PREFIX" build
cd build
CMAKE_COMMON=(
	-G Ninja
	-DCMAKE_BUILD_TYPE=Release
	-DCMAKE_OSX_ARCHITECTURES=arm64
	-DCMAKE_OSX_DEPLOYMENT_TARGET=$MACOSX_DEPLOYMENT_TARGET
	-DCMAKE_PREFIX_PATH="$PREFIX"
	-DCMAKE_INSTALL_PREFIX="$PREFIX"
	-DCMAKE_INSTALL_NAME_DIR="$PREFIX/lib"
	-DCMAKE_SHARED_LINKER_FLAGS="-dead_strip -dead_strip_dylibs"
	-DCMAKE_FIND_FRAMEWORK=LAST
	-DCMAKE_IGNORE_PREFIX_PATH=/opt/homebrew
)
build() { # folder, cmake source subfolder, options...
	local folder="$1" source="$2"
	shift 2
	echo "== $folder"
	cmake -S "$folder/$source" -B "$folder/out" "${CMAKE_COMMON[@]}" "$@" > "$folder.log"
	cmake --build "$folder/out" -j "$JOBS" >> "$folder.log"
	cmake --install "$folder/out" >> "$folder.log"
}

tar xf "$SOURCES/SDL3-$SDL.tar.gz"
build "SDL3-$SDL" . -DBUILD_SHARED_LIBS=ON -DSDL_TEST_LIBRARY=OFF -DSDL_EXAMPLES=OFF
tar xf "$SOURCES/zstd-$ZSTD.tar.gz"
build "zstd-$ZSTD" build/cmake -DBUILD_SHARED_LIBS=ON -DZSTD_BUILD_STATIC=OFF -DZSTD_BUILD_PROGRAMS=OFF
tar xf "$SOURCES/lz4-$LZ4.tar.gz"
build "lz4-$LZ4" build/cmake -DBUILD_SHARED_LIBS=ON -DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF
tar xf "$SOURCES/libpng-$LIBPNG.tar.xz"
gzip -dc "$SOURCES/libpng-$LIBPNG-apng.patch.gz" | (cd "libpng-$LIBPNG" && patch -p1 -s)
build "libpng-$LIBPNG" . -DBUILD_SHARED_LIBS=ON -DPNG_STATIC=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF -DPNG_FRAMEWORK=OFF
tar xf "$SOURCES/libjpeg-turbo-$LIBJPEGTURBO.tar.gz"
build "libjpeg-turbo-$LIBJPEGTURBO" . -DENABLE_STATIC=OFF -DENABLE_SHARED=ON
tar xf "$SOURCES/libwebp-$LIBWEBP.tar.gz"
build "libwebp-$LIBWEBP" . -DBUILD_SHARED_LIBS=ON -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF \
	-DWEBP_BUILD_DWEBP=OFF -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
	-DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_WEBPMUX=OFF -DWEBP_BUILD_EXTRAS=OFF
tar xf "$SOURCES/freetype-$FREETYPE.tar.xz"
build "freetype-$FREETYPE" . -DBUILD_SHARED_LIBS=ON -DFT_REQUIRE_ZLIB=ON -DFT_REQUIRE_PNG=ON -DFT_DISABLE_BZIP2=TRUE \
	-DFT_DISABLE_BROTLI=TRUE -DFT_DISABLE_HARFBUZZ=TRUE
tar xf "$SOURCES/plutovg-$PLUTOVG.tar.gz"
build "plutovg-$PLUTOVG" . -DBUILD_SHARED_LIBS=ON -DPLUTOVG_BUILD_EXAMPLES=OFF
tar xf "$SOURCES/plutosvg-$PLUTOSVG.tar.gz"
build "plutosvg-$PLUTOSVG" . -DBUILD_SHARED_LIBS=ON -DPLUTOSVG_ENABLE_FREETYPE=ON -DPLUTOSVG_BUILD_EXAMPLES=OFF
tar xf "$SOURCES/rapidyaml-$RAPIDYAML-src.tgz"
build "rapidyaml-$RAPIDYAML-src" . -DBUILD_SHARED_LIBS=ON

cd "$ROOT"
rm -rf "$SOURCES/build"
echo "release libraries ready: $PREFIX (macOS $MACOSX_DEPLOYMENT_TARGET, arm64)"
