#include <iostream>
#include <iomanip>
#include <string>
#include <filesystem>

#include "carbon_model.hpp"
#include "function_profile.hpp"
#include "static_analyzer.hpp"
#include "energy_estimator.hpp"
#include "report_generator.hpp"

static void print_usage(const char* argv0) {
    std::cerr <<
        "用法: " << argv0 << " <source.cpp> [选项]\n\n"
        "选项:\n"
        "  --hw <key>      硬件配置 (默认: laptop_mid)\n"
        "  --grid <key>    电网区域 (默认: global)\n"
        "  --no-color      禁用终端颜色\n"
        "  --list-hw       列出可用硬件配置\n"
        "  --list-grids    列出可用电网区域\n\n"
        "硬件配置:\n"
        "  rpi4, laptop_low, laptop_mid, desktop_mid, desktop_high,\n"
        "  server_1u, server_hpc\n\n"
        "电网区域:\n"
        "  cn, us, us_ca, us_tx, eu, de, fr, no, uk, jp, au, br, in, global\n";
}

static void list_hw() {
    std::cout << "\n可用硬件配置:\n";
    for (const auto& [key, hw] : HARDWARE_PROFILES)
        std::cout << "  " << std::left << std::setw(14) << key
                  << "  " << hw.name
                  << "  (TDP " << hw.tdp_watts << " W)\n";
    std::cout << "\n";
}

static void list_grids() {
    std::cout << "\n可用电网区域:\n";
    for (const auto& [key, g] : GRID_REGIONS)
        std::cout << "  " << std::left << std::setw(8) << key
                  << "  " << g.name
                  << "  (" << g.carbon_intensity << " gCO₂eq/kWh)\n";
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
    std::string source_file;
    std::string hw_key   = "laptop_mid";
    std::string grid_key = "global";
    bool use_color = true;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--hw" && i + 1 < argc) {
            hw_key = argv[++i];
        } else if (arg == "--grid" && i + 1 < argc) {
            grid_key = argv[++i];
        } else if (arg == "--no-color") {
            use_color = false;
        } else if (arg == "--list-hw") {
            list_hw(); return 0;
        } else if (arg == "--list-grids") {
            list_grids(); return 0;
        } else if (arg[0] != '-') {
            source_file = arg;
        } else {
            std::cerr << "未知选项: " << arg << "\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // 未指定文件时自动查找 demo.cpp
    if (source_file.empty()) {
        if (std::filesystem::exists("demo.cpp"))
            source_file = "demo.cpp";
        else if (std::filesystem::exists("../demo.cpp"))
            source_file = "../demo.cpp";
        else {
            std::cerr << "错误: 未指定源文件\n";
            print_usage(argv[0]);
            return 1;
        }
    }

    // 从构建目录运行时尝试上级目录
    if (!std::filesystem::exists(source_file)) {
        std::string parent_path = "../" + source_file;
        if (std::filesystem::exists(parent_path))
            source_file = parent_path;
    }

    // 解析硬件配置
    auto hw_it = HARDWARE_PROFILES.find(hw_key);
    if (hw_it == HARDWARE_PROFILES.end()) {
        std::cerr << "未知硬件配置: " << hw_key << "\n";
        list_hw();
        return 1;
    }
    const HardwareProfile& hw = hw_it->second;

    // 解析电网区域
    auto grid_it = GRID_REGIONS.find(grid_key);
    if (grid_it == GRID_REGIONS.end()) {
        std::cerr << "未知电网区域: " << grid_key << "\n";
        list_grids();
        return 1;
    }
    const GridRegion& grid = grid_it->second;

    // 静态分析
    StaticAnalyzer analyzer;
    std::vector<FunctionProfile> functions;
    try {
        functions = analyzer.analyze(source_file);
    } catch (const std::exception& e) {
        std::cerr << "分析错误: " << e.what() << "\n";
        return 1;
    }

    if (functions.empty()) {
        std::cerr << "未在 " << source_file << " 中找到函数\n";
        return 1;
    }

    // 能耗与碳排放估算
    ProgramProfile prog;
    prog.source_file  = source_file;
    prog.hardware_key = hw_key;
    prog.grid_key     = grid_key;
    prog.functions    = std::move(functions);

    EnergyEstimator estimator(hw, grid);
    estimator.estimate_all(prog);

    // 生成报告
    ReportGenerator reporter(use_color);
    reporter.print(prog, hw, grid, std::cout);

    return 0;
}
