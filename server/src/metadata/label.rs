//! Turning what a file says into what a reader sees.

/// What a raw chapter label was found to mean.
#[derive(Debug, Clone, PartialEq)]
pub struct ParsedLabel {
    pub raw: String,
    pub label: String,
    pub number: Option<f64>,
    pub title: Option<String>,
    /// CHAPTER when a number was found, BONUS otherwise.
    pub kind: &'static str,
}

/// Splits "Chap.099 : Coup de sifflet" into its parts.
///
/// The number is looked for in the label only. Otherwise "Bonus: Chapter 0" would pass
/// itself off as chapter 0.
pub fn parse(raw: &str) -> ParsedLabel {
    let text = raw.trim();
    let cut = separator(text);

    let (label, title) = match cut {
        Some((start, end)) => (
            text[..start].trim().to_string(),
            Some(text[end..].trim().to_string()).filter(|t| !t.is_empty()),
        ),
        None => (text.to_string(), None),
    };

    let number = last_number(&label);
    ParsedLabel {
        raw: text.to_string(),
        label,
        kind: if number.is_none() { "BONUS" } else { "CHAPTER" },
        number,
        title,
    }
}

/// Where the label stops and the title starts: whitespace around a dash or a colon, or a
/// colon followed by whitespace. Byte offsets of the separator itself.
fn separator(text: &str) -> Option<(usize, usize)> {
    let chars: Vec<char> = text.chars().collect();
    let mut offset = 0usize;
    for (i, ch) in chars.iter().enumerate() {
        let width = ch.len_utf8();
        let before_is_space = i > 0 && chars[i - 1].is_whitespace();
        let after_is_space = text[offset + width..].starts_with(char::is_whitespace);
        // " - ", " : ", "– " … and ": " on its own, which is what a colon usually gets.
        if matches!(ch, ':' | '–' | '—' | '-') && after_is_space && (before_is_space || *ch == ':')
        {
            let start = if before_is_space {
                text[..offset].trim_end().len()
            } else {
                offset
            };
            return Some((start, past_whitespace(text, offset + width)));
        }
        offset += width;
    }
    None
}

/// The first byte after the run of whitespace starting at `from`.
fn past_whitespace(text: &str, from: usize) -> usize {
    let rest = &text[from..];
    from + rest.len() - rest.trim_start().len()
}

fn last_number(label: &str) -> Option<f64> {
    let chars: Vec<char> = label.chars().collect();
    let mut found: Option<f64> = None;
    let mut i = 0;
    while i < chars.len() {
        let Some(end) = number_end(&chars, i) else {
            i += 1;
            continue;
        };
        let text: String = chars[i..end].iter().collect::<String>().replace(',', ".");
        if let Ok(value) = text.parse::<f64>() {
            found = Some(value);
        }
        i = end;
    }
    found
}

/// Where the number starting at `from` ends, or nothing when none starts there. A leading
/// minus counts as part of it, and so does one decimal separator — a point or a comma —
/// with a digit behind it.
fn number_end(chars: &[char], from: usize) -> Option<usize> {
    let mut i = from + usize::from(chars[from] == '-');
    if !chars.get(i).is_some_and(char::is_ascii_digit) {
        return None;
    }
    i = digits_end(chars, i);
    if matches!(chars.get(i), Some('.' | ',')) && chars.get(i + 1).is_some_and(char::is_ascii_digit)
    {
        i = digits_end(chars, i + 1);
    }
    Some(i)
}

/// The end of the run of digits starting at `from`.
fn digits_end(chars: &[char], from: usize) -> usize {
    from + chars[from..]
        .iter()
        .take_while(|c| c.is_ascii_digit())
        .count()
}

/// Composes a chapter label from the edition's pattern.
///
/// "Chap.{n:000}" with 99 gives "Chap.099", "Level.{n}" with 56.5 gives "Level.56.5".
/// Padding applies to the integer part only, and a negative number keeps its sign in front
/// of the padding.
pub fn compose(pattern: Option<&str>, number: Option<f64>) -> Option<String> {
    let pattern = pattern?.trim();
    let number = number?;
    if pattern.is_empty() {
        return None;
    }

    let plain = if number.fract() == 0.0 {
        format!("{}", number as i64)
    } else {
        format!("{number}")
    };

    let mut out = String::with_capacity(pattern.len() + plain.len());
    let mut rest = pattern;
    while let Some(start) = rest.find("{n") {
        out.push_str(&rest[..start]);
        let after = &rest[start..];
        let Some(close) = after.find('}') else {
            break;
        };
        let inside = &after[2..close]; // "" or ":000"
        let padding = inside.strip_prefix(':').map(str::len).unwrap_or(0);
        let valid = inside.is_empty()
            || inside
                .strip_prefix(':')
                .is_some_and(|z| z.chars().all(|c| c == '0'));
        if !valid {
            out.push_str(&after[..=close]);
        } else {
            out.push_str(&pad(&plain, padding));
        }
        rest = &after[close + 1..];
    }
    out.push_str(rest);
    Some(out)
}

fn pad(plain: &str, padding: usize) -> String {
    if padding == 0 {
        return plain.to_string();
    }
    let (sign, body) = match plain.strip_prefix('-') {
        Some(body) => ("-", body),
        None => ("", plain),
    };
    let (whole, fraction) = match body.split_once('.') {
        Some((w, f)) => (w, Some(f)),
        None => (body, None),
    };
    let padded = format!("{whole:0>padding$}");
    match fraction {
        Some(f) => format!("{sign}{padded}.{f}"),
        None => format!("{sign}{padded}"),
    }
}
