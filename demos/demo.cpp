#include <cstddef>
#include <fstream>
#include <vector>

double weighted_sum(const std::vector<double>& values) {
    double total = 0.0;
    for (std::size_t i = 0; i < values.size(); ++i) {
        total += values[i] * static_cast<double>(i + 1);
    }
    return total;
}

void persist_total(double total) {
    std::ofstream out("cpp-demo.txt");
    out << total << "\n";
}
