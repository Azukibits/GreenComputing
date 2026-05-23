fun weightedSum(values: List<Double>): Double {
    var total = 0.0
    for (value in values) {
        total += value * value
    }
    return total
}

fun greet(name: String): String {
    return "Hello, $name"
}
