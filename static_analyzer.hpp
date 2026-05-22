#pragma once
#include "function_profile.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <cctype>

// 指令类别匹配模式表
static const std::vector<std::string> PAT_MEMORY = {
    "new ", "new[", "malloc(", "calloc(", "realloc(", "free(",
    "delete ", "delete[", "std::vector", "std::list", "std::map",
    "std::unordered_map", "std::unordered_set", "std::set", "std::deque",
    "std::multimap", "push_back(", "emplace_back(", "insert(", "resize(",
    "reserve(", "std::make_shared", "std::make_unique",
    "memcpy(", "memmove(", "memset(",
};

static const std::vector<std::string> PAT_FPU = {
    "sqrt(", "pow(", "sin(", "cos(", "tan(", "exp(", "log(", "log2(", "log10(",
    "fabs(", "ceil(", "floor(", "round(", "fmod(", "hypot(", "atan2(",
    "std::sqrt", "std::pow", "std::sin", "std::cos", "std::exp", "std::log",
    "float ", "double ", "long double",
    ".0f", ".0,", ".0)", ".0;",
};

static const std::vector<std::string> PAT_IO = {
    "std::cout", "std::cin", "std::cerr", "std::clog",
    "printf(", "scanf(", "fprintf(", "fscanf(", "sprintf(", "sscanf(",
    "fopen(", "fclose(", "fread(", "fwrite(", "fseek(", "ftell(",
    "std::ifstream", "std::ofstream", "std::fstream",
    "std::getline", "getchar(", "putchar(", "puts(", "gets(",
    "read(", "write(", "open(", "close(",
    "socket(", "send(", "recv(", "connect(", "accept(", "bind(",
};

static const std::vector<std::string> PAT_SIMD = {
    "__m128", "__m256", "__m512",
    "_mm_", "_mm256_", "_mm512_",
    "#pragma omp simd", "std::execution::par",
    "std::transform(std::execution",
};

static const std::vector<std::string> PAT_ATOMIC = {
    "std::mutex", "std::lock_guard", "std::unique_lock", "std::shared_lock",
    "std::atomic", "std::condition_variable", "std::barrier", "std::latch",
    "pthread_mutex", "omp_set_lock",
    "__sync_", "__atomic_",
};

// 静态分析器，使用纯文本模式匹配
class StaticAnalyzer {
public:
    std::vector<FunctionProfile> analyze(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open())
            throw std::runtime_error("无法打开文件: " + filepath);

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line))
            lines.push_back(line);

        auto profiles = extract_functions(lines, filepath);
        build_call_graph(profiles, lines);
        return profiles;
    }

private:
    // 去除行尾注释
    static std::string strip_line_comment(const std::string& s) {
        bool in_str = false;
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] == '"' && (i == 0 || s[i-1] != '\\')) in_str = !in_str;
            if (!in_str && i+1 < s.size() && s[i] == '/' && s[i+1] == '/')
                return s.substr(0, i);
        }
        return s;
    }

    static int count_pat(const std::string& s, const std::string& pat) {
        int n = 0;
        size_t pos = 0;
        while ((pos = s.find(pat, pos)) != std::string::npos) { ++n; pos += pat.size(); }
        return n;
    }

    static bool has_any(const std::string& s, const std::vector<std::string>& pats) {
        for (const auto& p : pats)
            if (s.find(p) != std::string::npos) return true;
        return false;
    }

    static int count_any(const std::string& s, const std::vector<std::string>& pats) {
        int n = 0;
        for (const auto& p : pats) n += count_pat(s, p);
        return n;
    }

    // 花括号深度变化
    static int brace_delta(const std::string& raw) {
        std::string s = strip_line_comment(raw);
        int delta = 0;
        bool in_str = false;
        char prev = 0;
        for (char c : s) {
            if (c == '"' && prev != '\\') in_str = !in_str;
            if (!in_str) {
                if (c == '{') ++delta;
                else if (c == '}') --delta;
            }
            prev = c;
        }
        return delta;
    }

    static std::string trim(std::string s) {
        size_t first = s.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            return "";
        size_t last = s.find_last_not_of(" \t\r\n");
        return s.substr(first, last - first + 1);
    }

    static bool starts_with(const std::string& s, const char* prefix) {
        return s.rfind(prefix, 0) == 0;
    }

    static bool has_semicolon_before_body(const std::string& s) {
        size_t semi = s.find(';');
        size_t body = s.find('{');
        return semi != std::string::npos && (body == std::string::npos || semi < body);
    }

    static std::string detect_func_name(const std::string& raw) {
        std::string s = trim(raw);

        if (s.empty() || s[0] == '#') return "";
        for (const char* kw : {"class ", "struct ", "namespace ", "enum ", "union ",
                                "//", "/*", " * ", " */"})
            if (s.rfind(kw, 0) == 0) return "";

        size_t body = s.find('{');
        if (body == std::string::npos)
            return "";

        if (has_semicolon_before_body(s))
            return "";

        std::string head = trim(s.substr(0, body));
        if (head.empty() || head.find('(') == std::string::npos)
            return "";
        if (head.find('=') != std::string::npos)
            return "";

        size_t paren = head.find('(');
        std::string before = head.substr(0, paren);
        size_t ne = before.find_last_not_of(" \t");
        if (ne == std::string::npos) return "";
        size_t ns = before.find_last_of(" \t:>*&~", ne);
        ns = (ns == std::string::npos) ? 0 : ns + 1;
        std::string name = before.substr(ns, ne - ns + 1);

        static const std::set<std::string> kws = {
            "if","else","for","while","do","switch","try","catch","return",
            "case","default","operator"
        };
        if (kws.count(name) || name.empty()) return "";
        if (!std::isalpha((unsigned char)name[0]) && name[0] != '_' && name[0] != '~') return "";
        return name;
    }

    static bool detect_function_at(const std::vector<std::string>& lines,
                                   int start,
                                   std::string& name,
                                   int& body_start) {
        std::string sig;
        int n = (int)lines.size();

        for (int j = start; j < n && j < start + 40; ++j) {
            std::string part = trim(strip_line_comment(lines[j]));
            if (part.empty()) {
                if (sig.empty())
                    return false;
                continue;
            }

            if (sig.empty()) {
                if (part[0] == '#')
                    return false;
                if (starts_with(part, "class ") || starts_with(part, "struct ") ||
                    starts_with(part, "namespace ") || starts_with(part, "enum ") ||
                    starts_with(part, "union "))
                    return false;
            }

            if (!sig.empty())
                sig += ' ';
            sig += part;

            if (has_semicolon_before_body(sig))
                return false;

            if (sig.find('{') != std::string::npos) {
                name = detect_func_name(sig);
                if (name.empty())
                    return false;
                body_start = j;
                return true;
            }
        }
        return false;
    }

    // 分析函数体
    static void analyze_body(const std::vector<std::string>& lines,
                              int start, int end, FunctionProfile& fp) {
        int loop_depth = 0, max_loop_depth = 0, loop_count = 0;

        for (int i = start; i <= end && i < (int)lines.size(); ++i) {
            std::string s = strip_line_comment(lines[i]);

            // 循环检测
            bool is_loop = (s.find("for(")   != std::string::npos ||
                            s.find("for (")  != std::string::npos ||
                            s.find("while(") != std::string::npos ||
                            s.find("while (")!= std::string::npos ||
                            s.find("do {")   != std::string::npos ||
                            s.find("do{")    != std::string::npos);
            if (is_loop) {
                ++loop_count;
                ++loop_depth;
                max_loop_depth = std::max(max_loop_depth, loop_depth);
            }
            if (s.find('}') != std::string::npos && loop_depth > 0)
                --loop_depth;

            // 递归检测 (跳过签名行避免误报)
            if (i > start && !fp.name.empty() &&
                s.find(fp.name + "(") != std::string::npos)
                fp.loops.has_recursion = true;

            // 指令分类计数
            fp.raw.memory += count_any(s, PAT_MEMORY);
            fp.raw.fpu    += count_any(s, PAT_FPU);
            fp.raw.io     += count_any(s, PAT_IO);
            fp.raw.simd   += count_any(s, PAT_SIMD);
            fp.raw.atomic += count_any(s, PAT_ATOMIC);

            // 分支
            if (s.find("if(")     != std::string::npos ||
                s.find("if (")    != std::string::npos ||
                s.find("switch(") != std::string::npos ||
                s.find("switch (")!= std::string::npos ||
                s.find(" ? ")     != std::string::npos)
                ++fp.raw.branch;

            // ALU: 算术/位运算符计数
            for (char c : s)
                if (c=='+' || c=='-' || c=='*' || c=='/' || c=='%' ||
                    c=='&' || c=='|' || c=='^' || c=='~')
                    ++fp.raw.alu;
        }

        fp.loops.depth = max_loop_depth;
        fp.loops.count = loop_count;

        // 保守迭代估计: 100^depth
        int iters = 1;
        for (int d = 0; d < max_loop_depth; ++d) iters *= 100;
        fp.loops.estimated_iters = iters;
    }

    // 提取所有函数
    std::vector<FunctionProfile> extract_functions(
            const std::vector<std::string>& lines,
            const std::string& filepath) {

        std::vector<FunctionProfile> profiles;
        int n = (int)lines.size();
        int i = 0;

        while (i < n) {
            std::string name;
            int body_start = i;
            detect_function_at(lines, i, name, body_start);
            if (name.empty()) { ++i; continue; }

            FunctionProfile fp;
            fp.name       = name;
            fp.file       = filepath;
            fp.line_start = i + 1;

            int depth = 0, j = body_start;
            depth += brace_delta(lines[j]);
            while (j < n && depth > 0) {
                if (j > body_start) depth += brace_delta(lines[j]);
                ++j;
            }
            fp.line_end = j;

            analyze_body(lines, body_start, j - 1, fp);
            profiles.push_back(fp);
            i = j;
        }
        return profiles;
    }

    // 构建调用图
    static void build_call_graph(std::vector<FunctionProfile>& profiles,
                                  const std::vector<std::string>& lines) {
        std::vector<std::string> names;
        for (const auto& fp : profiles) names.push_back(fp.name);

        for (auto& fp : profiles) {
            for (int i = fp.line_start - 1; i < fp.line_end && i < (int)lines.size(); ++i) {
                std::string s = strip_line_comment(lines[i]);
                for (const auto& nm : names) {
                    if (nm == fp.name) continue;
                    if (s.find(nm + "(") != std::string::npos) {
                        if (std::find(fp.callees.begin(), fp.callees.end(), nm)
                                == fp.callees.end())
                            fp.callees.push_back(nm);
                    }
                }
            }
        }
    }
};
