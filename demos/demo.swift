func scaledEnergy(_ values: [Double], factor: Double) -> Double {
    var total = 0.0
    for value in values {
        total += value * factor
    }
    return total
}

func saveMessage(_ message: String) {
    print(message)
}
