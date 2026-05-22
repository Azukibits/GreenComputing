# GreenComputing

[中文文档](README.zh-CN.md)

![GreenComputing screenshot](img1.png)

GreenComputing is a C++ static analysis tool for estimating function-level energy usage and carbon emissions. It includes a graphical interface on macOS, Windows, and Linux, plus a command-line interface.

The analyzer reads C++ source files, identifies functions, estimates instruction-category activity, applies a hardware power model, and reports relative carbon hotspots across the program.

## Features

- Function-level energy and carbon estimation
- Desktop interface on macOS, Windows, and Linux
- Command-line interface for quick analysis
- Carbon hotspot ranking
- Function comparison chart
- Instruction-category breakdowns
- Hardware profile and grid-region selection
- Call relationship summary for detected functions

## Requirements

- CMake 3.20 or later
- C++20 compiler
- Apple Command Line Tools or Xcode
- Qt 6 for Windows/Linux GUI builds

## Build

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target GreenComputing GreenComputingCLI
```

## Run

Graphical interface on macOS:

```bash
open ./cmake-build-debug/GreenComputing.app
```

Graphical interface on Windows/Linux:

```bash
./build/GreenComputing
```

Command-line interface:

```bash
./cmake-build-debug/GreenComputingCLI demo.cpp --no-color
```

You can also analyze another source file:

```bash
./cmake-build-debug/GreenComputingCLI path/to/source.cpp --hw laptop_mid --grid global
```

## Project Structure

```text
CMakeLists.txt          Build configuration
main.cpp                Command-line entry point
gui_main.mm             macOS Cocoa interface
qt_main.cpp             Windows/Linux Qt interface
static_analyzer.hpp     Source parser and function analyzer
energy_estimator.hpp    Energy and carbon estimation model
carbon_model.hpp        Hardware and grid-region data
function_profile.hpp    Shared analysis data structures
report_generator.hpp    CLI report formatting
demo.cpp                Example source file
```

## Release Packages

- `GreenComputing-macos.dmg`: macOS GUI app and CLI
- `GreenComputing-windows.zip`: Windows GUI app and CLI
- `GreenComputing-linux.tar.gz`: Linux GUI app and CLI

## Estimation Model

GreenComputing uses a lightweight static model. It detects common source-code patterns and groups them into instruction categories such as:

- integer arithmetic
- floating-point operations
- memory and container operations
- branches
- I/O
- SIMD usage
- synchronization primitives

The estimated instruction activity is scaled by loop depth and mapped to energy usage using the selected hardware profile. Carbon emissions are then estimated from the selected grid carbon intensity.

## Limitations

This project provides an approximate static estimate rather than a hardware measurement. Results should be interpreted as relative hotspot guidance, not as exact runtime energy data.

For precise measurement, use platform counters or profiling tools such as RAPL, `perf`, Instruments, or vendor-specific power telemetry.

## License

See [LICENSE](LICENSE).
