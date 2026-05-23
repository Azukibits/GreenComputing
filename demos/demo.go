package demo

func Multiply(values []int, factor int) []int {
    out := make([]int, len(values))
    for i, value := range values {
        out[i] = value * factor
    }
    return out
}

func Sum(values []int) int {
    total := 0
    for _, value := range values {
        total += value
    }
    return total
}
