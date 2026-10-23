//! `/etc/leaf/server.env`, read back the way systemd reads it for the unit.
//!
//! The `device` commands run under `sudo`, outside the unit, so they do not inherit its
//! `EnvironmentFile`: without reading it themselves they would open `data/leaf.sqlite` in
//! whatever folder sudo was typed in, and link a device to an index the service never reads.

use std::collections::BTreeMap;

/// `NAME=value` lines; blank lines and `#` comments skipped.
///
/// A value in double quotes is unquoted with `\"` and `\\` decoded — the exact form
/// `quote_environment_value` in install.sh writes, so that a library path with a quote or a
/// backslash in it comes back as the installer was given it. Anything else is taken as it
/// stands.
pub fn read_env_file(text: &str) -> BTreeMap<String, String> {
    text.lines()
        .map(str::trim)
        .filter(|line| !line.is_empty() && !line.starts_with('#'))
        .filter_map(|line| line.split_once('='))
        .map(|(name, value)| (name.trim().to_string(), unquoted(value.trim())))
        .collect()
}

fn unquoted(value: &str) -> String {
    let Some(inner) = value
        .strip_prefix('"')
        .and_then(|rest| rest.strip_suffix('"'))
    else {
        return value.to_string();
    };
    let mut out = String::with_capacity(inner.len());
    let mut chars = inner.chars();
    while let Some(c) = chars.next() {
        match (c, chars.clone().next()) {
            ('\\', Some(escaped @ ('"' | '\\'))) => {
                out.push(escaped);
                chars.next();
            }
            _ => out.push(c),
        }
    }
    out
}
