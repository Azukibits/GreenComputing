#pragma once
#include <string>
#include <vector>
#include <cstdint>

// 静态分析提取的指令计数
struct InstrCounts {
    uint64_t alu    = 0;  // 整数算术/逻辑
    uint64_t fpu    = 0;  // 浮点运算
    uint64_t memory = 0;  // 堆分配、指针解引用、容器操作
    uint64_t branch = 0;  // if/switch/三目
    uint64_t io     = 0;  // 文件/网络/标准IO
    uint64_t simd   = 0;  // SIMD指令
    uint64_t atomic = 0;  // 互斥锁/原子操作/同步原语

    uint64_t total() const {
        return alu + fpu + memory + branch + io + simd + atomic;
    }
};

// 循环/递归信息
struct LoopInfo {
    int  depth          = 0;    // 最大嵌套深度
    int  count          = 0;    // 循环结构数量
    bool has_recursion  = false;
    int  estimated_iters = 1;   // 保守估计 N^depth
};

// 单函数分析结果
struct FunctionProfile {
    std::string name;
    std::string file;
    int         line_start = 0;
    int         line_end   = 0;

    InstrCounts raw;
    LoopInfo    loops;

    // 由 EnergyEstimator 计算
    double energy_score    = 0.0;  // 加权指令评分
    double estimated_joules = 0.0; // 单次调用能耗 (J)
    double estimated_co2_mg = 0.0; // 单次调用碳排放 (mg)

    // 调用图
    std::vector<std::string> callees;

    // 报告标注
    bool is_hotspot = false;
    std::vector<std::string> warnings;
    std::vector<std::string> suggestions;
};

// 整体程序分析结果
struct ProgramProfile {
    std::string source_file;
    std::string hardware_key;
    std::string grid_key;

    std::vector<FunctionProfile> functions;

    double total_energy_score = 0.0;
    double total_joules       = 0.0;
    double total_co2_mg       = 0.0;
};
