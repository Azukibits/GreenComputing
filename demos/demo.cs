using System;
using System.Collections.Generic;

static class DemoCs {
    static int SumValues(List<int> values) {
        int total = 0;
        foreach (var value in values) {
            total += value;
        }
        return total;
    }

    static void PrintTotal(int total) {
        Console.WriteLine(total);
    }
}
