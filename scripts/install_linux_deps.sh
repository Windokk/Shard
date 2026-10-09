#!/bin/bash
# Installs the libraries Shard needs on Debian / Ubuntu. SDL3 is built from source (submodules/SDL) and enables its
# X11 / Wayland / audio / input backends only when the matching development packages are found : without them it
# still builds, but it cannot open a window. See https://wiki.libsdl.org/SDL3/README-linux
#
# A compiler, CMake (3.28.2+) and git are expected to be installed already (sudo apt install build-essential cmake git).
set -euo pipefail

SUDO=""
if [ "$(id -u)" -ne 0 ]; then
    SUDO="sudo"
fi

$SUDO apt-get update
$SUDO apt-get install -y --no-install-recommends \
    ninja-build pkg-config zlib1g-dev liburing-dev \
    libasound2-dev libpulse-dev libudev-dev libdbus-1-dev \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev libxi-dev libxss-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev libdecor-0-dev \
    libdrm-dev libgbm-dev libgl1-mesa-dev libegl1-mesa-dev
