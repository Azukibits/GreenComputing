import java.util.List;

class DemoJava {
    static double accumulateSquares(List<Double> values) {
        double total = 0.0;
        for (double value : values) {
            total += value * value;
        }
        return total;
    }

    static void logCount(int count) {
        System.out.println(count);
    }
}
