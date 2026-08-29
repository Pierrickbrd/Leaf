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
    let bytes: Vec<char> = text.chars().collect();
    let mut offset = 0usize;
    for (i, ch) in bytes.iter().enumerate() {
        let width = ch.len_utf8();
        if matches!(ch, ':' | '–' | '—' | '-') {
            let before_is_space = i > 0 && bytes[i - 1].is_whitespace();
            let after = text[offset + width..].chars().next();
            let after_is_space = after.is_some_and(char::is_whitespace);
            // " - ", " : ", "– " … and ": " on its own, which is what a colon usually gets.
            let matched = after_is_space && (before_is_space || *ch == ':');
            if matched {
                let start = if before_is_space {
                    text[..offset].trim_end().len()
                } else {
                    offset
                };
                let mut end = offset + width;
                while text[end..].starts_with(char::is_whitespace) {
                    end += text[end..].chars().next().map(char::len_utf8).unwrap_or(0);
                }
                return Some((start, end));
            }
        }
        offset += width;
    }
    None
}

fn last_number(label: &str) -> Option<f64> {
    let mut found: Option<f64> = None;
    let chars: Vec<char> = label.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        if chars[i].is_ascii_digit()
            || (chars[i] == '-' && chars.get(i + 1).is_some_and(char::is_ascii_digit))
        {
            let start = i;
            if chars[i] == '-' {
                i += 1;
            }
            while i < chars.len() && chars[i].is_ascii_digit() {
                i += 1;
            }
            if i + 1 < chars.len() && matches!(chars[i], '.' | ',') && chars[i + 1].is_ascii_digit()
            {
                i += 1;
                while i < chars.len() && chars[i].is_ascii_digit() {
                    i += 1;
                }
            }
            let text: String = chars[start..i].iter().collect::<String>().replace(',', ".");
            if let Ok(value) = text.parse::<f64>() {
                found = Some(value);
            }
        } else {
            i += 1;
        }
    }
    found
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
