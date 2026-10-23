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
use crate::net::tls::{observe, Presented};

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

/// Where one link points, and what its operator should hear about it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Target {
    /// `None` for the one address given or configured: there is nothing to tell it from.
    /// Some label for one of the server's own listening addresses.
    pub label: Option<String>,
    pub address: Address,
    pub fingerprint: Option<String>,
    pub notes: Vec<Note>,
}

impl Target {
    /// The link to this target for `code`.
    pub fn link(&self, code: &str) -> Link {
        Link {
            https: self.address.https,
            host: self.address.host.clone(),
            port: self.address.port,
            code: code.to_string(),
            fingerprint: self.fingerprint.clone(),
        }
    }
}

/// What the person who runs `device add` is told about the certificate a link pins. Words
/// belong to the command; the server's own route needs none of them.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Note {
    /// The address did not answer from the server: Leaf's own certificate is pinned instead.
    Unreachable,
    /// The address did not answer, and Leaf, on plain http, has no certificate to pin.
    UnreachableNoCertificate,
    /// The address presented a certificate that is neither Leaf's nor publicly signed, by
    /// its fingerprint.
    NotLeafs(String),
}

/// Every address a link can point at, or why there is none.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Targets {
    Found(Vec<Target>),
    /// Leaf listens only on the loopback and nothing says where else it is reached.
    OnlyHere,
    /// Leaf listens on an address no interface of this machine carries.
    NoneListening,
}

/// Every address a new device may be handed, in the order a person should see them, and the
/// certificate each pins.
///
/// One function for `leaf-server device add` and for `POST /link/codes`: the second used to
/// be built by the client, from the address *it* reached the server at — the loopback on the
/// machine that runs the server, a tunnel's end through SSH — and handed a person a link that
/// worked nowhere but where they already were.
///
/// `configured` is `--address` or `LEAF_PUBLIC_URL`, first. Otherwise the server's own
/// listening addresses past the loopback, which carry Leaf's certificate. `own` is asked only
/// when it is needed, since asking may make the certificate.
pub fn targets(
    configured: Option<&str>,
    host: &str,
    port: u16,
    own: &dyn Fn() -> Result<Option<String>>,
) -> Result<Targets> {
    if let Some(typed) = configured {
        let address = Address::parse(typed)?;
        let (fingerprint, notes) = if address.https {
            decide(&address, own)?
        } else {
            (None, Vec::new())
        };
        return Ok(Targets::Found(vec![Target {
            label: None,
            address,
            fingerprint,
            notes,
        }]));
    }
    if is_loopback(host) {
        return Ok(Targets::OnlyHere);
    }
    // Leaf's own addresses: the certificate there is Leaf's, nothing to ask them.
    let fingerprint = own()?;
    let found = listening_addresses(host);
    if found.is_empty() {
        return Ok(Targets::NoneListening);
    }
    Ok(Targets::Found(
        found
            .into_iter()
            .map(|(label, ip)| Target {
                label: Some(label),
                address: Address {
                    https: true,
                    host: ip.to_string(),
                    port,
                },
                fingerprint: fingerprint.clone(),
                notes: Vec::new(),
            })
            .collect(),
    ))
}

/// The fingerprint a link to an https address carries, by asking the address what it
/// presents. Leaf's own is asked for only when it is needed, since asking may make it.
fn decide(
    address: &Address,
    own: &dyn Fn() -> Result<Option<String>>,
) -> Result<(Option<String>, Vec<Note>)> {
    Ok(match observe(&address.host, address.port) {
        Presented::PublicAuthority => (None, Vec::new()),
        Presented::Pinned(fingerprint) => {
            // Leaf's own certificate does not expire for twenty years; anything else pinned
            // here — a proxy's, from a private authority — will, and the link with it.
            let notes = if own()?.as_ref() == Some(&fingerprint) {
                Vec::new()
            } else {
                vec![Note::NotLeafs(fingerprint.clone())]
            };
            (Some(fingerprint), notes)
        }
        Presented::Unreachable(_) => match own()? {
            Some(fingerprint) => (Some(fingerprint), vec![Note::Unreachable]),
            None => (None, vec![Note::UnreachableNoCertificate]),
        },
    })
}

/// What a running server knows of where other devices reach it: `LEAF_PUBLIC_URL`, where it
/// listens, and the certificate it serves there. Read once at startup; what `POST
/// /link/codes` builds its link from, and what `/health` answers `relay` from.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Reach {
    pub public_url: Option<String>,
    pub host: String,
    pub port: u16,
    /// Leaf's own certificate, as a link carries it; `None` on plain http.
    pub fingerprint: Option<String>,
}

impl Reach {
    /// A server listening on `host:port`, with no public address declared.
    pub fn listening_on(host: &str, port: u16, fingerprint: Option<String>) -> Reach {
        Reach {
            public_url: None,
            host: host.to_string(),
            port,
            fingerprint,
        }
    }

    /// Where a device's relay points, decided once: `main` calls it at start, off the async
    /// threads, and the routes keep what it found.
    ///
    /// Once, because what it asks — which certificate `LEAF_PUBLIC_URL` presents — was asked
    /// again for every code, up to ten seconds each when the address dropped packets, and
    /// found the same thing every time.
    ///
    /// A public address whose certificate could not be checked — unreachable from here, or
    /// presenting one that is neither a public authority's nor Leaf's — gets a link that pins
    /// nothing. It used to pin Leaf's own certificate as a guess: behind a proxy holding a
    /// public certificate, with no way from the server back to the public name, every phone
    /// was then shown a certificate alarm for a certificate it never sees; reached from
    /// inside, the public address can land on the router's own page, whose certificate would
    /// have been pinned. With no fingerprint, the client trusts only what a public authority
    /// signed — safe whatever answers there. `device add` is where an operator checks and
    /// pins anything else.
    pub fn settle(&self) -> Relay {
        if let Some(typed) = &self.public_url {
            let address = match Address::parse(typed) {
                Ok(address) => address,
                Err(e) => {
                    tracing::warn!(
                        "LEAF_PUBLIC_URL is not an address, no device can relay a link: {e}"
                    );
                    return Relay::Nowhere;
                }
            };
            let own = || Ok(self.fingerprint.clone());
            let fingerprint = if !address.https {
                None
            } else {
                match decide(&address, &own) {
                    Ok((fingerprint, notes)) if notes.is_empty() => fingerprint,
                    Ok((_, notes)) => {
                        for note in &notes {
                            match note {
                                Note::NotLeafs(theirs) => tracing::warn!(
                                    fingerprint = %theirs,
                                    "LEAF_PUBLIC_URL presents a certificate that is neither Leaf's nor publicly signed: relayed links pin nothing; check it and use `device add` to pin it"
                                ),
                                Note::Unreachable | Note::UnreachableNoCertificate => tracing::warn!(
                                    "LEAF_PUBLIC_URL does not answer from this machine: relayed links pin nothing, and work only where a public authority signed what answers there"
                                ),
                            }
                        }
                        None
                    }
                    Err(e) => {
                        tracing::warn!("Leaf's own certificate could not be read: {e}");
                        None
                    }
                }
            };
            tracing::info!(
                address = typed,
                pinned = fingerprint.is_some(),
                "Relayed links point at LEAF_PUBLIC_URL"
            );
            return Relay::To {
                target: Target {
                    label: None,
                    address,
                    fingerprint,
                    notes: Vec::new(),
                },
                local_network: false,
            };
        }
        if is_loopback(&self.host) {
            tracing::info!("Leaf listens only on this machine and LEAF_PUBLIC_URL is not set: no device can relay a link");
            return Relay::Nowhere;
        }
        let found = listening_among(&self.host, &system_interfaces());
        let Some(chosen) = relay_choice(&found) else {
            tracing::warn!(host = %self.host, "no address another device can reach: no device can relay a link");
            return Relay::Nowhere;
        };
        tracing::info!(address = %chosen.ip, label = %chosen.label, "Relayed links point at a listening address");
        Relay::To {
            target: Target {
                label: Some(chosen.label.clone()),
                address: Address {
                    https: true,
                    host: chosen.ip.to_string(),
                    port: self.port,
                },
                fingerprint: self.fingerprint.clone(),
                notes: Vec::new(),
            },
            local_network: local_network(chosen.ip),
        }
    }
}

/// Where a device's relay points, as [`Reach::settle`] decided it at start.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Relay {
    To {
        target: Target,
        /// The address only works from the server's own network.
        local_network: bool,
    },
    /// Nobody but this machine reaches the server.
    Nowhere,
}

impl Relay {
    /// Whether another device has an address to reach this server at — and so whether a
    /// device linked here may link another, and leave without stranding itself.
    pub fn possible(&self) -> bool {
        matches!(self, Relay::To { .. })
    }
}

/// One address of this machine, as its interfaces list it.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Interface {
    pub name: String,
    pub ip: IpAddr,
}

/// An address a server listens on, under the label a person tells it apart by, and the
/// interface that carries it — empty for a host bound to one address no interface carries.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Listening {
    pub label: String,
    pub ip: IpAddr,
    pub interface: String,
}

/// The addresses a server bound to `host` can be reached on, each with a label to tell them
/// apart in front of a person.
pub fn listening_addresses(host: &str) -> Vec<(String, IpAddr)> {
    listening_among(host, &system_interfaces())
        .into_iter()
        .map(|found| (found.label, found.ip))
        .collect()
}

fn system_interfaces() -> Vec<Interface> {
    if_addrs::get_if_addrs()
        .unwrap_or_default()
        .into_iter()
        .map(|i| Interface {
            ip: i.ip(),
            name: i.name,
        })
        .collect()
}

/// [`listening_addresses`] among `interfaces` rather than the machine's own, so that a test
/// can hand it a machine it does not have.
///
/// The host itself when it is one address. For `0.0.0.0` and `::`, which no client can dial,
/// the addresses past the loopback — IPv4 only for `0.0.0.0`, both for `::`, which Linux
/// binds dual-stack. Link-local addresses are left out: one is only reachable with an
/// interface named after it, which a link cannot carry.
pub fn listening_among(host: &str, interfaces: &[Interface]) -> Vec<Listening> {
    let name_of = |address: IpAddr| {
        interfaces
            .iter()
            .find(|i| i.ip == address)
            .map(|i| i.name.as_str())
    };
    let host = host.trim_start_matches('[').trim_end_matches(']');
    let Ok(bound) = host.parse::<IpAddr>() else {
        return Vec::new();
    };
    if !bound.is_unspecified() {
        let label = label(bound, name_of(bound)).unwrap_or_else(|| host.to_string());
        return vec![Listening {
            label,
            ip: bound,
            interface: name_of(bound).unwrap_or_default().to_string(),
        }];
    }
    // Each address under what it is, or under its interface when it says nothing.
    let reachable: Vec<(&str, IpAddr, String)> = interfaces
        .iter()
        .filter(|i| !i.ip.is_loopback() && !link_local(i.ip))
        .filter(|i| bound.is_ipv6() || i.ip.is_ipv4())
        .map(|i| {
            let what = label(i.ip, Some(&i.name)).unwrap_or_else(|| i.name.clone());
            (i.name.as_str(), i.ip, what)
        })
        .collect();
    let mut found: Vec<Listening> = reachable
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
            Listening {
                label,
                ip: *address,
                interface: interface.to_string(),
            }
        })
        .collect();
    // The home network first: it is the one the device being linked is most likely on, and
    // the first link is the one whose QR code is drawn.
    found.sort_by_key(|f| (rank(&f.label), f.ip.is_ipv6()));
    // One link per label. An interface with several addresses of one family — IPv6 hands out
    // temporary ones by the handful — is one network to the device being linked, reached by
    // any of them, and six links for it is a list nobody reads to the end.
    let mut seen = std::collections::BTreeSet::new();
    found.retain(|f| seen.insert(f.label.clone()));
    found
}

/// The one listening address a device's relay hands another: the first that is not a
/// container's or a virtual machine's bridge.
///
/// On `0.0.0.0` with Docker, `docker0` (172.17.0.1) is a private address like the home
/// network's, ranked « réseau local » beside it and ahead of it, and was the address a
/// phone was handed. `device add` still lists every one, under names that tell them apart;
/// a single QR code has no room for that.
pub fn relay_choice(found: &[Listening]) -> Option<&Listening> {
    const BRIDGES: [&str; 7] = ["docker", "br-", "virbr", "veth", "cni", "flannel", "podman"];
    found
        .iter()
        .find(|f| !BRIDGES.iter().any(|bridge| f.interface.starts_with(bridge)))
}

/// 169.254.0.0/16 and fe80::/10.
fn link_local(address: IpAddr) -> bool {
    match address {
        IpAddr::V4(v4) => v4.is_link_local(),
        IpAddr::V6(v6) => v6.segments()[0] & 0xffc0 == 0xfe80,
    }
}

/// Whether a link to `address` only works from a network of its own: private, link-local,
/// Tailscale's range, IPv6's unique-local — the cases « le même réseau que la bibliothèque »
/// is true of. Not the label: a host bound to one public address is its own label.
pub fn local_network(address: IpAddr) -> bool {
    match address {
        IpAddr::V4(v4) => v4.is_private() || v4.is_link_local() || shared_range(v4),
        IpAddr::V6(v6) => unique_local(v6) || link_local(address),
    }
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
