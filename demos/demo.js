function sumItems(values) {
    let total = 0;
    for (const value of values) {
        total += value;
    }
    return total;
}

const squareItems = (values) => {
    return values.map((value) => value * value);
};
