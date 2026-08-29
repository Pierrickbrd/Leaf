//! Page order comes from entry names, never from zip order — the ZIP format guarantees
//! nothing there. And plain alphabetical order would put 10.jpg before 2.jpg, so numbers
//! have to be compared as numbers.

use std::cmp::Ordering;

pub fn compare(a: &str, b: &str) -> Ordering {
    let (a, b): (Vec<char>, Vec<char>) = (a.chars().collect(), b.chars().collect());
    let (mut i, mut j) = (0usize, 0usize);

    while i < a.len() && j < b.len() {
        let step = if a[i].is_ascii_digit() && b[j].is_ascii_digit() {
            let (end_a, end_b) = (digits_end(&a, i), digits_end(&b, j));
            let step = numbers(&a[i..end_a], &b[j..end_b]);
            (i, j) = (end_a, end_b);
            step
        } else {
            let step = a[i].to_lowercase().cmp(b[j].to_lowercase());
            (i, j) = (i + 1, j + 1);
            step
        };
        if step != Ordering::Equal {
            return step;
        }
    }
    (a.len() - i).cmp(&(b.len() - j))
}

/// Compare the value, not the spelling: 007 and 7 are the same number. Compared as text
/// once the leading zeroes are gone, so that a number longer than an i64 still sorts.
fn numbers(a: &[char], b: &[char]) -> Ordering {
    let (a, b) = (trimmed(a), trimmed(b));
    a.len().cmp(&b.len()).then_with(|| a.cmp(&b))
}

/// The end of the run of digits starting at `from`.
fn digits_end(chars: &[char], from: usize) -> usize {
    from + chars[from..]
        .iter()
        .take_while(|c| c.is_ascii_digit())
        .count()
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
