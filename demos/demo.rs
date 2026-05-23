fn energy_score(values: &[f64]) -> f64 {
    let mut total = 0.0;
    for value in values {
        total += value * value;
    }
    total
}

fn write_total(total: f64) {
    println!("{}", total);
}
