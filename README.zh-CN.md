# GreenComputing

[English README](README.md)

![GreenComputing 界面截图](img1.png)

GreenComputing 是一个用于估算 C++ 函数级能耗和碳排放的静态分析工具。项目包含 macOS、Windows、Linux 图形界面和跨平台命令行工具，可以帮助你快速发现代码中的相对碳排放热点。

分析器会读取 C++ 源文件，识别函数、估算指令类别活动量、套用硬件功耗模型，并根据所选电网区域的碳强度给出能耗和碳排放估算。

## 功能

- 函数级能耗与碳排放估算
- macOS、Windows、Linux 图形界面
- 图形界面支持中文和英文切换
- Windows、Linux、macOS 命令行版本
- 碳排放热点排名
- 函数对比折线图
- 指令类别贡献拆分
- 硬件配置和电网区域选择
- 函数调用关系摘要
- 自适应窗口布局

## 环境要求

- CMake 3.20 或更高版本
- 支持 C++20 的编译器
- macOS 图形界面需要 Apple Command Line Tools 或 Xcode
- Windows/Linux 图形界面构建需要 Qt 6

## 构建

macOS 会同时构建图形界面和命令行工具：

```bash
cmake -S . -B cmake-build-debug
cmake --build cmake-build-debug --target GreenComputing GreenComputingCLI
```

Windows 和 Linux 会构建 Qt 图形界面和命令行工具：

```bash
cmake -S . -B build
cmake --build build --target GreenComputing GreenComputingCLI --config Release
```

## 运行

图形界面：

```bash
open ./cmake-build-debug/GreenComputing.app
```

Windows/Linux 图形界面：

```bash
./build/GreenComputing
```

命令行：

```bash
./cmake-build-debug/GreenComputingCLI demo.cpp --no-color
```

分析其他源文件：

```bash
./cmake-build-debug/GreenComputingCLI path/to/source.cpp --hw laptop_mid --grid global
```

## 项目结构

```text
CMakeLists.txt          构建配置
main.cpp                命令行入口
gui_main.mm             macOS Cocoa 图形界面
qt_main.cpp             Windows/Linux Qt 图形界面
static_analyzer.hpp     源码解析与函数分析
energy_estimator.hpp    能耗和碳排放估算模型
carbon_model.hpp        硬件与电网区域数据
function_profile.hpp    分析数据结构
report_generator.hpp    命令行报告输出
demo.cpp                示例源文件
img1.png                项目界面截图
```

## 估算模型

GreenComputing 使用轻量级静态模型。它会检测常见源码模式，并将其归类到整数运算、浮点运算、内存和容器操作、分支、I/O、SIMD、同步原语等指令类别。

估算出的指令活动量会根据循环深度放大，再映射到所选硬件配置的功耗模型。碳排放量根据所选电网区域的碳强度计算。

## 局限性

本项目提供的是近似静态估算，不是硬件实测数据。结果适合用于相对热点定位和优化优先级判断，不适合作为精确运行时能耗数据。

如果需要精确测量，请使用 RAPL、`perf`、Instruments 或硬件厂商提供的功耗遥测工具。

## 发布包

GitHub Release 会提供：

- macOS 包：`GreenComputing-macos.dmg`，包含图形界面和命令行工具
- Linux 包：`GreenComputing-linux.tar.gz`，包含图形界面和命令行工具
- Windows 包：`GreenComputing-windows.zip`，包含图形界面和命令行工具

## 许可证

本项目使用仓库中的 `LICENSE` 文件。
