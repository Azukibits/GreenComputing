#pragma once
#include "function_profile.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <system_error>
#include <cctype>

// 指令类别匹配模式表
static const std::vector<std::string> PAT_MEMORY = {
    "new ", "new[", "malloc(", "calloc(", "realloc(", "free(",
    "delete ", "delete[", "std::vector", "std::list", "std::map",
    "std::unordered_map", "std::unordered_set", "std::set", "std::deque",
    "std::multimap", "push_back(", "emplace_back(", "insert(", "resize(",
    "reserve(", "std::make_shared", "std::make_unique",
    "memcpy(", "memmove(", "memset(",
    "ArrayList", "LinkedList", "HashMap", "HashSet", "TreeMap", "TreeSet",
    "StringBuilder", "ByteBuffer", "List<", "Map<", "Set<",
    "new Array", "new Map", "new Set", "Array(", "Object.assign(",
    "Buffer.", "make([]", "make(map", "make(chan", "append(", "copy(",
    "bytes.Buffer", ".push(", "Vec<", "Vec::new", "HashMap::new", "Box::new",
    ".append(", "list(", "dict(", "set(", "bytearray(", "collections.deque",
};

static const std::vector<std::string> PAT_FPU = {
    "sqrt(", "pow(", "sin(", "cos(", "tan(", "exp(", "log(", "log2(", "log10(",
    "fabs(", "ceil(", "floor(", "round(", "fmod(", "hypot(", "atan2(",
    "std::sqrt", "std::pow", "std::sin", "std::cos", "std::exp", "std::log",
    "Math.sqrt", "Math.pow", "Math.sin", "Math.cos", "Math.exp", "Math.log",
    "math.Sqrt", "math.Pow", "math.Sin", "math.Cos", "math.Exp", "math.Log",
    "math.sqrt", "math.pow", "math.sin", "math.cos", "math.exp", "math.log",
    "float ", "double ", "long double",
    "float32", "float64", "f32", "f64",
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
    "System.out", "System.err", "Scanner(", "BufferedReader", "FileReader",
    "FileInputStream", "FileOutputStream", "Files.", "InputStream",
    "OutputStream", "console.log(", "console.error(", "console.warn(",
    "fetch(", "XMLHttpRequest", "fs.", "process.stdout", "process.stderr",
    "fmt.Print", "fmt.Scan", "bufio.", "os.Open(", "os.Create(", "io.Copy(",
    "net.Dial(", "http.Get(", "http.Post(", "println(", "print(",
    "sys.stdout", "sys.stderr", "logging.", "echo ", "var_dump(", "file_get_contents(",
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
    "synchronized", "AtomicInteger", "AtomicLong", "ReentrantLock",
    "ConcurrentHashMap", "Promise.all", "Worker(", "SharedArrayBuffer",
    "Atomics.", "sync.Mutex", "sync.RWMutex", "sync.WaitGroup", "sync.Once",
    "std::sync", "Mutex<", "RwLock<", "Arc<",
    "threading.Lock", "threading.RLock", "asyncio.Lock", "DispatchQueue",
};

// 静态分析器，使用纯文本模式匹配
class StaticAnalyzer {
public:
    enum class SourceLanguage {
        C,
        Cpp,
        Java,
        JavaScript,
        TypeScript,
        Go,
        CSharp,
        Rust,
        Python,
        PHP,
        Kotlin,
        Swift,
        Unknown,
    };

    std::vector<FunctionProfile> analyze(const std::string& filepath) {
        auto profiles = analyze_file(filepath, filepath);
        build_call_graph(profiles);
        return profiles;
    }

    std::vector<FunctionProfile> analyze_path(const std::string& path) {
        namespace fs = std::filesystem;

        std::error_code ec;
        const fs::path input(path);
        if (fs::is_regular_file(input, ec)) {
            if (!is_supported_file(input))
                throw std::runtime_error("不支持的源文件类型: " + input.string());
            return analyze(input.string());
        }
        if (!fs::is_directory(input, ec))
            throw std::runtime_error("找不到源文件或目录: " + path);

        const auto files = collect_supported_files(input.string());
        if (files.empty())
            throw std::runtime_error("目录中没有可分析的源文件: " + input.string());

        std::vector<FunctionProfile> profiles;
        for (const auto& file : files) {
            const std::string display = relative_display_path(fs::path(file), input);
            auto file_profiles = analyze_file(file, display);
            profiles.insert(profiles.end(),
                            std::make_move_iterator(file_profiles.begin()),
                            std::make_move_iterator(file_profiles.end()));
        }
        build_call_graph(profiles);
        return profiles;
    }

    static SourceLanguage detect_language(const std::string& filepath) {
        std::string ext = extension_of(filepath);
        if (ext == ".c") return SourceLanguage::C;
        if (ext == ".cc" || ext == ".cpp" || ext == ".cxx" || ext == ".c++" ||
            ext == ".hpp" || ext == ".hh" || ext == ".hxx" || ext == ".h++" ||
            ext == ".ipp" || ext == ".tpp" || ext == ".mm")
            return SourceLanguage::Cpp;
        if (ext == ".h") return SourceLanguage::Cpp;
        if (ext == ".java") return SourceLanguage::Java;
        if (ext == ".js" || ext == ".mjs" || ext == ".cjs" || ext == ".jsx")
            return SourceLanguage::JavaScript;
        if (ext == ".ts" || ext == ".tsx") return SourceLanguage::TypeScript;
        if (ext == ".go") return SourceLanguage::Go;
        if (ext == ".cs") return SourceLanguage::CSharp;
        if (ext == ".rs") return SourceLanguage::Rust;
        if (ext == ".py") return SourceLanguage::Python;
        if (ext == ".php" || ext == ".phtml") return SourceLanguage::PHP;
        if (ext == ".kt" || ext == ".kts") return SourceLanguage::Kotlin;
        if (ext == ".swift") return SourceLanguage::Swift;
        return SourceLanguage::Unknown;
    }

    static std::string language_display_name(SourceLanguage language) {
        switch (language) {
            case SourceLanguage::C: return "C";
            case SourceLanguage::Cpp: return "C++";
            case SourceLanguage::Java: return "Java";
            case SourceLanguage::JavaScript: return "JavaScript";
            case SourceLanguage::TypeScript: return "TypeScript";
            case SourceLanguage::Go: return "Go";
            case SourceLanguage::CSharp: return "C#";
            case SourceLanguage::Rust: return "Rust";
            case SourceLanguage::Python: return "Python";
            case SourceLanguage::PHP: return "PHP";
            case SourceLanguage::Kotlin: return "Kotlin";
            case SourceLanguage::Swift: return "Swift";
            case SourceLanguage::Unknown: return "Unknown";
        }
        return "Unknown";
    }

    static bool is_supported_file(const std::filesystem::path& path) {
        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec))
            return false;
        return detect_language(path.string()) != SourceLanguage::Unknown;
    }

    static std::vector<std::string> collect_supported_files(const std::string& root) {
        namespace fs = std::filesystem;

        std::vector<std::string> files;
        std::error_code ec;
        fs::recursive_directory_iterator it(
            root,
            fs::directory_options::skip_permission_denied,
            ec);
        fs::recursive_directory_iterator end;

        for (; it != end; it.increment(ec)) {
            if (ec) {
                ec.clear();
                continue;
            }

            const fs::directory_entry& entry = *it;
            if (entry.is_directory(ec)) {
                if (should_skip_directory(entry.path()))
                    it.disable_recursion_pending();
                continue;
            }
            if (ec) {
                ec.clear();
                continue;
            }

            if (is_supported_file(entry.path()))
                files.push_back(entry.path().lexically_normal().string());
        }

        std::sort(files.begin(), files.end());
        return files;
    }

    static std::string summarize_languages(const std::vector<FunctionProfile>& profiles) {
        std::set<std::string> languages;
        for (const auto& fp : profiles) {
            if (!fp.language.empty() && fp.language != "Unknown")
                languages.insert(fp.language);
        }
        if (languages.empty())
            return "Unknown";
        if (languages.size() == 1)
            return *languages.begin();

        std::ostringstream out;
        out << "Mixed (";
        bool first = true;
        for (const auto& language : languages) {
            if (!first)
                out << ", ";
            out << language;
            first = false;
        }
        out << ")";
        return out.str();
    }

private:
    static std::vector<std::string> read_lines(const std::string& filepath) {
        std::ifstream f(filepath);
        if (!f.is_open())
            throw std::runtime_error("无法打开文件: " + filepath);

        std::vector<std::string> lines;
        std::string line;
        while (std::getline(f, line))
            lines.push_back(line);
        return lines;
    }

    static std::vector<FunctionProfile> analyze_file(const std::string& filepath,
                                                     const std::string& display_path) {
        const auto lines = read_lines(filepath);
        const SourceLanguage language = detect_language(filepath);
        return extract_functions(lines, filepath, display_path, language);
    }

    static bool should_skip_directory(const std::filesystem::path& path) {
        const std::string name = path.filename().string();
        static const std::set<std::string> skipped = {
            ".git", ".hg", ".svn", ".idea", ".vscode", ".cache",
            "build", "dist", "out", "target", "node_modules",
            "cmake-build-debug", "cmake-build-release"
        };
        if (skipped.count(name))
            return true;
        return !name.empty() && name[0] == '.';
    }

    static std::string relative_display_path(const std::filesystem::path& file,
                                             const std::filesystem::path& root) {
        std::error_code ec;
        std::filesystem::path relative = std::filesystem::relative(file, root, ec);
        if (ec || relative.empty())
            return file.lexically_normal().string();
        return relative.lexically_normal().string();
    }

    static std::string extension_of(const std::string& filepath) {
        size_t slash = filepath.find_last_of("/\\");
        size_t dot = filepath.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
            return "";
        std::string ext = filepath.substr(dot);
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char ch) {
            return (char)std::tolower(ch);
        });
        return ext;
    }

    // 去除行尾注释
    static std::string strip_line_comment(const std::string& s) {
        bool in_str = false;
        char quote = 0;
        for (size_t i = 0; i < s.size(); ++i) {
            if ((s[i] == '"' || s[i] == '\'' || s[i] == '`') && (i == 0 || s[i-1] != '\\')) {
                if (!in_str) {
                    in_str = true;
                    quote = s[i];
                } else if (quote == s[i]) {
                    in_str = false;
                    quote = 0;
                }
            }
            if (!in_str && i+1 < s.size() && s[i] == '/' && s[i+1] == '/')
                return s.substr(0, i);
            if (!in_str && s[i] == '#' &&
                (i == 0 || std::isspace((unsigned char)s[i - 1])))
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
        char quote = 0, prev = 0;
        for (char c : s) {
            if ((c == '"' || c == '\'' || c == '`') && prev != '\\') {
                if (!in_str) {
                    in_str = true;
                    quote = c;
                } else if (quote == c) {
                    in_str = false;
                    quote = 0;
                }
            }
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

    static bool is_identifier_char(char c) {
        return std::isalnum((unsigned char)c) || c == '_' || c == '$';
    }

    static bool is_valid_name(const std::string& name) {
        if (name.empty()) return false;
        if (!std::isalpha((unsigned char)name[0]) && name[0] != '_' &&
            name[0] != '~' && name[0] != '$')
            return false;
        static const std::set<std::string> kws = {
            "if","else","for","while","do","switch","try","catch","return",
            "case","default","operator","class","struct","namespace","enum",
            "union","function","func","fn","new","public","private","protected",
            "static","async","await","export","default","const","let","var",
            "get","set","synchronized"
        };
        return !kws.count(name);
    }

    static bool contains_word(const std::string& s, const std::string& word) {
        size_t pos = 0;
        while ((pos = s.find(word, pos)) != std::string::npos) {
            bool left_ok = pos == 0 || !is_identifier_char(s[pos - 1]);
            size_t right = pos + word.size();
            bool right_ok = right >= s.size() || !is_identifier_char(s[right]);
            if (left_ok && right_ok)
                return true;
            pos = right;
        }
        return false;
    }

    static bool has_semicolon_before_body(const std::string& s) {
        size_t semi = s.find(';');
        size_t body = s.find('{');
        return semi != std::string::npos && (body == std::string::npos || semi < body);
    }

    static bool uses_semicolon_signatures(SourceLanguage language) {
        return language == SourceLanguage::C ||
               language == SourceLanguage::Cpp ||
               language == SourceLanguage::Java ||
               language == SourceLanguage::CSharp ||
               language == SourceLanguage::Kotlin ||
               language == SourceLanguage::Swift ||
               language == SourceLanguage::PHP ||
               language == SourceLanguage::Unknown;
    }

    static int leading_indent_width(const std::string& line) {
        int width = 0;
        for (char c : line) {
            if (c == ' ')
                ++width;
            else if (c == '\t')
                width += 4;
            else
                break;
        }
        return width;
    }

    static bool is_indent_blank(const std::string& line) {
        return trim(strip_line_comment(line)).empty();
    }

    static std::string parse_python_name(const std::string& raw) {
        std::string s = trim(strip_line_comment(raw));
        if (starts_with(s, "async def "))
            s = trim(s.substr(6));
        if (!starts_with(s, "def "))
            return "";

        size_t pos = 4;
        while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
        size_t end = pos;
        while (end < s.size() && is_identifier_char(s[end])) ++end;
        std::string name = s.substr(pos, end - pos);
        return is_valid_name(name) ? name : "";
    }

    static std::string parse_name_before_paren(const std::string& head) {
        size_t paren = head.find('(');
        if (paren == std::string::npos)
            return "";
        std::string before = head.substr(0, paren);
        size_t ne = before.find_last_not_of(" \t");
        if (ne == std::string::npos) return "";
        size_t ns = before.find_last_of(" \t:>*&~.", ne);
        ns = (ns == std::string::npos) ? 0 : ns + 1;
        std::string name = before.substr(ns, ne - ns + 1);
        return is_valid_name(name) ? name : "";
    }

    static std::string parse_go_name(const std::string& head) {
        std::string s = trim(head);
        if (!starts_with(s, "func "))
            return "";

        s = trim(s.substr(5));
        if (!s.empty() && s[0] == '(') {
            int depth = 0;
            size_t i = 0;
            for (; i < s.size(); ++i) {
                if (s[i] == '(') ++depth;
                else if (s[i] == ')' && --depth == 0) {
                    ++i;
                    break;
                }
            }
            s = trim(s.substr(i));
        }

        size_t end = 0;
        while (end < s.size() && is_identifier_char(s[end])) ++end;
        std::string name = s.substr(0, end);
        return is_valid_name(name) ? name : "";
    }

    static std::string parse_rust_name(const std::string& head) {
        size_t fn = head.find("fn ");
        if (fn == std::string::npos)
            return "";
        fn += 3;
        while (fn < head.size() && std::isspace((unsigned char)head[fn])) ++fn;
        size_t end = fn;
        while (end < head.size() && is_identifier_char(head[end])) ++end;
        std::string name = head.substr(fn, end - fn);
        return is_valid_name(name) ? name : "";
    }

    static std::string parse_js_name(const std::string& head) {
        std::string s = trim(head);
        while (starts_with(s, "export "))
            s = trim(s.substr(7));
        while (starts_with(s, "default "))
            s = trim(s.substr(8));

        size_t function_pos = s.find("function ");
        if (function_pos != std::string::npos) {
            size_t pos = function_pos + 9;
            while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
            if (pos < s.size() && s[pos] == '*') ++pos;
            while (pos < s.size() && std::isspace((unsigned char)s[pos])) ++pos;
            size_t end = pos;
            while (end < s.size() && is_identifier_char(s[end])) ++end;
            std::string name = s.substr(pos, end - pos);
            return is_valid_name(name) ? name : "";
        }

        size_t arrow = s.find("=>");
        if (arrow != std::string::npos) {
            size_t eq = arrow == 0 ? std::string::npos : s.rfind('=', arrow - 1);
            if (eq != std::string::npos) {
                std::string left = trim(s.substr(0, eq));
                for (const char* kw : {"const ", "let ", "var ", "static "})
                    if (starts_with(left, kw)) left = trim(left.substr(std::string(kw).size()));
                size_t dot = left.find_last_of('.');
                if (dot != std::string::npos) left = left.substr(dot + 1);
                size_t colon = left.find(':');
                if (colon != std::string::npos) left = left.substr(0, colon);
                size_t space = left.find_last_of(" \t");
                if (space != std::string::npos) left = left.substr(space + 1);
                left = trim(left);
                return is_valid_name(left) ? left : "";
            }
        }

        std::string method = s;
        for (const char* kw : {"async ", "static ", "get ", "set ", "*"})
            if (starts_with(method, kw)) method = trim(method.substr(std::string(kw).size()));
        return parse_name_before_paren(method);
    }

    static std::string detect_func_name(const std::string& raw, SourceLanguage language) {
        std::string s = trim(raw);

        if (s.empty() || s[0] == '#') return "";
        for (const char* kw : {"class ", "struct ", "namespace ", "enum ", "union ",
                                "//", "/*", " * ", " */"})
            if (s.rfind(kw, 0) == 0) return "";

        size_t body = s.find('{');
        if (body == std::string::npos)
            return "";

        if (uses_semicolon_signatures(language) && has_semicolon_before_body(s))
            return "";

        std::string head = trim(s.substr(0, body));
        if (head.empty() || head.find('(') == std::string::npos)
            return "";

        if (language == SourceLanguage::Go)
            return parse_go_name(head);
        if (language == SourceLanguage::Rust)
            return parse_rust_name(head);
        if (language == SourceLanguage::JavaScript || language == SourceLanguage::TypeScript)
            return parse_js_name(head);

        if (head.find('=') != std::string::npos)
            return "";

        return parse_name_before_paren(head);
    }

    static bool detect_function_at(const std::vector<std::string>& lines,
                                   int start,
                                   SourceLanguage language,
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
                    starts_with(part, "union ") || starts_with(part, "interface ") ||
                    starts_with(part, "type ") || starts_with(part, "impl "))
                    return false;
            }

            if (!sig.empty())
                sig += ' ';
            sig += part;

            if (uses_semicolon_signatures(language) && has_semicolon_before_body(sig))
                return false;

            if (sig.find('{') != std::string::npos) {
                name = detect_func_name(sig, language);
                if (name.empty())
                    return false;
                body_start = j;
                return true;
            }
        }
        return false;
    }

    static void analyze_body_python(const std::vector<std::string>& lines,
                                    int start, int end, FunctionProfile& fp) {
        std::vector<int> loop_indents;
        int max_loop_depth = 0;
        int loop_count = 0;

        for (int i = start; i <= end && i < (int)lines.size(); ++i) {
            std::string raw = lines[i];
            std::string s = strip_line_comment(raw);
            std::string t = trim(s);
            if (t.empty())
                continue;

            int indent = leading_indent_width(raw);
            while (!loop_indents.empty() && indent <= loop_indents.back())
                loop_indents.pop_back();

            bool is_loop = contains_word(t, "for") || contains_word(t, "while");
            if (is_loop) {
                ++loop_count;
                loop_indents.push_back(indent);
                max_loop_depth = std::max(max_loop_depth, (int)loop_indents.size());
            }

            if (i > start && !fp.name.empty() &&
                s.find(fp.name + "(") != std::string::npos)
                fp.loops.has_recursion = true;

            fp.raw.memory += count_any(s, PAT_MEMORY);
            fp.raw.fpu    += count_any(s, PAT_FPU);
            fp.raw.io     += count_any(s, PAT_IO);
            fp.raw.simd   += count_any(s, PAT_SIMD);
            fp.raw.atomic += count_any(s, PAT_ATOMIC);

            if (contains_word(t, "if") ||
                contains_word(t, "elif") ||
                contains_word(t, "except") ||
                contains_word(t, "match") ||
                contains_word(t, "case") ||
                s.find(" if ") != std::string::npos)
                ++fp.raw.branch;

            for (char c : s)
                if (c=='+' || c=='-' || c=='*' || c=='/' || c=='%' ||
                    c=='&' || c=='|' || c=='^' || c=='~')
                    ++fp.raw.alu;
        }

        fp.loops.depth = max_loop_depth;
        fp.loops.count = loop_count;

        int iters = 1;
        for (int d = 0; d < max_loop_depth; ++d) iters *= 100;
        fp.loops.estimated_iters = iters;
    }

    // 分析函数体
    static void analyze_body(const std::vector<std::string>& lines,
                              int start, int end, FunctionProfile& fp,
                              SourceLanguage language) {
        if (language == SourceLanguage::Python) {
            analyze_body_python(lines, start, end, fp);
            return;
        }

        int loop_depth = 0, max_loop_depth = 0, loop_count = 0;

        for (int i = start; i <= end && i < (int)lines.size(); ++i) {
            std::string s = strip_line_comment(lines[i]);

            // 循环检测
            bool is_loop = (contains_word(s, "for") ||
                            contains_word(s, "while") ||
                            contains_word(s, "do") ||
                            contains_word(s, "foreach") ||
                            contains_word(s, "range"));
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
            if (contains_word(s, "if") ||
                contains_word(s, "switch") ||
                contains_word(s, "case") ||
                contains_word(s, "catch") ||
                contains_word(s, "match") ||
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

    static std::vector<FunctionProfile> extract_python_functions(
            const std::vector<std::string>& lines,
            const std::string& filepath,
            const std::string& display_path) {

        std::vector<FunctionProfile> profiles;
        int n = (int)lines.size();

        for (int i = 0; i < n; ++i) {
            const std::string current = trim(strip_line_comment(lines[i]));
            if (current.empty() || current[0] == '@')
                continue;

            std::string name = parse_python_name(lines[i]);
            if (name.empty())
                continue;

            int sig_indent = leading_indent_width(lines[i]);
            bool inline_suite = false;
            size_t colon = current.find(':');
            if (colon != std::string::npos &&
                trim(current.substr(colon + 1)).size() > 0)
                inline_suite = true;

            int body_start = i;
            int j = i + 1;
            if (!inline_suite) {
                body_start = i + 1;
                while (body_start < n && is_indent_blank(lines[body_start]))
                    ++body_start;
                if (body_start >= n || leading_indent_width(lines[body_start]) <= sig_indent) {
                    body_start = i;
                    j = i + 1;
                } else {
                    j = body_start;
                    while (j < n) {
                        if (is_indent_blank(lines[j])) {
                            ++j;
                            continue;
                        }
                        if (leading_indent_width(lines[j]) <= sig_indent)
                            break;
                        ++j;
                    }
                }
            }

            int line_start = i + 1;
            int decorator = i - 1;
            while (decorator >= 0) {
                std::string deco = trim(strip_line_comment(lines[decorator]));
                if (deco.empty()) {
                    --decorator;
                    continue;
                }
                if (deco[0] != '@' || leading_indent_width(lines[decorator]) != sig_indent)
                    break;
                line_start = decorator + 1;
                --decorator;
            }

            FunctionProfile fp;
            fp.name        = name;
            fp.file        = display_path;
            fp.source_path = filepath;
            fp.language    = language_display_name(SourceLanguage::Python);
            fp.line_start  = line_start;
            fp.line_end    = j;

            analyze_body(lines, body_start, std::max(body_start, j - 1), fp,
                         SourceLanguage::Python);
            profiles.push_back(fp);
        }

        return profiles;
    }

    // 提取所有函数
    static std::vector<FunctionProfile> extract_functions(
            const std::vector<std::string>& lines,
            const std::string& filepath,
            const std::string& display_path,
            SourceLanguage language) {

        if (language == SourceLanguage::Python)
            return extract_python_functions(lines, filepath, display_path);

        std::vector<FunctionProfile> profiles;
        int n = (int)lines.size();
        int i = 0;

        while (i < n) {
            if (language == SourceLanguage::PHP) {
                std::string part = trim(strip_line_comment(lines[i]));
                if (part == "<?php" || part == "?>") {
                    ++i;
                    continue;
                }
            }

            std::string name;
            int body_start = i;
            detect_function_at(lines, i, language, name, body_start);
            if (name.empty()) { ++i; continue; }

            FunctionProfile fp;
            fp.name        = name;
            fp.file        = display_path;
            fp.source_path = filepath;
            fp.language    = language_display_name(language);
            fp.line_start  = i + 1;

            int depth = brace_delta(lines[body_start]);
            int j = body_start + 1;
            while (j < n && depth > 0) {
                depth += brace_delta(lines[j]);
                ++j;
            }
            fp.line_end = j;

            analyze_body(lines, body_start, j - 1, fp, language);
            profiles.push_back(fp);
            i = j;
        }
        return profiles;
    }

    // 构建调用图
    static void build_call_graph(std::vector<FunctionProfile>& profiles) {
        std::vector<std::string> names;
        for (const auto& fp : profiles)
            names.push_back(fp.name);

        std::map<std::string, std::vector<std::string>> file_cache;

        for (auto& fp : profiles) {
            fp.callees.clear();

            auto cache_it = file_cache.find(fp.source_path);
            if (cache_it == file_cache.end())
                cache_it = file_cache.emplace(fp.source_path, read_lines(fp.source_path)).first;

            const auto& lines = cache_it->second;
            for (int i = fp.line_start - 1; i < fp.line_end && i < (int)lines.size(); ++i) {
                std::string s = strip_line_comment(lines[(size_t)i]);
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
