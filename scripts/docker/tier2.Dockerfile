# Tier-2 (Linux) build environment, runnable on the dev machine.
#
# The roadmap already anticipates gates running locally rather than on a hosted
# runner ("a scripted local gate ... recorded in the gate log either way"). This
# is that, for the Linux tier: the same distribution `ubuntu-latest` currently
# resolves to, the same compiler, and the same dependency list
# `.github/workflows/ci.yml` installs.
#
# It exists so a portability break is found in seconds on the machine that
# caused it, instead of minutes later on metered CI. Keeping the two in step is
# a maintenance cost paid deliberately: when the workflow's package list
# changes, this changes with it, and the comment in ci.yml says so.
FROM ubuntu:24.04

# Non-interactive, and no recommends: this image is a build environment, not a
# desktop, and every extra package is a slower rebuild for nothing.
ENV DEBIAN_FRONTEND=noninteractive

# Toolchain. Ubuntu 24.04 ships CMake 3.28, which is exactly the floor the root
# CMakeLists requires -- if that floor ever rises, this base image is the thing
# that has to move first.
RUN apt-get update -qq && apt-get install -y --no-install-recommends \
        build-essential \
        clang \
    # Deliberately NOT clang-tools. CMake 3.28+ would want `clang-scan-deps` to
    # scan C++20 translation units for module dependencies, and Ubuntu ships
    # that binary only under a versioned name, so the default configuration
    # fails here with CMAKE_CXX_COMPILER_CLANG_SCAN_DEPS-NOTFOUND while a hosted
    # runner is perfectly happy. The root CMakeLists turns the scan off instead
    # -- nothing here uses modules -- which removes the work rather than
    # installing a tool to do work nobody needs. This image found that.
        cmake \
        ninja-build \
        git \
        ca-certificates \
        curl \
        unzip \
    # SDL's video dependencies. Taken from third_party/sdl3/docs/README-linux.md
    # rather than assembled by hand: SDL's X11 check is all-or-nothing per
    # feature, so a list built from whatever the last failure named costs one
    # round trip per missing package. Everything video, input-method and DRM
    # related is here; only the audio and joystick packages are dropped, and
    # those match the subsystems third_party/CMakeLists.txt turns off by
    # decision (ADR 0009 for audio, ADR 0029 for input).
    && apt-get install -y --no-install-recommends \
        libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxfixes-dev \
        libxi-dev libxss-dev libxtst-dev libxkbcommon-dev \
        libthai-dev libfribidi-dev \
        libdrm-dev libgbm-dev libgl1-mesa-dev libgles2-mesa-dev libegl1-mesa-dev \
        libdbus-1-dev libibus-1.0-dev libudev-dev \
        libwayland-dev wayland-protocols libdecor-0-dev \
    && rm -rf /var/lib/apt/lists/*

# Out-of-tree (R14), and on a named volume so an incremental run reuses the
# previous one's objects. That is what makes this fast enough to run before
# every push -- the first build is a cold one, every build after it is not.
ENV LUAUG_BUILD_ROOT=/build

WORKDIR /repo
