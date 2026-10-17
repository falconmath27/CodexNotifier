# Dime&Penny

Dime&Penny is a Windows-first Qt desktop companion for managing and visualizing multiple Codex sessions.

This repository currently contains the Task 1 project scaffold:

- `dimepenny_app` - the Qt Widgets desktop application
- `dimepenny_core` - reusable domain and application code
- `dimepenny_bridge` - the future per-session IPC bridge
- `dimepenny_core_tests` - the initial Qt Test target

## Prerequisites

- CMake 3.24 or newer
- A C++20 compiler
- Qt 6 with the Core, Network, SQL, Widgets, and Test modules
- Ninja or another CMake-supported build tool

Use one consistent Windows toolchain. This machine already has the MSYS2 UCRT64 compiler, so the matching MSYS2 packages are the simplest option:

```sh
pacman -S --needed \
  mingw-w64-ucrt-x86_64-cmake \
  mingw-w64-ucrt-x86_64-ninja \
  mingw-w64-ucrt-x86_64-qt6-base
```

Run that command inside the **MSYS2 UCRT64** terminal, not ordinary PowerShell.

## Configure and build

From an MSYS2 UCRT64 terminal opened in this directory:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run tests

```sh
ctest --test-dir build --output-on-failure
```

## Run the scaffolds

```sh
./build/app/DimePenny.exe
./build/bridge/DimePennyBridge.exe
```

The bridge currently prints its version and exits. The desktop target opens a small placeholder window that confirms the Qt application is running.

## Project documents

- [Architecture plan](ARCHITECTURE.md)
- [Learning and implementation plan](PLAN.md)

# CodexNotifier
