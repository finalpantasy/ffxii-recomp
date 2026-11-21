#!/usr/bin/env bash
# Builds DXVK native (zlib license) as the D3D11 reference backend.
#
# Run inside Linux or WSL. Produces libdxvk_d3d11.so and libdxvk_dxgi.so with
# the SDL3 window backend, plus headers and pkg-config files, under
# $FFXII_DEPS/install. The game process must run with DXVK_WSI_DRIVER=SDL3 and
# pass an SDL_Window* wherever D3D11 expects an HWND.
#
# Needs: git, a C++ compiler, python3 with venv, ninja, glslang (glslangValidator),
# and SDL3 and Vulkan development packages.
set -euo pipefail

DXVK_REV="${DXVK_REV:-d9af36496fef7a5b24f0b916bfd578b9f875d9b1}"
DEPS="${FFXII_DEPS:-$HOME/ffxii-spike}"
SRC="$DEPS/dxvk"
BUILD="$DEPS/build"
PREFIX="$DEPS/install"

mkdir -p "$DEPS"
if [ ! -d "$SRC/.git" ]; then
  git clone https://github.com/doitsujin/dxvk.git "$SRC"
fi
git -C "$SRC" fetch --quiet origin
git -C "$SRC" checkout --quiet "$DXVK_REV"
git -C "$SRC" submodule update --init --recursive

if [ ! -x "$DEPS/venv/bin/meson" ]; then
  python3 -m venv "$DEPS/venv"
  "$DEPS/venv/bin/pip" install --quiet meson
fi

if [ ! -f "$BUILD/build.ninja" ]; then
  "$DEPS/venv/bin/meson" setup "$BUILD" "$SRC" \
    --buildtype=release --prefix="$PREFIX" \
    -Dnative_sdl3=enabled -Dnative_sdl2=disabled -Dnative_glfw=disabled \
    -Denable_d3d8=false -Denable_d3d9=false -Denable_d3d10=false
fi
nice -n 10 ninja -C "$BUILD" install

echo "DXVK native $DXVK_REV installed in $PREFIX"
echo "pkg-config path: $(dirname "$(find "$PREFIX" -name dxvk-d3d11.pc | head -1)")"
