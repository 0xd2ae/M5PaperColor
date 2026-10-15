# Repository Guidelines

## Project Structure & Module Organization

This repository is a minimal ESP-IDF C firmware project for M5Stack PaperColor (ESP32-S3). `main/M5PaperColor.c` contains the currently empty `app_main()` entry point. Register additional source files in `main/CMakeLists.txt`; the root `CMakeLists.txt` defines the ESP-IDF project. `docs/` is available for hardware notes and development instructions. There are currently no tests or assets.

`build/` contains generated output, including the compilation database used by clangd. `sdkconfig` holds active configuration; `sdkconfig.old` is a backup. `.vscode/settings.json` contains machine-specific toolchain, debugger, and serial-port settings.

## Build, Test, and Development Commands

Use the ESP-IDF environment before running commands. The current VS Code setup points to ESP-IDF 6.1; adapt this path for your machine:

```sh
. ${HOME}/.espressif/v6.1/esp-idf/export.sh
idf.py build
idf.py menuconfig
idf.py -p /dev/tty.usbmodem213101 flash monitor
```

`build` compiles firmware, `menuconfig` edits configuration, and `flash monitor` uploads firmware and opens serial logs. Replace the port with your connected device. Exit the monitor with Ctrl+]. The configured target is `esp32s3`.

## Coding Style & Naming Conventions

Use four-space indentation and braces on separate lines, matching the existing C entry point. Prefer `snake_case` for functions and variables and uppercase names for constants. Keep hardware initialization and peripheral logic in focused modules as functionality grows. Use ESP-IDF APIs and check returned error codes. No formatter or linter configuration is currently provided; clangd supplies editor diagnostics.

## Testing Guidelines

No automated testing framework or coverage threshold is configured. Run `idf.py build` for firmware changes and document device checks for affected peripherals. A successful build does not verify screen refresh, power behavior, or audio. When adding automated tests, use descriptive names such as `test_display_init` and document their execution commands.

## Commit & Pull Request Guidelines

This directory has no Git metadata, so no existing commit convention can be inferred. Use short imperative subjects, such as `Add display initialization`. Pull requests should explain behavior changes, configuration updates, build results, and hardware checks. Link relevant issues and include photos for display changes.

## Hardware & Configuration

Consult the [PaperColor documentation](https://docs.m5stack.com/en/core/PaperColor) before assigning pins or enabling peripheral power. Display and microSD share SPI pins. Screen refresh takes approximately 15–30 seconds. Current configuration selects 2 MB flash despite the board's documented 16 MB; review flash size and partition settings before expanding firmware. Keep credentials out of source files and documentation.
