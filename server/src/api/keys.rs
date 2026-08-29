//! Authentication in the simplest form that holds: a secret string each device knows,
//! compared against the ones in the configuration. No accounts, no passwords, no sign-in
//! screen.
//!
//! The key is not compiled into the applications — it is pasted once, into settings.
//! Otherwise anyone unpacking the APK could read it, and changing it would mean rebuilding
//! everything.
//!
//! And keys do not carry the same rights: the desktop imports, the phone only reads. Lose
//! the phone and whoever finds it gets to look at comics, not to write to the disk.
//!
//! ```text
//! LEAF_KEYS="desktop:8f3a92c1…:read,import  phone:2b71ef04…:read"
//! ```

use std::collections::BTreeSet;

use anyhow::{bail, Result};
use sha2::{Digest, Sha256};

pub const HEADER: &str = "X-Leaf-Key";

/// Sixteen characters of random is already far past guessing; below that is a word.
pub const MINIMUM_SECRET: usize = 16;

#[derive(Debug, Clone, Copy, PartialEq, Eq, PartialOrd, Ord)]
pub enum Permission {
    Read,
    Import,
}

impl Permission {
    pub fn name(self) -> &'static str {
        match self {
            Permission::Read => "read",
            Permission::Import => "import",
        }
    }
}

#[derive(Debug, Clone)]
pub struct Key {
    pub name: String,
    digest: [u8; 32],
    pub permissions: BTreeSet<Permission>,
}

#[derive(Debug, Clone, Default)]
pub struct Keys {
    keys: Vec<Key>,
}

impl Keys {
    /// True when nothing is configured, and the server therefore accepts everyone.
    pub fn open(&self) -> bool {
        self.keys.is_empty()
    }

    /// Reads the whole configuration, one key per line. Refuses only when it holds nothing
    /// usable — see `one_key` for what a single line is allowed to get away with.
    pub fn parse(configuration: Option<&str>) -> Result<Self> {
        let mut keys = Vec::new();
        let mut malformed = 0usize;
        for line in configuration
            .unwrap_or_default()
            .split([' ', '\t', '\n', '\r'])
            .map(str::trim)
            .filter(|l| !l.is_empty())
        {
            match one_key(line)? {
                Some(key) => keys.push(key),
                None => malformed += 1,
            }
        }

        // Configured and yet empty: every line was malformed. Refused rather than
        // warned about, because the two ways to have no keys are not the same thing —
        // "I did not set any" is a choice, "I set some and they were all typos" is a
        // server that silently accepts everyone when it was told not to.
        if malformed > 0 && keys.is_empty() {
            bail!(
                "LEAF_KEYS holds {malformed} entr{} and not one usable key. \
                 The form is name:secret:rights, separated by spaces.",
                if malformed == 1 { "y" } else { "ies" }
            );
        }

        let keys = Keys { keys };
        keys.announce();
        Ok(keys)
    }

    /// Says at startup what the server will accept, which is the one moment where saying so
    /// still helps.
    fn announce(&self) {
        if self.open() {
            tracing::warn!("No key configured: the server accepts everyone. Do not expose it.");
            return;
        }
        for key in &self.keys {
            let rights: Vec<&str> = key.permissions.iter().map(|p| p.name()).collect();
            tracing::info!(key = %key.name, rights = %rights.join(", "), "Key");
        }
    }

    /// The key behind a secret, or nothing.
    ///
    /// Compares digests rather than strings: fixed length, so neither the time taken nor an
    /// early exit says anything about the secret — not even how long it is.
    pub fn recognise(&self, secret: Option<&str>) -> Option<&Key> {
        let secret = secret?;
        if secret.trim().is_empty() {
            return None;
        }
        let offered = digest(secret);
        self.keys
            .iter()
            .find(|k| careful_equals(&k.digest, &offered))
    }
}

fn digest(value: &str) -> [u8; 32] {
    let mut hasher = Sha256::new();
    hasher.update(value.as_bytes());
    hasher.finalize().into()
}

/// Constant time over the whole thing: no early exit to time.
fn careful_equals(a: &[u8; 32], b: &[u8; 32]) -> bool {
    let mut difference = 0u8;
    for i in 0..32 {
        difference |= a[i] ^ b[i];
    }
    difference == 0
}

/// One `name:secret:rights` line. `None` when it is not one — which is warned about and
/// counted, not refused: a single typo in a configuration file should not stop a server
/// that has other, usable keys.
///
/// A short secret is refused outright, because it is not a key: it is a password someone
/// will guess. The throttle slows an attacker down; it does not make `phone:a:read` safe.
fn one_key(line: &str) -> Result<Option<Key>> {
    let parts: Vec<&str> = line.split(':').collect();
    if parts.len() < 2 {
        // By characters, not bytes: `&line[..12]` lands mid-character on anything
        // accented and takes the server down at startup with a slice panic, which
        // is a poor way to report a typo in a configuration file.
        let shown: String = line.chars().take(12).collect();
        tracing::warn!(key = %shown, "malformed key, ignored");
        return Ok(None);
    }
    let (name, secret) = (parts[0], parts[1]);
    if secret.chars().count() < MINIMUM_SECRET {
        bail!(
            "the key \"{name}\" has a secret of {} characters, {MINIMUM_SECRET} are needed",
            secret.chars().count()
        );
    }

    let mut permissions: BTreeSet<Permission> = parts
        .get(2)
        .unwrap_or(&"")
        .split(',')
        .filter_map(|p| match p.trim().to_ascii_lowercase().as_str() {
            "read" => Some(Permission::Read),
            "import" => Some(Permission::Import),
            _ => None,
        })
        .collect();
    if permissions.is_empty() {
        permissions.insert(Permission::Read);
    }

    Ok(Some(Key {
        name: name.to_string(),
        digest: digest(secret),
        permissions,
    }))
}
