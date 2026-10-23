//! What the `device` commands settle before they touch the database or print a link: the
//! environment file they read under sudo, the account they become, and the link they compose.

use std::path::Path;

use leaf_server::cli::env_file::read_env_file;
use leaf_server::cli::{account_for, printable, Account};
use leaf_server::net::link::{
    listening_addresses, listening_among, relay_choice, Address, Interface, Link,
};

#[test]
fn an_env_file_written_by_the_installer_reads_back_unquoted() {
    // The form `quote_environment_value` in install.sh writes: double quotes around, and a
    // backslash before every `"` and `\` inside.
    let read = read_env_file(
        "# written by install.sh\n\
         \n\
         LEAF_LIBRARY=\"/srv/a \\\"b\\\"\"\n\
         LEAF_INBOX=\"C:\\\\inbox\"\n\
         LEAF_PORT=8081\n\
         LEAF_TLS_CERT=\n",
    );
    assert_eq!(read["LEAF_LIBRARY"], "/srv/a \"b\"");
    assert_eq!(read["LEAF_INBOX"], "C:\\inbox");
    assert_eq!(read["LEAF_PORT"], "8081");
    assert_eq!(read["LEAF_TLS_CERT"], "");
    assert_eq!(read.len(), 4, "{read:?}");
}

fn link(https: bool, host: &str, port: u16, fingerprint: Option<&str>) -> Link {
    Link {
        https,
        host: host.into(),
        port,
        code: "ABCD-EFGH".into(),
        fingerprint: fingerprint.map(str::to_string),
    }
}

#[test]
fn a_link_on_443_names_no_port() {
    assert_eq!(
        link(true, "leaf.example.net", 443, None).to_string(),
        "leaf://leaf.example.net/lier/ABCDEFGH"
    );
    // 443 is only implied by https: on the loopback a link means http, and says its port.
    assert_eq!(
        link(false, "127.0.0.1", 443, None).to_string(),
        "leaf://127.0.0.1:443/lier/ABCDEFGH"
    );
}

#[test]
fn a_link_to_an_ipv6_address_puts_it_in_brackets() {
    assert_eq!(
        link(true, "fd00::2", 8081, None).to_string(),
        "leaf://[fd00::2]:8081/lier/ABCDEFGH"
    );
}

#[test]
fn a_link_carries_the_fingerprint_only_when_there_is_one() {
    let fingerprint = "ab".repeat(32);
    assert_eq!(
        link(true, "192.168.1.20", 8081, Some(&fingerprint)).to_string(),
        format!("leaf://192.168.1.20:8081/lier/ABCDEFGH?empreinte={fingerprint}")
    );
    assert!(!link(true, "192.168.1.20", 8081, None)
        .to_string()
        .contains('?'));
}

#[test]
fn on_every_interface_each_address_gets_a_label() {
    let found = listening_addresses("0.0.0.0");
    for (label, address) in &found {
        assert!(!address.is_loopback(), "{found:?}");
        assert!(address.is_ipv4(), "0.0.0.0 listens on IPv4 only: {found:?}");
        assert!(!label.is_empty(), "{found:?}");
    }
    // A precise host is the one address, whatever the machine has.
    let one = listening_addresses("192.168.1.20");
    assert_eq!(one.len(), 1, "{one:?}");
    assert_eq!(one[0].0, "réseau local");
    assert_eq!(listening_addresses("100.101.102.103")[0].0, "Tailscale");
}

#[test]
fn a_relay_never_points_at_a_container_or_virtual_machine_bridge() {
    // On 0.0.0.0 with Docker, docker0 is a private address like the home network's, came
    // first among them, and was the address a phone was handed.
    let one = |name: &str, ip: &str| Interface {
        name: name.into(),
        ip: ip.parse().unwrap(),
    };
    let interfaces = [
        one("lo", "127.0.0.1"),
        one("docker0", "172.17.0.1"),
        one("br-4f2a9c", "172.18.0.1"),
        one("virbr0", "192.168.122.1"),
        one("veth1234", "10.0.3.1"),
        one("enp3s0", "192.168.1.20"),
    ];
    let found = listening_among("0.0.0.0", &interfaces);
    // `device add` still lists them all, each under a name that tells them apart.
    assert_eq!(5, found.len(), "{found:?}");
    let chosen = relay_choice(&found).expect("an address");
    assert_eq!("192.168.1.20", chosen.ip.to_string());

    let only_bridges = listening_among("0.0.0.0", &interfaces[..3]);
    assert!(relay_choice(&only_bridges).is_none(), "{only_bridges:?}");
}

#[test]
fn an_address_is_read_from_the_forms_a_person_types() {
    let read = |typed: &str| Address::parse(typed).unwrap();
    assert_eq!(
        read("https://leaf.example.net"),
        Address {
            https: true,
            host: "leaf.example.net".into(),
            port: 443
        }
    );
    assert_eq!(read("https://leaf.example.net:8443/").port, 8443);
    assert_eq!(read("leaf://[fd00::2]:8081").host, "fd00::2");
    // The SSH tunnel of the spec: the loopback, where a link means http.
    let tunnel = read("leaf://127.0.0.1:8081");
    assert!(!tunnel.https);
    assert_eq!(tunnel.port, 8081);
    assert!(!read("http://localhost:8081").https);
}

#[test]
fn plain_http_past_the_loopback_is_refused_as_an_address() {
    let refused = Address::parse("http://192.168.1.20:8081").unwrap_err();
    assert!(refused.to_string().contains("https"), "{refused}");
    assert!(Address::parse("ftp://leaf.example.net").is_err());
    assert!(Address::parse("https://").is_err());
}

#[test]
fn a_command_run_without_root_opens_the_database_as_it_is() {
    assert_eq!(
        account_for(1000, Path::new("/home/me/leaf"), (1000, 1000)).unwrap(),
        Account::AsItIs
    );
}

#[test]
fn as_root_it_becomes_the_owner_of_the_database_folder() {
    assert_eq!(
        account_for(0, Path::new("/var/lib/leaf"), (998, 997)).unwrap(),
        Account::Become { uid: 998, gid: 997 }
    );
}

#[test]
fn as_root_a_folder_root_owns_is_refused_unless_it_is_the_state_directory() {
    // systemd hands /var/lib/leaf back to the service at its next start, files and all.
    assert_eq!(
        account_for(0, Path::new("/var/lib/leaf"), (0, 0)).unwrap(),
        Account::AsItIs
    );
    // Anywhere else nothing would, and the service could not write its own index again.
    let refused = account_for(0, Path::new("/srv/leaf"), (0, 0)).unwrap_err();
    assert!(refused.to_string().contains("/srv/leaf"), "{refused}");
}

#[test]
fn an_unbracketed_ipv6_address_is_refused_rather_than_split_at_its_last_colon() {
    // `https://fd00::2` read as host `fd00:` and port 2 — a link to nowhere, said nowhere.
    let refused = Address::parse("https://fd00::2").unwrap_err();
    assert!(refused.to_string().contains("[fd00::2]"), "{refused}");
    assert!(Address::parse("leaf://fd00::2:8081").is_err());
    assert_eq!(Address::parse("https://[fd00::2]").unwrap().host, "fd00::2");
}

#[test]
fn a_value_printed_to_the_terminal_has_its_control_characters_escaped() {
    assert_eq!(printable("PC du salon"), "PC du salon");
    assert_eq!(printable("PC\u{1b}[2J"), "PC\\u{1b}[2J");
    assert_eq!(printable("a\u{7}\u{9b}x"), "a\\u{7}\\u{9b}x");
    assert!(!printable("\u{1b}]0;owned\u{7}")
        .chars()
        .any(char::is_control));
}

/// An output that takes nothing: the installer's pipe, closed before the key arrived.
struct Closed;

impl std::io::Write for Closed {
    fn write(&mut self, _: &[u8]) -> std::io::Result<usize> {
        Err(std::io::ErrorKind::BrokenPipe.into())
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Err(std::io::ErrorKind::BrokenPipe.into())
    }
}

#[test]
fn a_key_that_cannot_be_written_takes_its_device_back_with_it() {
    use leaf_server::cli::device::{enroll, Console};
    use leaf_server::config::Config;
    use leaf_server::store::devices::Devices;

    let dir = tempfile::tempdir().unwrap();
    let db = dir.path().join("index.sqlite");
    let config = Config::read(|name| (name == "LEAF_DB").then(|| db.display().to_string()));
    let mut err = Vec::new();
    let failed = enroll(
        &config,
        "Phone",
        &mut Console {
            out: &mut Closed,
            err: &mut err,
            terminal: false,
        },
    );
    assert!(failed.is_err());
    let devices = Devices::new(std::sync::Arc::new(
        leaf_server::store::Db::open(&db).unwrap(),
    ));
    let (linked, _) = devices.list(0).unwrap();
    assert!(linked.is_empty(), "a device nobody holds the key of");

    // The same name is free again, and a working output gets the key.
    let mut out = Vec::new();
    enroll(
        &config,
        "Phone",
        &mut Console {
            out: &mut out,
            err: &mut err,
            terminal: false,
        },
    )
    .unwrap();
    assert_eq!(String::from_utf8(out).unwrap().trim_end().len(), 43);
    assert_eq!(1, devices.list(0).unwrap().0.len());
}
