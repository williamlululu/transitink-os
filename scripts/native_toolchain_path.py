"""Optionally expose an isolated native compiler to PlatformIO tests."""

import os

Import("env")

toolchain_bin = os.environ.get("TRANSITINK_NATIVE_TOOLCHAIN_BIN")
if toolchain_bin:
    env.PrependENVPath("PATH", toolchain_bin)
