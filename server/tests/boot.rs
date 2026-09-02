//! Everything the server settles before it listens.
//!
//! These were lines inside `main`, where nothing could reach them. Each is a decision the
//! next edit could get wrong in silence — a default that stops defaulting, a refusal that
//! stops refusing.

use std::path::{Path, PathBuf};

use leaf_server::api::keys::Keys;
use leaf_server::boot::{
    cache_ceiling, jpeg_quality, refuse_an_open_library, split_volumes, tls_hosts, Invocation,
    Outcome,
};

/// Parses a line that is expected to run rather than to print usage or be refused.
fn asked(args: &[&str]) -> Invocation {
    match Invocation::of(args.iter().copied().map(String::from)).unwrap() {
        Outcome::Run(it) => it,
        Outcome::Usage(usage) => panic!("expected a run, got usage:\n{usage}"),
    }
}

/// The message a refused line was refused with.
fn refused(args: &[&str]) -> String {
    Invocation::of(args.iter().copied().map(String::from))
        .unwrap_err()
        .to_string()
}

#[test]
fn a_server_started_with_no_argument_serves() {
    let it = asked(&[]);
    assert_eq!(it.command, "serve");
    assert!(!it.is_scan());
    assert!(it.requested.is_empty());
    assert!(it.all_dimensions);
}

#[test]
fn scan_is_the_other_mode_and_says_so() {
    assert!(asked(&["scan"]).is_scan());
    assert!(!asked(&["serve"]).is_scan());
}

#[test]
fn the_roots_are_the_ones_named() {
    let it = asked(&["scan", "/a", "/b"]);
    assert_eq!(it.requested, vec![PathBuf::from("/a"), PathBuf::from("/b")]);
    assert_eq!(
        it.roots(Path::new("/library")),
        vec![PathBuf::from("/a"), PathBuf::from("/b")]
    );
}

#[test]
fn naming_none_means_the_configured_library() {
    assert_eq!(
        asked(&["scan"]).roots(Path::new("/srv/leaf/library")),
        vec![PathBuf::from("/srv/leaf/library")]
    );
}

#[test]
fn an_option_is_never_read_as_a_root() {
    // `leaf scan --no-dimensions` used to scan a folder called "--no-dimensions" in every
    // version of this that filtered the arguments in only one of the two places.
    let it = asked(&["scan", "--no-dimensions"]);
    assert!(it.requested.is_empty());
    assert_eq!(
        it.roots(Path::new("/library")),
        vec![PathBuf::from("/library")]
    );
}

#[test]
fn the_dimensions_are_measured_unless_the_option_says_not_to() {
    assert!(asked(&["scan", "/a"]).all_dimensions);
    assert!(!asked(&["scan", "/a", "--no-dimensions"]).all_dimensions);
    // Wherever it sits: an option is an option, not a positional.
    assert!(!asked(&["serve", "--no-dimensions", "/a"]).all_dimensions);
}

#[test]
fn an_unknown_option_is_not_a_root_either() {
    // `leaf-server scan --quickly` used to run with `requested` silently empty, as though
    // nothing had been asked for — the option was dropped, not read as a root, but nothing
    // said it had been dropped either. It is refused now, so an unread option can no longer
    // be mistaken for "no roots were named."
    let message = refused(&["scan", "--quickly"]);
    assert!(message.contains("--quickly"), "{message}");
}

#[test]
fn an_unknown_command_is_refused_rather_than_served() {
    // `leaf-server sacn` used to serve, silently, because any first argument became
    // `command` and only `"scan"` was ever compared against it.
    let message = refused(&["sacn"]);
    assert!(message.contains("sacn"), "{message}");
    assert!(message.contains("scan"), "{message}");
    assert!(message.contains("serve"), "{message}");
}

#[test]
fn an_unknown_option_is_refused_and_named() {
    let message = refused(&["scan", "--upside-down"]);
    assert!(message.contains("--upside-down"), "{message}");
}

#[test]
fn a_near_miss_option_is_refused_rather_than_silently_ignored() {
    // The exact typo the old filter swallowed: it kept only the literal string
    // "--no-dimensions", so "--no-dimension" — singular — passed through as though it meant
    // nothing, and dimensions were measured anyway with nothing said about why.
    let message = refused(&["scan", "--no-dimension"]);
    assert!(message.contains("--no-dimension"), "{message}");
}

#[test]
fn help_is_a_question_not_a_mistake() {
    // Asked for the same way whether it comes before or after the command word — both
    // `leaf-server --help` and `leaf-server scan --help` served or scanned instead of
    // answering, in the deployment that found this.
    for args in [
        vec!["--help"],
        vec!["scan", "--help"],
        vec!["serve", "--help"],
    ] {
        match Invocation::of(args.iter().copied().map(String::from)).unwrap() {
            Outcome::Usage(usage) => {
                assert!(usage.contains("scan"), "{usage}");
                assert!(usage.contains("serve"), "{usage}");
            }
            Outcome::Run(_) => panic!("--help must not run anything, args: {args:?}"),
        }
    }
}

#[test]
fn the_loopback_needs_no_key() {
    let open = Keys::parse(None).unwrap();
    assert!(open.open());
    assert!(refuse_an_open_library("127.0.0.1", &open).is_ok());
    assert!(refuse_an_open_library("localhost", &open).is_ok());
}

#[test]
fn binding_wider_with_no_key_is_refused_rather_than_warned_about() {
    let open = Keys::parse(None).unwrap();
    for host in ["0.0.0.0", "192.168.1.20", "::"] {
        let refused = refuse_an_open_library(host, &open).unwrap_err().to_string();
        assert!(refused.contains(host), "{refused}");
        assert!(refused.contains("Set LEAF_KEYS"), "{refused}");
    }
}

#[test]
fn binding_wider_is_allowed_once_a_key_exists() {
    let keys = Keys::parse(Some("desktop:un-secret-assez-long:read")).unwrap();
    assert!(!keys.open());
    assert!(refuse_an_open_library("0.0.0.0", &keys).is_ok());
}

#[test]
fn the_jpeg_quality_is_a_dial_that_stops_at_its_ends() {
    assert_eq!(jpeg_quality(None), 85);
    assert_eq!(jpeg_quality(Some("0.85")), 85);
    assert_eq!(jpeg_quality(Some("1.0")), 100);
    // Past either end, it stops at the end rather than refusing.
    assert_eq!(jpeg_quality(Some("0.1")), 40);
    assert_eq!(jpeg_quality(Some("9")), 100);
}

#[test]
fn a_quality_that_is_not_a_number_falls_back() {
    assert_eq!(jpeg_quality(Some("haute")), 85);
    assert_eq!(jpeg_quality(Some("")), 85);
}

#[test]
fn the_cache_ceiling_is_read_in_megabytes() {
    assert_eq!(cache_ceiling(None), 4096 * 1024 * 1024);
    assert_eq!(cache_ceiling(Some("1")), 1024 * 1024);
    assert_eq!(cache_ceiling(Some("0")), 0);
    assert_eq!(cache_ceiling(Some("beaucoup")), 4096 * 1024 * 1024);
}

#[test]
fn a_certificate_names_the_host_it_was_bound_to_unless_told_otherwise() {
    assert_eq!(tls_hosts(&[], "leaf.maison"), vec!["leaf.maison"]);
    let named = vec!["leaf.maison".to_string(), "192.168.1.20".to_string()];
    assert_eq!(tls_hosts(&named, "0.0.0.0"), named);
}

#[test]
fn two_folders_on_one_filesystem_are_not_split() {
    let dir = tempfile::tempdir().unwrap();
    let library = dir.path().join("library");
    let inbox = dir.path().join("inbox");
    std::fs::create_dir_all(&library).unwrap();
    std::fs::create_dir_all(&inbox).unwrap();
    assert!(!split_volumes(&library, &inbox));
}

#[test]
fn a_folder_that_is_not_there_yet_is_no_answer_rather_than_a_wrong_one() {
    // Warning at every first start, before the inbox has been created, would be noise.
    let dir = tempfile::tempdir().unwrap();
    assert!(!split_volumes(dir.path(), &dir.path().join("not yet")));
    assert!(!split_volumes(Path::new("/no/such/library"), dir.path()));
}
