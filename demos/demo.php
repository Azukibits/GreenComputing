<?php

function sum_values(array $values): int {
    $total = 0;
    foreach ($values as $value) {
        $total += $value;
    }
    return $total;
}

function write_snapshot(array $values): void {
    $content = implode(",", $values);
    file_put_contents("snapshot.txt", $content);
}
