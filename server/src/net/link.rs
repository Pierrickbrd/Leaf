//! The `leaf://` link a new device is handed, and the addresses it can name.
//!
//! ```text
//! leaf://HÔTE[:PORT]/lier/CODE[?empreinte=SHA256]
//! ```
//!
//! `leaf://` rather than `https://` so that a click opens Leaf and not a browser, which would
//! stop at a certificate it does not know. It means https everywhere but the loopback, where
//! it means http — the one place plain http is allowed, an SSH tunnel to the server.

use std::fmt;
use std::net::{IpAddr, Ipv4Addr, Ipv6Addr};

use anyhow::{bail, Result};

use crate::boot::is_loopback;

/// A link to hand a device.
#[derive(Clone, PartialEq, Eq)]
pub struct Link {
    /// Whether the address speaks https; only then is 443 left out of the link.
    pub https: bool,
    /// A name or an address, IPv6 without its brackets.
    pub host: String,
    pub port: u16,
    /// As issued, `ABCD-EFGH`; written in the link without its dash.
    pub code: String,
    /// The certificate to pin, as 64 lowercase hex digits; `None` for a certificate a public
    /// authority signed, and for plain http.
    pub fingerprint: Option<String>,
}

/// Written by hand, like `Issued`'s: the code is a secret for the ten minutes it lives, and a
/// derived `Debug` would put it in any log line that printed a link.
impl fmt::Debug for Link {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.debug_struct("Link")
            .field("https", &self.https)
            .field("host", &self.host)
            .field("port", &self.port)
            .field("code", &"…")
            .field("fingerprint", &self.fingerprint)
            .finish()
    }
}

impl fmt::Display for Link {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "leaf://")?;
        if self.host.contains(':') {
            write!(f, "[{}]", self.host)?;
        } else {
            write!(f, "{}", self.host)?;
        }
        if !(self.https && self.port == 443) {
            write!(f, ":{}", self.port)?;
        }
        // Without the dash: a dash is how a person reads a code, and the client reads one
        // with or without it, but a link is copied whole and the shorter the better in a QR.
        write!(f, "/lier/{}", self.code.replace('-', ""))?;
        if let Some(fingerprint) = &self.fingerprint {
            write!(f, "?empreinte={fingerprint}")?;
        }
        Ok(())
    }
}

/// Where a link points, before it has a code: `LEAF_PUBLIC_URL`, `--address`, or one of the
/// addresses the server listens on.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Address {
    pub https: bool,
    pub host: String,
    pub port: u16,
}

impl Address {
    /// Reads an address the way a person types one: `https://name[:port][/…]`, or a
    /// `leaf://` one already, or `http://` on the loopback.
    ///
    /// Plain http anywhere else is refused here rather than turned into a link: the server
    /// does not speak it past the loopback, and a client would send its key in clear to
    /// whatever did.
    pub fn parse(typed: &str) -> Result<Address> {
        let typed = typed.trim();
        let Some((scheme, rest)) = typed.split_once("://") else {
            bail!("« {typed} » n'est pas une adresse : elle commence par https://.");
        };
        let scheme = scheme.to_ascii_lowercase();
        if !matches!(scheme.as_str(), "https" | "http" | "leaf") {
            bail!("« {typed} » n'est pas une adresse https.");
        }
        let authority = rest.split(['/', '?', '#']).next().unwrap_or_default();
        let (host, port) = if let Some(bracketed) = authority.strip_prefix('[') {
            let Some((host, after)) = bracketed.split_once(']') else {
                bail!("« {typed} » : un crochet n'est pas refermé.");
            };
            (host, after.strip_prefix(':'))
        } else if authority.matches(':').count() > 1 {
            // Split at its last colon, `fd00::2` became host `fd00:` and port 2 — a link to
            // nowhere, printed as though it were fine.
            bail!(
                "« {typed} » : une adresse IPv6 s'écrit entre crochets, comme [{authority}], \
                 suivie au besoin de :port."
            );
        } else {
            match authority.rsplit_once(':') {
                Some((host, port)) => (host, Some(port)),
                None => (authority, None),
            }
        };
        if host.is_empty() {
            bail!("« {typed} » ne nomme aucune machine.");
        }
        let loopback = is_loopback(host);
        if scheme == "http" && !loopback {
            bail!(
                "« {typed} » : Leaf ne parle qu'en https hors de cette machine ; \
                 écrivez https://."
            );
        }
        let port = match port {
            Some(port) => port
                .parse::<u16>()
                .map_err(|_| anyhow::anyhow!("« {typed} » : « {port} » n'est pas un port."))?,
            None if scheme == "http" => 80,
            None => 443,
        };
        Ok(Address {
            https: !loopback,
            host: host.to_string(),
            port,
        })
    }
}

/// The addresses a server bound to `host` can be reached on, each with a label to tell them
/// apart in front of a person.
///
/// The host itself when it is one address. For `0.0.0.0` and `::`, which no client can dial,
/// the machine's own addresses past the loopback — IPv4 only for `0.0.0.0`, both for `::`,
/// which Linux binds dual-stack. Link-local addresses are left out: one is only reachable
/// with an interface named after it, which a link cannot carry.
pub fn listening_addresses(host: &str) -> Vec<(String, IpAddr)> {
    let interfaces = if_addrs::get_if_addrs().unwrap_or_default();
    let name_of = |address: IpAddr| {
        interfaces
            .iter()
            .find(|i| i.ip() == address)
            .map(|i| i.name.as_str())
    };
    let host = host.trim_start_matches('[').trim_end_matches(']');
    let Ok(bound) = host.parse::<IpAddr>() else {
        return Vec::new();
    };
    if !bound.is_unspecified() {
        let label = label(bound, name_of(bound)).unwrap_or_else(|| host.to_string());
        return vec![(label, bound)];
    }
    // Each address under what it is, or under its interface when it says nothing.
    let reachable: Vec<(&str, IpAddr, String)> = interfaces
        .iter()
        .filter(|i| !i.is_loopback() && !i.is_link_local())
        .filter(|i| bound.is_ipv6() || i.ip().is_ipv4())
        .map(|i| {
            let what = label(i.ip(), Some(&i.name)).unwrap_or_else(|| i.name.clone());
            (i.name.as_str(), i.ip(), what)
        })
        .collect();
    let mut found: Vec<(String, IpAddr)> = reachable
        .iter()
        .map(|(interface, address, what)| {
            let mut label = what.clone();
            // Two private networks — the home one and a virtual machines' bridge, say — are
            // both « réseau local », and `--qr` has to be able to name either.
            let shared = reachable
                .iter()
                .any(|(other, _, same)| same == what && other != interface);
            if shared && what != interface {
                label = format!("{label}, {interface}");
            }
            if bound.is_ipv6() && address.is_ipv6() {
                label.push_str(", IPv6");
            }
            (label, *address)
        })
        .collect();
    // The home network first: it is the one the device being linked is most likely on, and
    // the first link is the one whose QR code is drawn.
    found.sort_by_key(|(label, address)| (rank(label), address.is_ipv6()));
    // One link per label. An interface with several addresses of one family — IPv6 hands out
    // temporary ones by the handful — is one network to the device being linked, reached by
    // any of them, and six links for it is a list nobody reads to the end.
    let mut seen = std::collections::BTreeSet::new();
    found.retain(|(label, _)| seen.insert(label.clone()));
    found
}

const LOCAL: &str = "réseau local";
const TAILSCALE: &str = "Tailscale";

fn rank(label: &str) -> u8 {
    if label.starts_with(LOCAL) {
        0
    } else if label.starts_with(TAILSCALE) {
        1
    } else {
        2
    }
}

/// « Tailscale » for its range or its interface, « réseau local » for a private address,
/// and nothing when the address says neither — the caller then names the interface.
fn label(address: IpAddr, interface: Option<&str>) -> Option<String> {
    let tailscale = interface.is_some_and(|name| name.starts_with("tailscale"))
        || matches!(address, IpAddr::V4(v4) if shared_range(v4));
    if tailscale {
        return Some(TAILSCALE.into());
    }
    let private = match address {
        IpAddr::V4(v4) => v4.is_private(),
        IpAddr::V6(v6) => unique_local(v6),
    };
    private.then(|| LOCAL.into())
}

/// 100.64.0.0/10, the carrier-grade NAT range Tailscale hands its machines addresses from.
fn shared_range(address: Ipv4Addr) -> bool {
    let [a, b, ..] = address.octets();
    a == 100 && (64..128).contains(&b)
}

/// fc00::/7, IPv6's own private range.
fn unique_local(address: Ipv6Addr) -> bool {
    address.segments()[0] & 0xfe00 == 0xfc00
}
