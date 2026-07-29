# The NextUI toolchains ship gcc, ninja and the target sysroot, but no Meson.
# Adding it in a cached layer keeps builds fast.

ARG TOOLCHAIN=ghcr.io/loveretro/my355-toolchain:latest
FROM ${TOOLCHAIN}

RUN apt-get update \
 && apt-get install -y --no-install-recommends meson \
 && rm -rf /var/lib/apt/lists/*
