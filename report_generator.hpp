#pragma once
#include "function_profile.hpp"
#include "carbon_model.hpp"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

// 终端色彩控制
namespace ansi {
    inline const char* RESET  = "\033[0m";
    inline const char* BOLD   = "\033[1m";
    inline const char* DIM    = "\033[2m";
    inline const char* RED    = "\033[38;5;203m";
    inline const char* ORANGE = "\033[38;5;208m";
    inline const char* YELLOW = "\033[38;5;220m";
    inline const char* GREEN  = "\033[38;5;114m";
    inline const char* CYAN   = "\033[38;5;80m";
    inline const char* BLUE   = "\033[38;5;75m";
    inline const char* PURPLE = "\033[38;5;141m";
    inline const char* WHITE  = "\033[38;5;255m";
    inline const char* GRAY   = "\033[38;5;245m";
}

// 报告生成器
class ReportGenerator {
public:
    explicit ReportGenerator(bool color = true) : color_(color) {}

    void print(const ProgramProfile& prog,
               const HardwareProfile& hw,
               const GridRegion& grid,
               std::ostream& out = std::cout) const {
        print_banner(out);
        print_config(prog, hw, grid, out);
        print_totals(prog, out);
        print_functions(prog, out);
        print_notes(out);
    }

private:
    bool color_;

    std::string c(const char* code) const { return color_ ? code : ""; }

    static std::string fmt_co2(double mg) {
        std::ostringstream ss;
        if (mg < 0.001)
            ss << std::fixed << std::setprecision(3) << mg * 1e6 << " ng";
        else if (mg < 1.0)
            ss << std::fixed << std::setprecision(3) << mg * 1000.0 << " μg";
        else if (mg < 1000.0)
            ss << std::fixed << std::setprecision(3) << mg << " mg";
        else
            ss << std::fixed << std::setprecision(4) << mg / 1000.0 << " g";
        return ss.str() + " CO₂eq";
    }

    static std::string fmt_energy(double j) {
        std::ostringstream ss;
        if (j < 1e-9)
            ss << std::fixed << std::setprecision(2) << j * 1e12 << " pJ";
        else if (j < 1e-6)
            ss << std::fixed << std::setprecision(2) << j * 1e9 << " nJ";
        else if (j < 1e-3)
            ss << std::fixed << std::setprecision(2) << j * 1e6 << " μJ";
        else if (j < 1.0)
            ss << std::fixed << std::setprecision(2) << j * 1e3 << " mJ";
        else
            ss << std::fixed << std::setprecision(3) << j << " J";
        return ss.str();
    }

    static std::string fmt_score(double s) {
        std::ostringstream ss;
        if (s >= 1e9)      ss << std::fixed << std::setprecision(1) << s/1e9 << "G";
        else if (s >= 1e6) ss << std::fixed << std::setprecision(1) << s/1e6 << "M";
        else if (s >= 1e3) ss << std::fixed << std::setprecision(1) << s/1e3 << "K";
        else               ss << std::fixed << std::setprecision(0) << s;
        return ss.str();
    }

    // 渐变进度条
    std::string make_bar(double pct, int width = 30) const {
        int filled = static_cast<int>(std::round(pct * width));
        filled = std::max(0, std::min(filled, width));

        std::string bar;
        for (int k = 0; k < filled; ++k) {
            if (pct > 0.6)       bar += c(ansi::RED);
            else if (pct > 0.3)  bar += c(ansi::ORANGE);
            else                 bar += c(ansi::GREEN);
            bar += "#";
        }
        bar += c(ansi::GRAY);
        for (int k = filled; k < width; ++k)
            bar += "-";
        bar += c(ansi::RESET);
        return bar;
    }

    // 热度标签
    std::string heat_tag(double pct) const {
        if (pct > 0.6)  return c(ansi::RED)    + std::string("高耗") + c(ansi::RESET);
        if (pct > 0.3)  return c(ansi::ORANGE) + std::string("中等") + c(ansi::RESET);
        if (pct > 0.1)  return c(ansi::YELLOW) + std::string("轻载") + c(ansi::RESET);
        return                  c(ansi::GREEN)  + std::string("极低") + c(ansi::RESET);
    }

    void print_banner(std::ostream& out) const {
        out << "\n";
        out << c(ansi::CYAN) << c(ansi::BOLD);
        out << "  GreenComputing 碳排放静态分析器\n";
        out << c(ansi::RESET) << "\n";
    }

    void print_config(const ProgramProfile& prog,
                      const HardwareProfile& hw,
                      const GridRegion& grid,
                      std::ostream& out) const {
        out << c(ansi::GRAY) << "  分析配置" << c(ansi::RESET) << "\n";
        out << c(ansi::GRAY) << "  " << c(ansi::RESET)
            << "源文件  " << c(ansi::WHITE) << prog.source_file << c(ansi::RESET) << "\n";
        out << c(ansi::GRAY) << "  " << c(ansi::RESET)
            << "硬件    " << c(ansi::WHITE) << hw.name
            << c(ansi::GRAY) << "  TDP " << hw.tdp_watts << "W" << c(ansi::RESET) << "\n";
        out << c(ansi::GRAY) << "  " << c(ansi::RESET)
            << "电网    " << c(ansi::WHITE) << grid.name
            << c(ansi::GRAY) << "  " << grid.carbon_intensity << " gCO₂/kWh" << c(ansi::RESET) << "\n";
        out << c(ansi::GRAY) << "  " << c(ansi::RESET)
            << "函数数  " << c(ansi::WHITE) << prog.functions.size() << c(ansi::RESET) << "\n";
        out << "\n";
    }

    void print_totals(const ProgramProfile& prog, std::ostream& out) const {
        out << c(ansi::BOLD) << c(ansi::WHITE)
            << "  总体碳足迹估算"
            << c(ansi::RESET) << "\n";
        out << "  能耗评分  " << c(ansi::CYAN) << fmt_score(prog.total_energy_score)
            << c(ansi::GRAY) << "  (加权指令单位)" << c(ansi::RESET) << "\n";
        out << "  能量消耗  " << c(ansi::CYAN) << fmt_energy(prog.total_joules)
            << c(ansi::GRAY) << "  /次执行" << c(ansi::RESET) << "\n";
        out << "  碳排放量  " << c(ansi::CYAN) << fmt_co2(prog.total_co2_mg)
            << c(ansi::GRAY) << "  /次执行" << c(ansi::RESET) << "\n";

        double led_s = prog.total_joules / 10.0;
        double searches = prog.total_co2_mg / 1000.0 / 0.3;
        out << c(ansi::GRAY) << "  约 " << std::fixed << std::setprecision(4)
            << led_s << "s 10W灯泡, "
            << std::setprecision(5) << searches << " 次搜索"
            << c(ansi::RESET) << "\n";
        out << "\n";
    }

    void print_functions(const ProgramProfile& prog, std::ostream& out) const {
        if (prog.functions.empty()) return;

        double max_co2 = prog.functions.front().estimated_co2_mg;
        if (max_co2 <= 0.0) max_co2 = 1.0;

        out << c(ansi::BOLD) << c(ansi::WHITE)
            << "  函数碳热点排名"
            << c(ansi::RESET) << "\n";

        int rank = 1;
        for (const auto& fp : prog.functions) {
            double pct = fp.estimated_co2_mg / max_co2;
            double share = (prog.total_co2_mg > 0)
                           ? fp.estimated_co2_mg / prog.total_co2_mg * 100.0 : 0.0;

            out << "\n";

            // 排名 + 函数名
            out << "  " << c(ansi::BOLD) << c(ansi::WHITE)
                << std::setw(2) << rank++ << "  " << fp.name << c(ansi::RESET);
            out << c(ansi::GRAY) << "  " << fp.file << ":"
                << fp.line_start << "~" << fp.line_end << c(ansi::RESET) << "\n";

            // 进度条 + 热度 + 碳排放
            out << "      " << make_bar(pct) << "  " << heat_tag(pct)
                << "  " << c(ansi::CYAN) << fmt_co2(fp.estimated_co2_mg) << c(ansi::RESET)
                << c(ansi::GRAY) << "  " << std::fixed << std::setprecision(1)
                << share << "%" << c(ansi::RESET) << "\n";

            // 指标行
            out << c(ansi::GRAY) << "      "
                << "评分:" << fmt_score(fp.energy_score)
                << "  能量:" << fmt_energy(fp.estimated_joules)
                << "  循环深度:" << fp.loops.depth
                << "  内存:" << fp.raw.memory
                << "  浮点:" << fp.raw.fpu
                << "  IO:" << fp.raw.io;
            if (fp.loops.has_recursion) out << "  [递归]";
            out << c(ansi::RESET) << "\n";

            // 调用关系
            if (!fp.callees.empty()) {
                out << c(ansi::GRAY) << "      调用: ";
                for (size_t i = 0; i < fp.callees.size(); ++i) {
                    if (i) out << ", ";
                    out << fp.callees[i];
                }
                out << c(ansi::RESET) << "\n";
            }

            // 警告
            for (const auto& w : fp.warnings)
                out << "      " << c(ansi::RED) << "警告: " << w << c(ansi::RESET) << "\n";

            // 建议
            for (const auto& s : fp.suggestions)
                out << "      " << c(ansi::GREEN) << "建议: " << s << c(ansi::RESET) << "\n";
        }
        out << "\n";
    }

    void print_notes(std::ostream& out) const {
        out << c(ansi::GRAY)
            << "  说明\n"
            << "  估算基于静态代码模式 + 硬件TDP功耗模型\n"
            << "  循环迭代次数假设每层 N=100\n"
            << "  精确测量请使用 RAPL 或 perf_events\n"
            << c(ansi::RESET) << "\n";
    }
};
