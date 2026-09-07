"""Select the pinned ESP32-S3 compiler without Windows alias launchers.

The package's chip-specific executables are Rust forwarding launchers. Some
restricted Windows runners cannot resolve those launchers' paths. Calling the
same package's generic tools with its explicit ESP32-S3 configuration avoids
that launcher; it does not replace or update the compiler toolchain.

Run as both a pre and post extra script. A pre build middleware reaches cloned
library/framework environments before their object actions are constructed;
the post invocation configures the final link and size commands as well.
"""

import os
import subprocess
from os.path import dirname, join

Import("env")


def configure_windows_xtensa(build_env):
    if os.name != "nt" or build_env.BoardConfig().get("build.mcu") != "esp32s3":
        return
    toolchain_dir = build_env.PioPlatform().get_package_dir("toolchain-xtensa-esp-elf")
    if not toolchain_dir:
        raise RuntimeError("The pinned Xtensa toolchain is not installed")
    build_env.Replace(
        AR="xtensa-esp-elf-gcc-ar",
        AS="xtensa-esp-elf-as",
        CC="xtensa-esp-elf-gcc",
        CXX="xtensa-esp-elf-g++",
        RANLIB="xtensa-esp-elf-gcc-ranlib",
        SIZETOOL="xtensa-esp-elf-size",
    )
    build_env["ENV"]["XTENSA_GNU_CONFIG"] = join(
        toolchain_dir, "lib", "xtensa_esp32s3.so")
    # The environment selects the ISA configuration; this exact driver option
    # also selects the matching ESP32-S3 C/C++ runtime multilib at link time.
    build_env.AppendUnique(
        CCFLAGS=["-mdynconfig=xtensa_esp32s3.so"],
        LINKFLAGS=["-mdynconfig=xtensa_esp32s3.so"],
        ASFLAGS=["--dynconfig=xtensa_esp32s3.so"],
    )
    # Keep build-machine usernames/directories out of __FILE__ and debug paths.
    # Apply to every compile environment, including framework and dependencies.
    for original, replacement in (
        (build_env.subst("$PROJECT_DIR"), "/transitink"),
        (dirname(dirname(toolchain_dir)), "/platformio"),
    ):
        for path in dict.fromkeys([original, original.replace("\\", "/")]):
            build_env.AppendUnique(CCFLAGS=["-ffile-prefix-map=" + path + "=" + replacement])


def configure_source_compiler(build_env, node):
    configure_windows_xtensa(build_env)
    return node


def create_factory_image_with_module(source, target, env):
    """The platform's offline merge step, using its same installed Python package."""
    board = env.BoardConfig()
    firmware = env.subst("$BUILD_DIR/${PROGNAME}.bin")
    factory = env.subst("$BUILD_DIR/${PROGNAME}.factory.bin")
    command = [env.subst("$PYTHONEXE"), "-m", "esptool",
               "--chip", board.get("build.mcu"), "merge-bin", "-o", factory,
               "--flash-mode", env.subst("${__get_board_flash_mode(__env__)}"),
               "--flash-freq", env.subst("${__get_board_f_image(__env__)}"),
               "--flash-size", board.get("upload.flash_size", "4MB")]
    for offset, path in env.get("FLASH_EXTRA_IMAGES", []):
        command.extend([str(offset), env.subst(path)])
    command.extend([env.subst("$ESP32_APP_OFFSET") or "0x10000", firmware])
    return subprocess.run(command, check=False).returncode


def module_image_action(action):
    function = getattr(action, "execfunction", None)
    if getattr(function, "__name__", "") == "esp32_create_combined_bin":
        return env.VerboseAction(
            create_factory_image_with_module,
            "Creating offline factory image with the pinned esptool module")
    return action


configure_windows_xtensa(env)
if not env.get("_TRANSITINK_WINDOWS_COMPILER_MIDDLEWARE"):
    env.AddBuildMiddleware(configure_source_compiler)
    env["_TRANSITINK_WINDOWS_COMPILER_MIDDLEWARE"] = True

if os.name == "nt" and env.BoardConfig().get("build.mcu") == "esp32s3":
    # Use the platform's normal module entrypoint for offline image creation.
    # No security policy or installed console launcher is modified.
    # This pinned platform calls its firmware converter through ERASETOOL too;
    # both variables here select a CLI, not an operation. This hook runs no
    # device commands: normal build targets invoke only elf2image / merge-bin.
    env.Replace(OBJCOPY='"$PYTHONEXE" -m esptool',
                ERASETOOL='"$PYTHONEXE" -m esptool')
    if env.get("PIOMAINPROG"):
        # PlatformIO defers actions for files that do not yet exist (clean
        # builds), but registers them immediately for incremental builds.
        env["_PIO_DELAYED_ACTIONS"] = [
            (handler, nodes, module_image_action(action))
            for handler, nodes, action in env.get("_PIO_DELAYED_ACTIONS", [])]
        image = env.File(env.subst("$BUILD_DIR/${PROGNAME}.bin"))
        executor = image.get_executor()
        for index, action in enumerate(executor.post_actions):
            executor.post_actions[index] = module_image_action(action)
