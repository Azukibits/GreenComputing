import math


def accumulate_roots(values):
    total = 0.0
    for value in values:
        total += math.sqrt(value)
    return total


def rolling_pairs(values):
    pairs = []
    for index in range(len(values) - 1):
        pairs.append(values[index] + values[index + 1])
    return pairs
