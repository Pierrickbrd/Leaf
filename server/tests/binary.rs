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
fn a_free_port() -> u16 {
    let listener = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    listener.local_addr().unwrap().port()
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

#[test]
fn binding_past_the_loopback_with_no_key_is_refused_at_startup() {
    let dir = a_library();
    let done = leaf(&dir)
        .arg("serve")
        .env("LEAF_HOST", "0.0.0.0")
        .env("LEAF_PORT", a_free_port().to_string())
        .output()
        .unwrap();
    assert!(!done.status.success());
    let complained = String::from_utf8_lossy(&done.stderr);
    assert!(complained.contains("LEAF_KEYS"), "{complained}");
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
    stop(&mut server, Ask::Interrupt);
    assert!(listening, "nothing ever listened on the TLS port");
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
