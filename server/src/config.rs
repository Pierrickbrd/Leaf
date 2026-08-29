//! Configured through the environment, so the container has nothing else to do.
//!
//! The same names the Kotlin server reads, deliberately: the compose file, the `.env` and
//! every note written about running it stay true across the port.

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
                env::var("LEAF_TRUST_PROXY")
                    .unwrap_or_default()
                    .to_lowercase()
                    .as_str(),
                "1" | "true" | "yes"
            ),
            max_upload_bytes: env::var("LEAF_MAX_UPLOAD_MB")
                .ok()
                .and_then(|v| v.parse::<u64>().ok())
                .unwrap_or(2048)
                * 1024
                * 1024,
            tls_certificate: path("LEAF_TLS_CERT"),
            // Beside the certificate unless it is named: two paths for one thing is two
            // chances to configure half of it.
            tls_key: path("LEAF_TLS_KEY")
                .or_else(|| path("LEAF_TLS_CERT").map(|c| c.with_extension("key"))),
            tls_hosts: env::var("LEAF_TLS_HOSTS")
                .ok()
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
            // protection. The container sets 0.0.0.0 explicitly.
            host: env::var("LEAF_HOST").unwrap_or_else(|_| "127.0.0.1".into()),
            port: env::var("LEAF_PORT")
                .ok()
                .and_then(|v| v.parse().ok())
                .unwrap_or(8081),
            library,
        }
    }
}

fn path(name: &str) -> Option<PathBuf> {
    env::var(name)
        .ok()
        .filter(|v| !v.is_empty())
        .map(PathBuf::from)
}
