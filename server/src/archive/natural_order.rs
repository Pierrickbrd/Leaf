//! Page order comes from entry names, never from zip order — the ZIP format guarantees
//! nothing there. And plain alphabetical order would put 10.jpg before 2.jpg, so numbers
//! have to be compared as numbers.

use std::cmp::Ordering;

pub fn compare(a: &str, b: &str) -> Ordering {
    let (a, b): (Vec<char>, Vec<char>) = (a.chars().collect(), b.chars().collect());
    let (mut i, mut j) = (0usize, 0usize);

    while i < a.len() && j < b.len() {
        if a[i].is_ascii_digit() && b[j].is_ascii_digit() {
            let (mut end_a, mut end_b) = (i, j);
            while end_a < a.len() && a[end_a].is_ascii_digit() {
                end_a += 1;
            }
            while end_b < b.len() && b[end_b].is_ascii_digit() {
                end_b += 1;
            }
            // Compare the value, not the spelling: 007 and 7 are the same number.
            let num_a = trimmed(&a[i..end_a]);
            let num_b = trimmed(&b[j..end_b]);
            match num_a
                .len()
                .cmp(&num_b.len())
                .then_with(|| num_a.cmp(&num_b))
            {
                Ordering::Equal => {}
                other => return other,
            }
            i = end_a;
            j = end_b;
        } else {
            match a[i].to_lowercase().cmp(b[j].to_lowercase()) {
                Ordering::Equal => {}
                other => return other,
            }
            i += 1;
            j += 1;
        }
    }
    (a.len() - i).cmp(&(b.len() - j))
}

fn trimmed(digits: &[char]) -> String {
    let text: String = digits.iter().collect();
    let trimmed = text.trim_start_matches('0');
    if trimmed.is_empty() {
        "0".to_string()
    } else {
        trimmed.to_string()
    }
}
