#pragma once
#include <string>
#include <unordered_map>
#include <vector>

// 硬件功耗配置
struct HardwareProfile {
    std::string name;
    double tdp_watts;       // 热设计功耗 (W)
    double idle_watts;      // 空闲功耗 (W)
    double mem_coeff;       // 内存操作额外功耗系数
    double fp_coeff;        // 浮点操作额外功耗系数
    double io_coeff;        // IO操作额外功耗系数
};

inline const std::unordered_map<std::string, HardwareProfile> HARDWARE_PROFILES = {
    {"rpi4",         {"Raspberry Pi 4",               6.4,   2.0,  0.3,  0.5,  0.8}},
    {"laptop_low",   {"Laptop (低功耗, ~15W TDP)",     15.0,  5.0,  0.8,  1.2,  2.0}},
    {"laptop_mid",   {"Laptop (中端, ~28W TDP)",       28.0,  8.0,  1.2,  1.8,  3.0}},
    {"desktop_mid",  {"Desktop (中端, ~65W TDP)",      65.0, 15.0,  2.0,  3.0,  5.0}},
    {"desktop_high", {"Desktop (高端, ~125W TDP)",    125.0, 25.0,  3.5,  5.0,  8.0}},
    {"server_1u",    {"Server 1U",                   200.0, 60.0,  5.0,  7.0, 12.0}},
    {"server_hpc",   {"Server HPC 节点",              400.0,100.0, 10.0, 15.0, 20.0}},
};

// 电网碳强度 (gCO₂eq/kWh)
// 数据来源: IEA 2023, Electricity Maps
struct GridRegion {
    std::string name;
    double carbon_intensity;
};

inline const std::unordered_map<std::string, GridRegion> GRID_REGIONS = {
    {"cn",     {"中国",           581.0}},
    {"us",     {"美国 (均值)",     386.0}},
    {"us_ca",  {"美国 加州",       210.0}},
    {"us_tx",  {"美国 德州",       420.0}},
    {"eu",     {"欧盟 (均值)",     255.0}},
    {"de",     {"德国",           350.0}},
    {"fr",     {"法国",            58.0}},
    {"no",     {"挪威",            26.0}},
    {"uk",     {"英国",           233.0}},
    {"jp",     {"日本",           474.0}},
    {"au",     {"澳大利亚",       560.0}},
    {"br",     {"巴西",           100.0}},
    {"in",     {"印度",           708.0}},
    {"global", {"全球均值",        436.0}},
};

// 指令类别能耗权重 (相对于基准整数ALU操作)
enum class InstrCat { ALU, FPU, MEMORY, BRANCH, IO, SIMD, ATOMIC };

struct InstrWeight {
    InstrCat    cat;
    double      multiplier;
    const char* label;
};

inline const std::vector<InstrWeight> INSTR_WEIGHTS = {
    {InstrCat::ALU,    1.0,  "整数运算"},
    {InstrCat::FPU,    2.5,  "浮点运算"},
    {InstrCat::MEMORY, 8.0,  "堆/缓存未命中"},
    {InstrCat::BRANCH, 0.5,  "分支/条件"},
    {InstrCat::IO,    50.0,  "IO系统调用"},
    {InstrCat::SIMD,   0.3,  "SIMD (每元素)"},
    {InstrCat::ATOMIC, 4.0,  "原子/同步"},
};

inline double weight_of(InstrCat cat) {
    for (const auto& w : INSTR_WEIGHTS)
        if (w.cat == cat) return w.multiplier;
    return 1.0;
}
