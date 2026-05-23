function normalize(values: number[]): number[] {
    let total = 0;
    for (const value of values) {
        total += value;
    }
    return values.map((value) => value / total);
}

const toMagnitudes = (values: number[]): number[] => {
    return values.map((value) => Math.sqrt(value * value));
};
