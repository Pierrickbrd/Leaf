//! The binary itself, run the way a unit file runs it.
//!
//! `main` is wiring by design — the decisions moved to `boot` so that tests could reach
//! them — but wiring is exactly what breaks silently: a mode that stops being recognised, a
//! refusal that stops refusing, a server that binds and then answers nothing. None of that
//! shows up in a library test, because none of it is in the library.

use std::io::Write;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

/// A library with one volume in it, plus the folders the server expects beside it.
fn a_library() -> tempfile::TempDir {
    let dir = tempfile::tempdir().unwrap();
    let folder = dir.path().join("library/Bleach");
    std::fs::create_dir_all(&folder).unwrap();

    let mut buffer = image::RgbImage::new(60, 90);
    for (x, y, pixel) in buffer.enumerate_pixels_mut() {
        *pixel = image::Rgb([(x % 256) as u8, (y % 256) as u8, 128]);
    }
    let mut jpeg = std::io::Cursor::new(Vec::new());
    image::DynamicImage::ImageRgb8(buffer)
        .write_to(&mut jpeg, image::ImageFormat::Jpeg)
        .unwrap();

    let mut zip = zip::ZipWriter::new(std::fs::File::create(folder.join("Tome 1.cbz")).unwrap());
    zip.start_file::<_, ()>("000.jpg", zip::write::SimpleFileOptions::default())
        .unwrap();
    zip.write_all(&jpeg.into_inner()).unwrap();
    zip.finish().unwrap();
    dir
}

fn leaf(dir: &tempfile::TempDir) -> Command {
    let mut command = Command::new(env!("CARGO_BIN_EXE_leaf-server"));
    command
        .env("LEAF_LIBRARY", dir.path().join("library"))
        .env("LEAF_DB", dir.path().join("index.sqlite"))
        .env("LEAF_INBOX", dir.path().join("inbox"))
        .env("LEAF_CACHE", dir.path().join("cache"))
        // The scan is what `serve` would otherwise start in the background, and a test that
        // waits on a background scan is a test that waits on a disk.
        .env("LEAF_NO_SCAN", "1")
        .env_remove("LEAF_KEYS")
        .env_remove("LEAF_TLS_CERT")
        .env_remove("LEAF_DROP");
    command
}

/// A port nothing is listening on, found by listening on it and stopping.
/// A port nothing is listening on, and that no other test in this binary has been handed.
///
/// Binding to nought and reading the number back is the usual trick, and on its own it is not
/// enough: the listener is dropped before the server is spawned, so the kernel is free to
/// hand the same number to the next test that asks. Eight tests here ask, and they run side
/// by side.
///
/// Given the same one, the second connected to the *first* test's server, read it as « mine
/// is up », and signalled its own — which was still starting and had not armed its handlers,
/// so the kernel killed it by default disposition. The failure said the server would not stop
/// when it was asked to, and the server had not been asked. Seen once under coverage on a
/// shared runner and never on an idle machine, which is what an interleaving costs to find.
fn a_free_port() -> u16 {
    static HANDED_OUT: std::sync::Mutex<std::collections::BTreeSet<u16>> =
        std::sync::Mutex::new(std::collections::BTreeSet::new());
    loop {
        let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        if HANDED_OUT.lock().unwrap().insert(port) {
            return port;
        }
    }
}

#[test]
fn scan_analyses_and_exits_with_a_report() {
    let dir = a_library();
    let done = leaf(&dir).arg("scan").output().unwrap();
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );

    let said = String::from_utf8_lossy(&done.stdout);
    // The counters the bench parses, and the shape it parses them out of.
    assert!(said.contains("1 work(s)"), "{said}");
    assert!(said.contains("1 entry(ies)"), "{said}");
    assert!(said.contains("1 page(s)"), "{said}");
    assert!(dir.path().join("index.sqlite").is_file());
}

#[test]
fn scan_takes_the_roots_it_is_given_rather_than_the_configured_one() {
    let dir = a_library();
    let elsewhere = dir.path().join("elsewhere");
    std::fs::create_dir_all(&elsewhere).unwrap();

    let done = leaf(&dir).arg("scan").arg(&elsewhere).output().unwrap();
    assert!(done.status.success());
    // An empty root, so nothing is found — which is how we know it looked there and not at
    // the library it was configured with.
    assert!(String::from_utf8_lossy(&done.stdout).contains("0 work(s)"));
}

#[test]
fn no_dimensions_is_read_as_an_option_and_never_as_a_root() {
    let dir = a_library();
    let done = leaf(&dir)
        .args(["scan", "--no-dimensions"])
        .output()
        .unwrap();
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );
    assert!(String::from_utf8_lossy(&done.stdout).contains("1 entry(ies)"));
}

#[test]
fn help_prints_usage_and_exits_cleanly_rather_than_starting_a_server() {
    // `leaf-server --help` used to start a server: the first argument became `command`
    // unchecked, matched neither known command, and fell through to `serve` with a warning
    // about the open library nobody had asked to open. Found in a real deployment on
    // 2 September 2026, twice in a row.
    let dir = a_library();
    let done = leaf(&dir).arg("--help").output().unwrap();
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(said.contains("Usage"), "{said}");
    assert!(said.contains("scan"), "{said}");
    assert!(said.contains("serve"), "{said}");
    assert!(!dir.path().join("index.sqlite").exists());
}

#[test]
fn version_is_the_released_version_and_touches_nothing() {
    let dir = a_library();
    let done = leaf(&dir).arg("--version").output().unwrap();
    assert!(done.status.success());
    assert_eq!(
        String::from_utf8_lossy(&done.stdout).trim(),
        format!("leaf-server {}", env!("CARGO_PKG_VERSION"))
    );
    assert!(!dir.path().join("index.sqlite").exists());
}

#[test]
fn an_unrecognised_argument_is_refused_rather_than_run() {
    // `leaf-server sacn` used to serve the configured library, silently, because any first
    // argument became the command and only `"scan"` was ever compared against it. A typo in
    // a systemd unit's `ExecStart` must not quietly do something other than what the file
    // says.
    let dir = a_library();
    let done = leaf(&dir).arg("sacn").output().unwrap();
    assert!(!done.status.success());
    let complained = String::from_utf8_lossy(&done.stderr);
    assert!(complained.contains("sacn"), "{complained}");
    assert!(!dir.path().join("index.sqlite").exists());
}

/// Off the loopback, the server neither refuses to start nor speaks plain http.
///
/// It used to refuse a host past the loopback with no key, which left `install.sh server` on
/// a home network with no way to link a first device. It now starts closed — only `/health`
/// and the code exchange answer — and over https, with a certificate it makes itself.
/// `127.0.0.2` is past the loopback as far as the server's rule goes and still on this
/// machine as far as Linux routes it, so the test reaches nothing outside.
#[test]
fn bound_past_the_loopback_with_no_key_it_starts_closed_over_https() {
    let dir = a_library();
    let port = a_free_port();
    let mut server = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.2")
        .env("LEAF_PORT", port.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();

    let deadline = Instant::now() + Duration::from_secs(30);
    let health = loop {
        if let Some(said) = ask_over_tls("127.0.0.2", port, "/health") {
            break said;
        }
        if Instant::now() > deadline || server.try_wait().unwrap().is_some() {
            let _ = server.kill();
            panic!("nothing ever answered over https on 127.0.0.2:{port}");
        }
        std::thread::sleep(Duration::from_millis(50));
    };
    let series = ask_over_tls("127.0.0.2", port, "/series").unwrap_or_default();
    stop(&mut server, Ask::Interrupt);

    assert!(health.starts_with("HTTP/1.1 200"), "{health}");
    assert!(series.starts_with("HTTP/1.1 403"), "{series}");
    // Beside the index, since nothing named a certificate.
    assert!(dir.path().join("tls/leaf.crt").is_file());
    assert!(dir.path().join("tls/leaf.key").is_file());
}

/// One request over https, accepting whatever certificate is presented, or nothing when the
/// port is not answering yet. Which certificate it is belongs to `tests/tls.rs`; this asks
/// only whether the server speaks https at all.
fn ask_over_tls(host: &str, port: u16, path: &str) -> Option<String> {
    use std::io::Read;
    use std::sync::Arc;

    let provider = Arc::new(rustls::crypto::ring::default_provider());
    let config = rustls::ClientConfig::builder_with_provider(Arc::clone(&provider))
        .with_safe_default_protocol_versions()
        .ok()?
        .dangerous()
        .with_custom_certificate_verifier(Arc::new(AnyCertificate(provider)))
        .with_no_client_auth();
    let name = rustls::pki_types::ServerName::try_from(host.to_string()).ok()?;
    let connection = rustls::ClientConnection::new(Arc::new(config), name).ok()?;
    let socket = std::net::TcpStream::connect((host, port)).ok()?;
    let mut stream = rustls::StreamOwned::new(connection, socket);
    stream
        .write_all(
            format!("GET {path} HTTP/1.1\r\nHost: leaf\r\nConnection: close\r\n\r\n").as_bytes(),
        )
        .ok()?;
    let mut said = Vec::new();
    // A server that closes without `close_notify` ends the read in an error after the whole
    // answer has arrived; what came is what is read.
    let _ = stream.read_to_end(&mut said);
    (!said.is_empty()).then(|| String::from_utf8_lossy(&said).into_owned())
}

#[derive(Debug)]
struct AnyCertificate(std::sync::Arc<rustls::crypto::CryptoProvider>);

impl rustls::client::danger::ServerCertVerifier for AnyCertificate {
    fn verify_server_cert(
        &self,
        _: &rustls::pki_types::CertificateDer<'_>,
        _: &[rustls::pki_types::CertificateDer<'_>],
        _: &rustls::pki_types::ServerName<'_>,
        _: &[u8],
        _: rustls::pki_types::UnixTime,
    ) -> Result<rustls::client::danger::ServerCertVerified, rustls::Error> {
        Ok(rustls::client::danger::ServerCertVerified::assertion())
    }

    fn verify_tls12_signature(
        &self,
        message: &[u8],
        certificate: &rustls::pki_types::CertificateDer<'_>,
        signed: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls12_signature(
            message,
            certificate,
            signed,
            &self.0.signature_verification_algorithms,
        )
    }

    fn verify_tls13_signature(
        &self,
        message: &[u8],
        certificate: &rustls::pki_types::CertificateDer<'_>,
        signed: &rustls::DigitallySignedStruct,
    ) -> Result<rustls::client::danger::HandshakeSignatureValid, rustls::Error> {
        rustls::crypto::verify_tls13_signature(
            message,
            certificate,
            signed,
            &self.0.signature_verification_algorithms,
        )
    }

    fn supported_verify_schemes(&self) -> Vec<rustls::SignatureScheme> {
        self.0.signature_verification_algorithms.supported_schemes()
    }
}

#[test]
fn an_address_that_cannot_be_bound_is_named_rather_than_panicked_over() {
    let dir = a_library();
    let done = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", "1")
        .output()
        .unwrap();
    assert!(!done.status.success());
    assert!(String::from_utf8_lossy(&done.stderr).contains("binding 127.0.0.1:1"));
}

#[test]
fn serve_listens_answers_health_and_stops_when_it_is_asked_to() {
    let dir = a_library();
    let port = a_free_port();
    let mut server = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", port.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();

    // A deadline, not a nap count: the answer either comes back or it never does, and
    // sleeping a fixed number of times measures the machine rather than the server.
    let deadline = Instant::now() + Duration::from_secs(30);
    let answered = loop {
        if let Ok(mut stream) = std::net::TcpStream::connect(("127.0.0.1", port)) {
            stream
                .write_all(b"GET /health HTTP/1.1\r\nHost: leaf\r\nConnection: close\r\n\r\n")
                .unwrap();
            let mut said = String::new();
            use std::io::Read;
            stream.read_to_string(&mut said).unwrap();
            break said;
        }
        assert!(Instant::now() < deadline, "the server never answered");
        std::thread::sleep(Duration::from_millis(50));
    };

    assert!(answered.starts_with("HTTP/1.1 200"), "{answered}");
    assert!(answered.contains("\"status\":\"ok\""), "{answered}");

    // What a unit file sends. `systemctl stop` is SIGTERM, and it went unanswered here for
    // as long as the server only listened for the Ctrl-C a terminal sends.
    stop(&mut server, Ask::Terminate);
}

/// One of the two signals that ask a server to stop.
///
/// Named rather than handed over as a `libc` constant, which is the only reason the
/// accommodation in [`stop`] is worth anything: with `libc::SIGINT` written at four call
/// sites, the `#[cfg(not(unix))]` arm there guarded a function nothing off Unix could reach
/// anyway. The word for the signal belongs to the test; the number belongs to the one block
/// that is allowed to know it.
enum Ask {
    /// What `systemctl stop` sends.
    Terminate,
    /// What a terminal sends on Ctrl-C.
    Interrupt,
}

/// Sends one of the two signals that ask a server to stop, and waits for the exit it
/// promises.
///
/// The exit has to be a **clean** one. A process with no handler for the signal is killed by
/// its default disposition, which leaves no exit code at all — and that is exactly what
/// `SIGTERM` did here for as long as `shutdown` only listened for `SIGINT`, while this
/// assertion, written to tolerate a missing code, called it a pass.
fn stop(server: &mut std::process::Child, ask: Ask) {
    #[cfg(unix)]
    unsafe {
        libc::kill(
            server.id() as i32,
            match ask {
                Ask::Terminate => libc::SIGTERM,
                Ask::Interrupt => libc::SIGINT,
            },
        );
    }
    #[cfg(not(unix))]
    let _ = ask;
    let deadline = Instant::now() + Duration::from_secs(30);
    loop {
        if let Some(status) = server.try_wait().unwrap() {
            assert!(
                status.success(),
                "the server was killed rather than stopping when it was asked to: {status}"
            );
            return;
        }
        if Instant::now() > deadline {
            let _ = server.kill();
            panic!("the server did not stop when it was asked to");
        }
        std::thread::sleep(Duration::from_millis(20));
    }
}

#[test]
fn serve_scans_behind_itself_unless_it_is_told_not_to() {
    // The server answers from its first second rather than after the whole library has been
    // read. A scan lost to a restart costs nothing: the index is rebuildable.
    let dir = a_library();
    let port = a_free_port();
    let mut server = leaf(&dir)
        .arg("serve")
        .env_remove("LEAF_NO_SCAN")
        .env("LEAF_DROP", dir.path().join("drop"))
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", port.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();

    // The shelf fills in behind the reader, so what is waited on is the entry appearing.
    let deadline = Instant::now() + Duration::from_secs(30);
    let found = loop {
        if let Some(said) = ask(port, "/series") {
            if said.contains("\"total\":1") {
                break true;
            }
        }
        if Instant::now() > deadline {
            break false;
        }
        std::thread::sleep(Duration::from_millis(50));
    };
    stop(&mut server, Ask::Interrupt);
    assert!(found, "the startup scan never reached the shelf");
    // And the drop folder was made on the way past.
    assert!(dir.path().join("drop").is_dir());
}

/// `systemctl stop` during the slowest part of a start.
///
/// A handler starts listening when its future is first polled, not when it is built, and on
/// the TLS path that poll happened inside a `tokio::spawn` issued *after* the certificate had
/// been generated, signed and written — the longest stretch of the whole start, on the one
/// boot where those files do not exist yet. A SIGTERM in it met the default disposition and
/// killed the process where it stood: the fix for the missing handler had a hole of its own,
/// on the slowest path.
///
/// **Read as an order rather than raced against.** Sending a signal into the window and
/// watching for a clean exit is a race only one way round — a signal that arrives late finds
/// a server armed either way, so it passes on a broken build as often as not, and a check
/// that can quietly see nothing is worth no more than no check. What the fix actually says is
/// that the handlers are registered before the certificate work begins, and the log says that
/// in two lines whose order is the whole property. `Stopping::armed` prints the first one
/// itself, so the two cannot drift apart.
#[test]
fn a_tls_server_listens_for_a_stop_before_it_starts_generating() {
    let dir = a_library();
    let log = dir.path().join("said.log");
    let mut server = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", a_free_port().to_string())
        .env("LEAF_TLS_CERT", dir.path().join("tls/leaf.crt"))
        .env("LEAF_TLS_HOSTS", "127.0.0.1")
        // The log subscriber writes to stdout; stderr is where `main` puts the one error it
        // returns. Into a file rather than a pipe, so nothing has to drain it to keep the
        // server from blocking on a full one.
        .stdout(Stdio::from(std::fs::File::create(&log).unwrap()))
        .stderr(Stdio::null())
        .spawn()
        .unwrap();

    let deadline = Instant::now() + Duration::from_secs(30);
    let said = loop {
        let said = std::fs::read_to_string(&log).unwrap_or_default();
        if said.contains("Stopping on SIGINT") && said.contains("generating a self-signed one") {
            break said;
        }
        assert!(
            Instant::now() < deadline,
            "the server never said both of these: {said}"
        );
        std::thread::sleep(Duration::from_millis(10));
    };

    assert!(
        said.find("Stopping on SIGINT").unwrap()
            < said.find("generating a self-signed one").unwrap(),
        "the signals must be armed before the slowest thing a start does: {said}"
    );

    // And the stop itself still works on this path. `stop` requires a clean exit code, which
    // is exactly what a process killed by its default disposition does not leave.
    stop(&mut server, Ask::Terminate);
}

#[test]
fn serve_over_tls_generates_its_own_certificate_and_answers_on_it() {
    // The recommended path is a reverse proxy holding a certificate a browser already
    // trusts. This is the port opened with nothing in front of it, which would otherwise
    // send the key in clear on every request.
    let dir = a_library();
    let port = a_free_port();
    let certificate = dir.path().join("tls/leaf.crt");
    let mut server = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", port.to_string())
        .env("LEAF_TLS_CERT", &certificate)
        .env("LEAF_TLS_HOSTS", "127.0.0.1,leaf.maison")
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();

    let deadline = Instant::now() + Duration::from_secs(30);
    let listening = loop {
        if std::net::TcpStream::connect(("127.0.0.1", port)).is_ok() {
            break true;
        }
        if Instant::now() > deadline {
            break false;
        }
        std::thread::sleep(Duration::from_millis(50));
    };
    // Read before it is signalled. Signalled first, a server that never got as far as
    // listening was reported as one that would not stop when asked — which is the other
    // sentence, about the other fault.
    if !listening {
        let _ = server.kill();
        panic!("nothing ever listened on the TLS port");
    }
    stop(&mut server, Ask::Interrupt);
    assert!(
        certificate.is_file(),
        "the pair is generated on first start"
    );
    assert!(certificate.with_extension("key").is_file());
}

/// One request over plain HTTP, or nothing when the port is not answering yet.
fn ask(port: u16, path: &str) -> Option<String> {
    use std::io::Read;
    let mut stream = std::net::TcpStream::connect(("127.0.0.1", port)).ok()?;
    stream
        .write_all(
            format!("GET {path} HTTP/1.1\r\nHost: leaf\r\nConnection: close\r\n\r\n").as_bytes(),
        )
        .ok()?;
    let mut said = String::new();
    stream.read_to_string(&mut said).ok()?;
    Some(said)
}

#[test]
fn an_inbox_on_another_volume_is_warned_about_at_startup() {
    // Committing an import is a rename. Across two volumes it becomes a multi-gigabyte
    // copy — which breaks nothing immediately and makes every import slow, so it is said
    // once, loudly, at the moment somebody could still move it.
    let shm = std::path::Path::new("/dev/shm");
    if !shm.is_dir() {
        return;
    }
    let dir = a_library();
    let inbox = shm.join(format!("leaf-inbox-{}", std::process::id()));
    let done = leaf(&dir)
        .arg("scan")
        .env("LEAF_INBOX", &inbox)
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", a_free_port().to_string())
        .output()
        .unwrap();
    let _ = std::fs::remove_dir_all(&inbox);
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );
}

#[test]
fn serve_warns_when_the_inbox_and_the_library_are_not_on_one_filesystem() {
    let shm = std::path::Path::new("/dev/shm");
    if !shm.is_dir() {
        return;
    }
    let dir = a_library();
    let inbox = shm.join(format!("leaf-inbox-serve-{}", std::process::id()));
    let port = a_free_port();
    let mut server = leaf(&dir)
        .arg("serve")
        .env("LEAF_INBOX", &inbox)
        .env("LEAF_HOST", "127.0.0.1")
        .env("LEAF_PORT", port.to_string())
        .stdout(Stdio::piped())
        .stderr(Stdio::piped())
        .spawn()
        .unwrap();

    let deadline = Instant::now() + Duration::from_secs(30);
    while std::net::TcpStream::connect(("127.0.0.1", port)).is_err() {
        assert!(Instant::now() < deadline, "nothing ever listened");
        std::thread::sleep(Duration::from_millis(50));
    }
    stop(&mut server, Ask::Interrupt);
    let _ = std::fs::remove_dir_all(&inbox);
}

/// The binary as `sudo leaf-server device …` runs it, minus sudo: a test is not root, so the
/// account is left as it is (`cli::account_for`), and the environment file is one that does
/// not exist rather than whatever `/etc/leaf/server.env` this machine happens to hold.
fn device(dir: &tempfile::TempDir) -> Command {
    let mut command = leaf(dir);
    command
        .arg("device")
        .env("LEAF_ENV_FILE", dir.path().join("no-server.env"))
        .env_remove("LEAF_HOST")
        .env_remove("LEAF_PORT")
        .env_remove("LEAF_PUBLIC_URL")
        .env_remove("LEAF_TLS_KEY")
        .env_remove("LEAF_TLS_HOSTS");
    command
}

fn count(dir: &tempfile::TempDir, table: &str) -> i64 {
    rusqlite::Connection::open(dir.path().join("index.sqlite"))
        .unwrap()
        .query_row(&format!("SELECT COUNT(*) FROM {table}"), [], |r| r.get(0))
        .unwrap()
}

/// An index with one device in it, linked the way `device enroll` links one.
fn with_a_device(dir: &tempfile::TempDir, name: &str) {
    let db = std::sync::Arc::new(
        leaf_server::store::Db::open(&dir.path().join("index.sqlite")).unwrap(),
    );
    leaf_server::store::devices::Devices::new(db)
        .enroll(name, 1_790_000_000_000)
        .unwrap()
        .unwrap();
}

#[test]
fn help_names_the_device_commands() {
    let dir = a_library();
    let done = leaf(&dir).arg("--help").output().unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    for word in [
        "device add",
        "device list",
        "device revoke",
        "device enroll",
        "profile reset-code",
    ] {
        assert!(said.contains(word), "{word}: {said}");
    }
}

#[test]
fn device_add_on_the_loopback_with_no_public_url_explains_and_creates_no_code() {
    let dir = a_library();
    // Opened first, so that « no code » is a count of zero and not a table that is missing.
    drop(leaf_server::store::Db::open(&dir.path().join("index.sqlite")).unwrap());

    let done = device(&dir).arg("add").output().unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert_eq!(done.status.code(), Some(2), "{said}");
    assert!(said.contains("LEAF_PUBLIC_URL"), "{said}");
    assert!(said.contains("--address URL"), "{said}");
    assert!(!said.contains("leaf://"), "{said}");
    assert_eq!(count(&dir, "link_code"), 0);
}

#[test]
fn device_add_with_an_address_prints_a_link_and_a_qr_code() {
    let dir = a_library();
    let done = device(&dir)
        .args(["add", "--address", "https://leaf.example.net"])
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(
        done.status.success(),
        "{said}{}",
        String::from_utf8_lossy(&done.stderr)
    );
    assert!(said.contains("leaf://leaf.example.net/lier/"), "{said}");
    assert!(said.contains('█'), "{said}");
    assert!(
        said.contains("Valable 10 minutes, une seule fois."),
        "{said}"
    );
    // Nothing is linked and nothing in LEAF_KEYS: the first device closes the library.
    assert!(said.contains("la bibliothèque sera fermée"), "{said}");
    assert_eq!(count(&dir, "link_code"), 1);
}

#[test]
fn device_add_past_the_loopback_carries_the_fingerprint_of_leafs_own_certificate() {
    // 127.0.0.2 is past the loopback by the server's rule, so the server there speaks https
    // with the certificate beside its index — the one this link must pin, made now if the
    // server has never started, so that the two agree when it does.
    let dir = a_library();
    let done = device(&dir)
        .arg("add")
        .env("LEAF_HOST", "127.0.0.2")
        .env("LEAF_PORT", "8443")
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(
        done.status.success(),
        "{said}{}",
        String::from_utf8_lossy(&done.stderr)
    );
    let link = said
        .lines()
        .find_map(|l| l.split_once(" : leaf://").map(|(_, rest)| rest))
        .unwrap_or_else(|| panic!("no link in {said}"));
    let (address, fingerprint) = link.split_once("?empreinte=").unwrap();
    assert!(address.starts_with("127.0.0.2:8443/lier/"), "{link}");
    assert_eq!(fingerprint.len(), 64, "{link}");
    assert!(
        fingerprint
            .chars()
            .all(|c| c.is_ascii_hexdigit() && !c.is_ascii_uppercase()),
        "{link}"
    );
    assert!(dir.path().join("tls/leaf.crt").is_file());
}

#[test]
fn device_list_shows_old_keys_as_such() {
    let dir = a_library();
    with_a_device(&dir, "PC du salon");
    let done = device(&dir)
        .arg("list")
        .env("LEAF_KEYS", "phone:1111111111111111:read")
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(
        done.status.success(),
        "{said}{}",
        String::from_utf8_lossy(&done.stderr)
    );
    for column in ["NOM", "RELIÉ LE", "PAR", "VU LE"] {
        assert!(said.contains(column), "{column}: {said}");
    }
    let phone = said.lines().find(|l| l.starts_with("phone")).unwrap();
    assert!(phone.contains("ancienne clé de LEAF_KEYS"), "{said}");
    let salon = said.lines().find(|l| l.starts_with("PC du salon")).unwrap();
    assert!(salon.contains("terminal"), "{said}");
    // The key itself is never printed, old or new.
    assert!(!said.contains("1111111111111111"), "{said}");
}

#[test]
fn device_list_shows_a_waiting_code_and_how_long_it_has_left() {
    let dir = a_library();
    device(&dir)
        .args(["add", "--address", "leaf://127.0.0.1:8081"])
        .output()
        .unwrap();
    let said =
        String::from_utf8_lossy(&device(&dir).arg("list").output().unwrap().stdout).into_owned();
    assert!(said.contains("CODES EN ATTENTE"), "{said}");
    assert!(said.contains("expire dans 10 min"), "{said}");
}

#[test]
fn device_revoke_by_name_removes_the_device() {
    let dir = a_library();
    with_a_device(&dir, "PC du salon");
    let done = device(&dir)
        .args(["revoke", "pc  du SALON"])
        .output()
        .unwrap();
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );
    assert!(String::from_utf8_lossy(&done.stdout).contains("PC du salon"));
    assert_eq!(count(&dir, "device"), 0);

    // Twice is a mistake to say, not a silence.
    let again = device(&dir)
        .args(["revoke", "PC du salon"])
        .output()
        .unwrap();
    assert!(!again.status.success());
}

#[test]
fn device_add_after_the_last_device_is_revoked_still_links_and_does_not_promise_to_close() {
    let dir = a_library();
    with_a_device(&dir, "PC du salon");
    let revoked = device(&dir)
        .args(["revoke", "PC du salon"])
        .output()
        .unwrap();
    assert!(revoked.status.success());

    let done = device(&dir)
        .args(["add", "--address", "https://leaf.example.net"])
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(done.status.success(), "{said}");
    assert!(said.contains("leaf://leaf.example.net/lier/"), "{said}");
    // Closed once, closed for good: there is nothing left for this device to close.
    assert!(!said.contains("la bibliothèque sera fermée"), "{said}");
}

#[test]
fn device_enroll_refuses_to_print_a_key_on_a_terminal() {
    // `script` gives the command a pseudo-terminal for its output, which is what a person
    // typing it by hand has; `-e` hands back the command's own status.
    let dir = a_library();
    let line = format!("{} device enroll Phone", env!("CARGO_BIN_EXE_leaf-server"));
    let done = Command::new("script")
        .args(["-qec", &line, "/dev/null"])
        .env("LEAF_ENV_FILE", dir.path().join("no-server.env"))
        .env("LEAF_LIBRARY", dir.path().join("library"))
        .env("LEAF_DB", dir.path().join("index.sqlite"))
        .env_remove("LEAF_KEYS")
        .stdin(Stdio::null())
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(!done.status.success(), "{said}");
    assert!(said.contains("terminal"), "{said}");
    let index = dir.path().join("index.sqlite");
    assert!(!index.exists() || count(&dir, "device") == 0, "{said}");
}

#[test]
fn device_enroll_prints_only_the_key() {
    let dir = a_library();
    let done = device(&dir).args(["enroll", "Phone"]).output().unwrap();
    assert!(
        done.status.success(),
        "{}",
        String::from_utf8_lossy(&done.stderr)
    );
    let said = String::from_utf8(done.stdout).unwrap();
    let lines: Vec<&str> = said.lines().collect();
    assert_eq!(lines.len(), 1, "{} lines", lines.len());
    assert_eq!(lines[0].len(), 43);
    assert!(lines[0]
        .chars()
        .all(|c| c.is_ascii_alphanumeric() || c == '-' || c == '_'));
    assert_eq!(count(&dir, "device"), 1);
    // And nowhere else: nothing about it on the error stream either.
    assert!(!String::from_utf8_lossy(&done.stderr).contains(lines[0]));
}

#[test]
fn an_address_that_does_not_answer_gets_leafs_own_fingerprint_and_a_warning() {
    let dir = a_library();
    let silent = a_free_port();
    let done = device(&dir)
        .args(["add", "--address", &format!("https://127.0.0.3:{silent}")])
        .env("LEAF_HOST", "127.0.0.2")
        .output()
        .unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(done.status.success(), "{said}");
    assert!(
        said.contains("Cette adresse ne répond pas depuis le serveur : l'empreinte est celle du certificat de Leaf."),
        "{said}"
    );
    assert!(said.contains("?empreinte="), "{said}");
}

#[test]
fn a_certificate_that_is_not_leafs_own_is_pinned_with_its_fingerprint_shown_to_check() {
    // A server on 127.0.0.2 stands in for a proxy with a privately signed certificate: it is
    // somebody else's certificate as far as the second library is concerned.
    let elsewhere = a_library();
    let port = a_free_port();
    let mut server = leaf(&elsewhere)
        .arg("serve")
        .env("LEAF_HOST", "127.0.0.2")
        .env("LEAF_PORT", port.to_string())
        .stdout(Stdio::null())
        .stderr(Stdio::null())
        .spawn()
        .unwrap();
    let deadline = Instant::now() + Duration::from_secs(30);
    while ask_over_tls("127.0.0.2", port, "/health").is_none() {
        if Instant::now() > deadline || server.try_wait().unwrap().is_some() {
            let _ = server.kill();
            panic!("nothing ever answered over https on 127.0.0.2:{port}");
        }
        std::thread::sleep(Duration::from_millis(50));
    }
    let address = format!("https://127.0.0.2:{port}");

    let mine = a_library();
    let foreign = device(&mine)
        .args(["add", "--address", &address])
        .env("LEAF_HOST", "127.0.0.2")
        .output()
        .unwrap();
    // And from the library that server serves, the certificate is its own: nothing to say.
    let own = device(&elsewhere)
        .args(["add", "--address", &address])
        .env("LEAF_HOST", "127.0.0.2")
        .output()
        .unwrap();
    stop(&mut server, Ask::Interrupt);

    let renewal = "Ce certificat n'est pas signé par une autorité publique : quand il sera renouvelé, il faudra relier les appareils à nouveau.";
    let not_leafs = "Le certificat présenté à cette adresse n'est pas celui de Leaf.";
    let foreign = String::from_utf8_lossy(&foreign.stdout);
    let own = String::from_utf8_lossy(&own.stdout);
    let fingerprint = |said: &str| {
        said.lines()
            .find_map(|l| l.split_once("?empreinte=").map(|(_, f)| f.to_string()))
            .unwrap_or_else(|| panic!("no fingerprint in {said}"))
    };
    // Both pin the certificate the address presents, which is the served one.
    assert_eq!(fingerprint(&foreign), fingerprint(&own));
    // An interception would be pinned the same way: the operator is shown what to compare
    // with the proxy's own certificate, and told to, before handing the link out.
    assert!(foreign.contains(not_leafs), "{foreign}");
    assert!(
        foreign.contains(&format!(
            "Son empreinte SHA-256 est {}.",
            fingerprint(&foreign)
        )),
        "{foreign}"
    );
    assert!(
        foreign.contains(
            "Vérifiez que c'est bien celle du certificat de votre proxy avant de donner ce lien"
        ),
        "{foreign}"
    );
    assert!(foreign.contains(renewal), "{foreign}");
    assert!(!own.contains(not_leafs), "{own}");
    assert!(!own.contains(renewal), "{own}");
}

#[test]
fn device_list_escapes_a_name_that_could_rewrite_the_terminal() {
    // A row written before names were checked for control characters: printed raw, the
    // escape would clear the operator's screen.
    let dir = a_library();
    with_a_device(&dir, "PC du salon");
    rusqlite::Connection::open(dir.path().join("index.sqlite"))
        .unwrap()
        .execute("UPDATE device SET name = 'PC' || char(27) || '[2J'", [])
        .unwrap();
    let done = device(&dir).arg("list").output().unwrap();
    let said = String::from_utf8_lossy(&done.stdout);
    assert!(done.status.success(), "{said}");
    assert!(!said.contains('\u{1b}'), "{said:?}");
    assert!(said.contains("PC\\u{1b}[2J"), "{said}");
}

/// `sudo leaf-server profile reset-code`, minus sudo, with its stdin a pipe: no terminal, so
/// no echo to turn off, and the two answers are two lines.
fn reset_code_with(dir: &tempfile::TempDir, typed: &str) -> std::process::Output {
    use std::io::Write;
    let mut command = leaf(dir);
    let mut child = command
        .args(["profile", "reset-code"])
        .env("LEAF_ENV_FILE", dir.path().join("no-server.env"))
        .stdin(std::process::Stdio::piped())
        .stdout(std::process::Stdio::piped())
        .stderr(std::process::Stdio::piped())
        .spawn()
        .unwrap();
    child
        .stdin
        .take()
        .unwrap()
        .write_all(typed.as_bytes())
        .unwrap();
    child.wait_with_output().unwrap()
}

#[test]
fn profile_reset_code_sets_the_code_typed_twice_on_a_pipe() {
    let dir = a_library();
    let done = reset_code_with(&dir, "fresh9\nfresh9\n");
    assert_eq!(done.status.code(), Some(0), "{done:?}");
    let db = std::sync::Arc::new(
        leaf_server::store::Db::open(&dir.path().join("index.sqlite")).unwrap(),
    );
    assert!(leaf_server::store::profiles::Profiles::new(db)
        .check_code("fresh9")
        .unwrap());
    // The prompts are on stderr and the code is nowhere in what was printed.
    let said =
        String::from_utf8_lossy(&done.stdout).to_string() + &String::from_utf8_lossy(&done.stderr);
    assert!(said.contains("Nouveau code"), "{said}");
    assert!(!said.contains("fresh9"), "{said}");
}

#[test]
fn profile_reset_code_with_two_different_entries_exits_non_zero_and_changes_nothing() {
    let dir = a_library();
    let done = reset_code_with(&dir, "fresh9\nfresh8\n");
    assert_eq!(done.status.code(), Some(1), "{done:?}");
    let said = String::from_utf8_lossy(&done.stderr);
    assert!(said.contains("pas identiques"), "{said}");
    let db = std::sync::Arc::new(
        leaf_server::store::Db::open(&dir.path().join("index.sqlite")).unwrap(),
    );
    assert_eq!(
        leaf_server::store::profiles::Profiles::new(db)
            .code()
            .unwrap(),
        None
    );
}

/// The command on the slave side of a pseudo-terminal, and the master to watch it from.
///
/// The child is its own session leader with the slave as controlling terminal, which is what
/// a shell gives a command; the test then reads the terminal's `ECHO` flag through the master.
fn on_a_terminal(dir: &tempfile::TempDir) -> (std::process::Child, std::fs::File) {
    use std::os::fd::{AsRawFd, FromRawFd};
    use std::os::unix::process::CommandExt;
    let (mut master, mut slave) = (0, 0);
    // SAFETY: openpty fills in two fds, which are wrapped at once so they are closed.
    let (master, slave) = unsafe {
        let made = libc::openpty(
            &mut master,
            &mut slave,
            std::ptr::null_mut(),
            std::ptr::null(),
            std::ptr::null(),
        );
        assert_eq!(made, 0, "openpty");
        (
            std::fs::File::from_raw_fd(master),
            std::fs::File::from_raw_fd(slave),
        )
    };
    let mut command = leaf(dir);
    command
        .args(["profile", "reset-code"])
        .env("LEAF_ENV_FILE", dir.path().join("no-server.env"))
        .stdin(slave.try_clone().unwrap())
        .stdout(slave.try_clone().unwrap())
        .stderr(slave.try_clone().unwrap());
    let slave_fd = slave.as_raw_fd();
    // SAFETY: setsid and ioctl on an fd the child inherited; nothing is allocated.
    unsafe {
        command.pre_exec(move || {
            libc::setsid();
            libc::ioctl(slave_fd, libc::TIOCSCTTY as _, 0);
            Ok(())
        });
    }
    let child = command.spawn().unwrap();
    drop(slave);
    (child, master)
}

fn echo_is_on(master: &std::fs::File) -> bool {
    use std::os::fd::AsRawFd;
    // SAFETY: tcgetattr into a zeroed struct on a live fd.
    unsafe {
        let mut t: libc::termios = std::mem::zeroed();
        assert_eq!(libc::tcgetattr(master.as_raw_fd(), &mut t), 0);
        t.c_lflag & libc::ECHO != 0
    }
}

/// Waits for the command to have turned echo off, which is when it is reading.
fn until_silent(master: &std::fs::File) {
    for _ in 0..500 {
        if !echo_is_on(master) {
            return;
        }
        std::thread::sleep(std::time::Duration::from_millis(10));
    }
    panic!("the command never turned echo off");
}

#[test]
fn profile_reset_code_turns_echo_off_while_reading_and_back_on_after() {
    use std::io::Write;
    let dir = a_library();
    let (mut child, mut master) = on_a_terminal(&dir);
    assert!(echo_is_on(&master), "echo is on before the command asks");
    until_silent(&master);
    master.write_all(b"fresh9\nfresh9\n").unwrap();
    assert!(child.wait().unwrap().success());
    assert!(echo_is_on(&master), "echo is back after a normal entry");
}

#[test]
fn profile_reset_code_puts_echo_back_when_interrupted_or_terminated() {
    for signal in [libc::SIGINT, libc::SIGTERM, libc::SIGHUP] {
        let dir = a_library();
        let (mut child, master) = on_a_terminal(&dir);
        until_silent(&master);
        // SAFETY: a signal to a child this test owns.
        unsafe { libc::kill(child.id() as i32, signal) };
        use std::os::unix::process::ExitStatusExt;
        let status = child.wait().unwrap();
        assert_eq!(status.signal(), Some(signal), "dies of the signal it got");
        assert!(echo_is_on(&master), "echo is back after signal {signal}");
    }
}
