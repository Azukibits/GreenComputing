#pragma once
#include "function_profile.hpp"
#include "carbon_model.hpp"
#include <cmath>
#include <algorithm>

// 能耗估算器
// 将原始指令计数转换为焦耳和碳排放估算值
//
// 模型流程:
//   1. 加权指令评分 = Σ(count_i × weight_i) × loop_iters
//   2. 活跃功率比例 = clamp(score / NORM, 0, 1)
//   3. 功率 (W)    = idle + fraction × (TDP - idle) + 分类附加
//   4. 执行时间 (s) = score / THROUGHPUT_HZ
//   5. 能量 (J)    = 功率 × 时间
//   6. CO₂ (mg)   = (J / 3.6e6) × 碳强度 × 1000

class EnergyEstimator {
public:
    EnergyEstimator(const HardwareProfile& hw, const GridRegion& grid)
        : hw_(hw), grid_(grid) {}

    void estimate(FunctionProfile& fp) const {
        // 加权评分
        double score = 0.0;
        score += fp.raw.alu    * weight_of(InstrCat::ALU);
        score += fp.raw.fpu    * weight_of(InstrCat::FPU);
        score += fp.raw.memory * weight_of(InstrCat::MEMORY);
        score += fp.raw.branch * weight_of(InstrCat::BRANCH);
        score += fp.raw.io     * weight_of(InstrCat::IO);
        score += fp.raw.simd   * weight_of(InstrCat::SIMD);
        score += fp.raw.atomic * weight_of(InstrCat::ATOMIC);

        // 循环放大
        score *= fp.loops.estimated_iters;

        // 递归惩罚 (假设最多50次递归调用)
        if (fp.loops.has_recursion) score *= 50.0;

        fp.energy_score = score;

        // 映射到活跃功率比例
        constexpr double NORM             = 1000.0;
        constexpr double TYPICAL_FRACTION = 0.10;
        double frac = std::min((score / NORM) * TYPICAL_FRACTION, 1.0);

        // 总功率
        double power_w = hw_.idle_watts
            + frac * (hw_.tdp_watts - hw_.idle_watts)
            + fp.raw.memory * hw_.mem_coeff * 0.001
            + fp.raw.fpu    * hw_.fp_coeff  * 0.001
            + fp.raw.io     * hw_.io_coeff  * 0.001;

        // 执行时间 (假设1GHz有效吞吐)
        constexpr double THROUGHPUT_HZ = 1e9;
        double exec_s = score / THROUGHPUT_HZ;

        // 能量
        fp.estimated_joules = power_w * exec_s;

        // CO₂ (1 kWh = 3,600,000 J)
        double kwh = fp.estimated_joules / 3.6e6;
        fp.estimated_co2_mg = kwh * grid_.carbon_intensity * 1000.0;
    }

    void estimate_all(ProgramProfile& prog) const {
        prog.total_energy_score = 0.0;
        prog.total_joules       = 0.0;
        prog.total_co2_mg       = 0.0;

        for (auto& fp : prog.functions) {
            estimate(fp);
            prog.total_energy_score += fp.energy_score;
            prog.total_joules       += fp.estimated_joules;
            prog.total_co2_mg       += fp.estimated_co2_mg;
        }

        // 按碳排放降序排列
        std::sort(prog.functions.begin(), prog.functions.end(),
            [](const FunctionProfile& a, const FunctionProfile& b){
                return a.estimated_co2_mg > b.estimated_co2_mg;
            });

        // 标记热点并附加建议
        for (size_t i = 0; i < prog.functions.size(); ++i) {
            auto& fp = prog.functions[i];
            fp.is_hotspot = (i < 5);
            attach_suggestions(fp);
        }
    }

private:
    HardwareProfile hw_;
    GridRegion      grid_;

    static void attach_suggestions(FunctionProfile& fp) {
        if (fp.loops.depth >= 3)
            fp.warnings.push_back("三重嵌套循环，O(N^3) 能耗增长");
        if (fp.loops.depth >= 2)
            fp.suggestions.push_back("考虑循环分块或算法降阶 (O(N^2) 到 O(N log N))");
        if (fp.loops.has_recursion)
            fp.warnings.push_back("无界递归，建议记忆化或改为迭代");
        if (fp.raw.memory > 10)
            fp.suggestions.push_back("堆分配频繁，优先使用栈/内存池/对象复用");
        if (fp.raw.io > 0)
            fp.suggestions.push_back("热路径含IO，批量化或移出循环");
        if (fp.raw.fpu > 20 && fp.raw.simd == 0)
            fp.suggestions.push_back("浮点密集但无SIMD，启用 -O2/AVX 或 std::execution::par");
        if (fp.raw.atomic > 5)
            fp.suggestions.push_back("同步开销大，减小锁粒度或使用无锁结构");
        if (fp.raw.memory > 5 && fp.loops.depth >= 1)
            fp.warnings.push_back("循环内堆分配，可能导致缓存抖动");
    }
};
