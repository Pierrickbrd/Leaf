//! Configured through the environment, and from nowhere else.
//!
//! One place to look when a server is behaving oddly, and one thing for a unit file to set.
//! Nothing here reads a configuration file: a setting that can arrive by two routes is a
//! setting somebody will change in the wrong one.

use std::env;
use std::path::PathBuf;

pub struct Config {
    /// The root that is read and written.
    pub library: PathBuf,
    /// Transfers in flight. Must sit on the library's filesystem: committing an import is
    /// a rename, instant and atomic. On another volume it would be a multi-gigabyte copy,
    /// and neither of those two things.
    pub inbox: PathBuf,
    /// Resized pages, disposable.
    pub cache: PathBuf,
    /// The index, rebuildable.
    pub db: PathBuf,
    /// A folder the application and the server share, for the short path.
    pub drop: Option<PathBuf>,
    /// Read `X-Forwarded-For`. Only correct behind a proxy that sets it.
    pub trust_proxy: bool,
    /// The ceiling on a single upload.
    pub max_upload_bytes: u64,
    /// Serving HTTPS directly, for the case where nothing sits in front.
    pub tls_certificate: Option<PathBuf>,
    pub tls_key: Option<PathBuf>,
    pub tls_hosts: Vec<String>,
    pub host: String,
    pub port: u16,
}

impl Config {
    pub fn from_env() -> Self {
        Self::read(|name| env::var(name).ok())
    }

    /// Every setting, from wherever they come.
    ///
    /// `from_env` reads the process environment; a test reads whatever it likes. The two
    /// are the same code, because a defaulting rule that only the real environment can
    /// exercise is a rule nothing checks — and every value below has one.
    pub fn read(get: impl Fn(&str) -> Option<String>) -> Self {
        let get = |name: &str| get(name).filter(|v| !v.is_empty());
        let path = |name: &str| get(name).map(PathBuf::from);
        let library = path("LEAF_LIBRARY").unwrap_or_else(|| PathBuf::from("library"));
        let beside = |name: &str| {
            library
                .parent()
                .unwrap_or_else(|| std::path::Path::new("."))
                .join(name)
        };
        Config {
            inbox: path("LEAF_INBOX").unwrap_or_else(|| beside("inbox")),
            cache: path("LEAF_CACHE").unwrap_or_else(|| beside("cache")),
            db: path("LEAF_DB").unwrap_or_else(|| PathBuf::from("data/leaf.sqlite")),
            drop: path("LEAF_DROP"),
            trust_proxy: matches!(
                get("LEAF_TRUST_PROXY")
                    .unwrap_or_default()
                    .to_lowercase()
                    .as_str(),
                "1" | "true" | "yes"
            ),
            max_upload_bytes: get("LEAF_MAX_UPLOAD_MB")
                .and_then(|v| v.parse::<u64>().ok())
                .unwrap_or(2048)
                * 1024
                * 1024,
            tls_certificate: path("LEAF_TLS_CERT"),
            // Beside the certificate unless it is named: two paths for one thing is two
            // chances to configure half of it.
            tls_key: path("LEAF_TLS_KEY")
                .or_else(|| path("LEAF_TLS_CERT").map(|c| c.with_extension("key"))),
            tls_hosts: get("LEAF_TLS_HOSTS")
                .map(|v| {
                    v.split(',')
                        .map(str::trim)
                        .filter(|h| !h.is_empty())
                        .map(str::to_string)
                        .collect()
                })
                .unwrap_or_default(),
            // Loopback by default. A server run straight on a desktop has no business
            // being reachable from the rest of the house, and a warning is not a
            // protection. Binding wider is a thing the unit file says out loud.
            host: get("LEAF_HOST").unwrap_or_else(|| "127.0.0.1".into()),
            port: get("LEAF_PORT")
                .and_then(|v| v.parse().ok())
                .unwrap_or(8081),
            library,
        }
    }
}
