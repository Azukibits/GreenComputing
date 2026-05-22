// Demo source used by the analyzer.

#include <vector>
#include <cmath>
#include <string>
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <mutex>
#include <atomic>

// Triple nested loop.
std::vector<double> matrix_multiply_naive(int n) {
    std::vector<double> A(n * n, 1.0);
    std::vector<double> B(n * n, 2.0);
    std::vector<double> C(n * n, 0.0);

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            for (int k = 0; k < n; ++k) {
                C[i * n + j] += A[i * n + k] * B[k * n + j];
            }
        }
    }
    return C;
}

// Allocation and file output in a loop.
void log_each_result(int n) {
    std::ofstream log("results.log");
    for (int i = 0; i < n; ++i) {
        std::vector<double> tmp(i + 1, sqrt((double)i));
        double sum = 0.0;
        for (int j = 0; j < (int)tmp.size(); ++j)
            sum += tmp[j];
        log << "step " << i << " sum=" << sum << "\n";
    }
}

// Floating point heavy workload.
double compute_physics(const std::vector<double>& positions, double dt) {
    double energy = 0.0;
    for (int i = 0; i < (int)positions.size(); ++i) {
        double x = positions[i];
        double v = sin(x) * cos(x * dt) + exp(-x * x) * log(1.0 + x * x);
        double ke = 0.5 * v * v;
        double pe = pow(x, 2.0) - sqrt(fabs(x));
        energy += ke + pe;
    }
    return energy;
}

// Shared counter updated under a lock.
std::mutex g_mutex;
std::atomic<long long> g_counter{0};

void increment_shared(int iterations) {
    for (int i = 0; i < iterations; ++i) {
        std::lock_guard<std::mutex> lock(g_mutex);
        g_counter++;
    }
}

// Recursive Fibonacci.
long long fib_recursive(int n) {
    if (n <= 1) return n;
    return fib_recursive(n - 1) + fib_recursive(n - 2);
}

// Single pass dot product.
double dot_product(const std::vector<double>& a, const std::vector<double>& b) {
    double result = 0.0;
    for (size_t i = 0; i < a.size(); ++i)
        result += a[i] * b[i];
    return result;
}

// Constant time conversion.
inline double celsius_to_joules_per_mol(double temp_c) {
    constexpr double R = 8.314;   // J/(mol·K)
    return R * (temp_c + 273.15);
}

// Iterative Fibonacci.
long long fib_iterative(int n) {
    if (n <= 1) return n;
    long long a = 0, b = 1;
    for (int i = 2; i <= n; ++i) {
        long long c = a + b;
        a = b;
        b = c;
    }
    return b;
}

int main() {
    auto v = matrix_multiply_naive(64);
    log_each_result(100);

    std::vector<double> pos(1000, 1.0);
    double e = compute_physics(pos, 0.01);

    increment_shared(10000);

    long long f = fib_recursive(30);
    long long g = fib_iterative(30);

    std::vector<double> x(512, 1.0), y(512, 2.0);
    double d = dot_product(x, y);

    double j = celsius_to_joules_per_mol(25.0);

    std::cout << e << f << g << d << j << v[0] << "\n";
    return 0;
}
