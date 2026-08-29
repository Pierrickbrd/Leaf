//! Every setting, and the rule it falls back on.
//!
//! Read from a map rather than from the process environment: `set_var` is global to the
//! process and the test harness runs threads, so a test that set `LEAF_PORT` would be
//! setting it for whatever else happened to be running. `Config::read` takes its lookup as
//! an argument for exactly this reason.

use std::collections::HashMap;
use std::path::PathBuf;

use leaf_server::config::Config;

fn given(pairs: &[(&str, &str)]) -> Config {
    let set: HashMap<String, String> = pairs
        .iter()
        .map(|(k, v)| ((*k).to_string(), (*v).to_string()))
        .collect();
    Config::read(move |name| set.get(name).cloned())
}

fn nothing_set() -> Config {
    given(&[])
}

#[test]
fn an_unset_server_reads_a_library_beside_itself() {
    let it = nothing_set();
    assert_eq!(it.library, PathBuf::from("library"));
    assert_eq!(it.db, PathBuf::from("data/leaf.sqlite"));
    assert_eq!(it.host, "127.0.0.1");
    assert_eq!(it.port, 8081);
}

#[test]
fn the_inbox_and_the_cache_sit_beside_the_library() {
    // The inbox has to share the library's filesystem: committing an import is a rename.
    // Defaulting it beside the library is what makes that true without anybody saying so.
    let it = given(&[("LEAF_LIBRARY", "/srv/leaf/library")]);
    assert_eq!(it.inbox, PathBuf::from("/srv/leaf/inbox"));
    assert_eq!(it.cache, PathBuf::from("/srv/leaf/cache"));
}

#[test]
fn a_library_at_the_root_of_a_relative_path_still_has_a_beside() {
    let it = given(&[("LEAF_LIBRARY", "library")]);
    assert_eq!(it.inbox, PathBuf::from("inbox"));
    assert_eq!(it.cache, PathBuf::from("cache"));
}

#[test]
fn each_folder_can_be_named_instead() {
    let it = given(&[
        ("LEAF_LIBRARY", "/l"),
        ("LEAF_INBOX", "/i"),
        ("LEAF_CACHE", "/c"),
        ("LEAF_DB", "/d/leaf.sqlite"),
        ("LEAF_DROP", "/drop"),
    ]);
    assert_eq!(it.inbox, PathBuf::from("/i"));
    assert_eq!(it.cache, PathBuf::from("/c"));
    assert_eq!(it.db, PathBuf::from("/d/leaf.sqlite"));
    assert_eq!(it.drop, Some(PathBuf::from("/drop")));
}

#[test]
fn a_variable_set_to_nothing_counts_as_unset() {
    // A unit file that writes `Environment=LEAF_DROP=` means "no drop folder", not "a
    // folder whose name is the empty string" — which would resolve to the process's
    // working directory and quietly file uploads into it.
    let it = given(&[("LEAF_LIBRARY", ""), ("LEAF_DROP", ""), ("LEAF_HOST", "")]);
    assert_eq!(it.library, PathBuf::from("library"));
    assert_eq!(it.drop, None);
    assert_eq!(it.host, "127.0.0.1");
}

#[test]
fn nothing_is_dropped_into_unless_a_folder_is_named() {
    assert_eq!(nothing_set().drop, None);
}

#[test]
fn the_proxy_is_trusted_only_when_it_is_said_in_so_many_words() {
    for word in ["1", "true", "yes", "TRUE", "Yes"] {
        assert!(
            given(&[("LEAF_TRUST_PROXY", word)]).trust_proxy,
            "{word} should turn it on"
        );
    }
    for word in ["0", "false", "no", "", "maybe"] {
        assert!(
            !given(&[("LEAF_TRUST_PROXY", word)]).trust_proxy,
            "{word} must not"
        );
    }
    assert!(!nothing_set().trust_proxy);
}

#[test]
fn the_upload_ceiling_is_read_in_megabytes() {
    assert_eq!(
        given(&[("LEAF_MAX_UPLOAD_MB", "1")]).max_upload_bytes,
        1024 * 1024
    );
    assert_eq!(nothing_set().max_upload_bytes, 2048 * 1024 * 1024);
}

#[test]
fn a_ceiling_that_is_not_a_number_falls_back_rather_than_becoming_zero() {
    // Zero would refuse every upload, which is a far stranger way to answer a typo than
    // simply behaving as though nobody had set it.
    assert_eq!(
        given(&[("LEAF_MAX_UPLOAD_MB", "beaucoup")]).max_upload_bytes,
        2048 * 1024 * 1024
    );
}

#[test]
fn a_port_that_is_not_a_number_falls_back_too() {
    assert_eq!(given(&[("LEAF_PORT", "8443")]).port, 8443);
    assert_eq!(given(&[("LEAF_PORT", "not a port")]).port, 8081);
    assert_eq!(given(&[("LEAF_PORT", "70000")]).port, 8081);
}

#[test]
fn the_key_sits_beside_the_certificate_unless_it_is_named() {
    // Two paths for one thing is two chances to configure half of it.
    let beside = given(&[("LEAF_TLS_CERT", "/etc/leaf/leaf.pem")]);
    assert_eq!(
        beside.tls_certificate,
        Some(PathBuf::from("/etc/leaf/leaf.pem"))
    );
    assert_eq!(beside.tls_key, Some(PathBuf::from("/etc/leaf/leaf.key")));

    let named = given(&[
        ("LEAF_TLS_CERT", "/etc/leaf/leaf.pem"),
        ("LEAF_TLS_KEY", "/keys/k"),
    ]);
    assert_eq!(named.tls_key, Some(PathBuf::from("/keys/k")));
}

#[test]
fn no_certificate_means_no_key_either() {
    let it = nothing_set();
    assert_eq!(it.tls_certificate, None);
    assert_eq!(it.tls_key, None);
    assert!(it.tls_hosts.is_empty());
}

#[test]
fn the_tls_hosts_are_a_list_and_the_spaces_around_them_do_not_count() {
    let it = given(&[("LEAF_TLS_HOSTS", " leaf.maison , 192.168.1.20 ,, ")]);
    assert_eq!(it.tls_hosts, vec!["leaf.maison", "192.168.1.20"]);
}

#[test]
fn the_environment_is_the_only_place_it_looks() {
    // `from_env` is `read` with the process environment behind it, and nothing else.
    let it = Config::from_env();
    assert!(!it.host.is_empty());
    assert!(it.port > 0);
}
