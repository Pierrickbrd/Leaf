//! Devices: the one-time codes, the key each one is handed in exchange, and what can be
//! done to a device afterwards.
//!
//! Every time is in milliseconds, like every other time the index keeps.

use std::sync::{Arc, Barrier};

use leaf_server::store::devices::{Creator, Devices, Linked, Refusal, CODE_LIFETIME};
use leaf_server::store::Db;

const NOW: i64 = 1_790_000_000_000;

struct Fixture {
    dir: tempfile::TempDir,
    db: Arc<Db>,
    devices: Devices,
}

impl Fixture {
    fn new() -> Self {
        let dir = tempfile::tempdir().expect("a directory");
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));
        let devices = Devices::new(db.clone());
        Fixture { dir, db, devices }
    }

    fn code(&self, by: Creator) -> String {
        self.devices.issue(by, NOW).expect("issuing").code
    }

    fn link(&self, name: &str) -> Linked {
        let code = self.code(Creator::Terminal);
        self.devices
            .exchange(&code, name, NOW)
            .expect("exchanging")
            .expect("a linked device")
    }

    fn count(&self, table: &str) -> i64 {
        self.db
            .read(|cx| {
                cx.query_one(&format!("SELECT COUNT(*) FROM {table}"), [], |r| r.get(0))
                    .map(|n| n.unwrap_or(0))
            })
            .expect("counting")
    }
}

#[test]
fn an_issued_code_reads_as_two_groups_of_four_unambiguous_letters() {
    let fx = Fixture::new();
    for _ in 0..50 {
        let code = fx.code(Creator::Terminal);
        assert_eq!(code.len(), 9, "{code}");
        assert_eq!(code.as_bytes()[4], b'-', "{code}");
        assert!(!code.chars().any(|c| "01IOL".contains(c)), "{code}");
        assert!(
            code.chars()
                .all(|c| c == '-' || c.is_ascii_uppercase() || c.is_ascii_digit()),
            "{code}"
        );
    }
}

#[test]
fn a_code_links_one_device_and_hands_its_key_over_once() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    let linked = fx
        .devices
        .exchange(&code, "PC du salon", NOW)
        .unwrap()
        .expect("linked");
    assert_eq!(linked.key.len(), 43);
    assert_eq!(linked.device.name, "PC du salon");
    assert_eq!(linked.device.profile_id, "default");

    let (known, profile) = fx
        .devices
        .recognise(&linked.key, NOW)
        .unwrap()
        .expect("recognised");
    assert_eq!(known.id, linked.device.id);
    assert_eq!(known.name, "PC du salon");
    assert_eq!(profile.id, "default", "and the profile it reads as");
    assert!(fx.devices.recognise("not a key", NOW).unwrap().is_none());
    assert!(fx.devices.recognise("", NOW).unwrap().is_none());
}

#[test]
fn the_key_itself_is_never_stored() {
    let fx = Fixture::new();
    let linked = fx.link("PC du salon");

    let digest: Vec<u8> = fx
        .db
        .read(|cx| {
            cx.query_one("SELECT key_digest FROM device", [], |r| r.get(0))
                .map(Option::unwrap)
        })
        .unwrap();
    assert_ne!(digest, linked.key.as_bytes());
    assert_eq!(digest.len(), 32);

    // Every column of every row of both tables, read as text and as bytes.
    for table in ["device", "link_code"] {
        let cells: Vec<Vec<u8>> = fx
            .db
            .read(|cx| {
                let mut statement = cx.raw().prepare(&format!("SELECT * FROM {table}"))?;
                let width = statement.column_count();
                let mut rows = statement.query([])?;
                let mut cells = Vec::new();
                while let Some(row) = rows.next()? {
                    for i in 0..width {
                        use rusqlite::types::ValueRef;
                        match row.get_ref(i)? {
                            ValueRef::Text(t) | ValueRef::Blob(t) => cells.push(t.to_vec()),
                            _ => {}
                        }
                    }
                }
                Ok(cells)
            })
            .unwrap();
        let key = linked.key.as_bytes();
        assert!(
            !cells.iter().any(|c| c.windows(key.len()).any(|w| w == key)),
            "the key is in {table}"
        );
    }
}

#[test]
fn a_code_is_read_whatever_its_case_dashes_and_spaces() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    let typed = format!(
        " {} {}\n",
        code[..4].to_lowercase(),
        code[5..].to_lowercase()
    );
    assert!(fx
        .devices
        .exchange(&typed, "PC du salon", NOW)
        .unwrap()
        .is_ok());
}

#[test]
fn a_code_served_once_is_not_valid_a_second_time() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    assert!(fx
        .devices
        .exchange(&code, "PC du salon", NOW)
        .unwrap()
        .is_ok());
    assert_eq!(
        fx.devices.exchange(&code, "Tablette", NOW).unwrap().err(),
        Some(Refusal::CodeNotValid)
    );
}

#[test]
fn the_same_code_exchanged_twice_at_once_links_one_device() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    let devices = Arc::new(Devices::new(fx.db.clone()));
    let barrier = Arc::new(Barrier::new(2));
    let handles: Vec<_> = ["PC du salon", "Tablette"]
        .into_iter()
        .map(|name| {
            let (devices, barrier, code) = (devices.clone(), barrier.clone(), code.clone());
            std::thread::spawn(move || {
                barrier.wait();
                devices.exchange(&code, name, NOW).unwrap()
            })
        })
        .collect();
    let outcomes: Vec<_> = handles.into_iter().map(|h| h.join().unwrap()).collect();

    assert_eq!(outcomes.iter().filter(|o| o.is_ok()).count(), 1);
    assert_eq!(
        outcomes
            .iter()
            .filter(|o| matches!(o, Err(Refusal::CodeNotValid)))
            .count(),
        1
    );
    assert_eq!(fx.count("device"), 1);
}

#[test]
fn a_code_is_not_valid_after_ten_minutes() {
    let fx = Fixture::new();
    assert_eq!(CODE_LIFETIME, 600_000);
    let late = fx.code(Creator::Terminal);
    assert_eq!(
        fx.devices
            .exchange(&late, "PC du salon", NOW + 600_000)
            .unwrap()
            .err(),
        Some(Refusal::CodeNotValid)
    );
    let just_in_time = fx.code(Creator::Terminal);
    assert!(fx
        .devices
        .exchange(&just_in_time, "PC du salon", NOW + 599_999)
        .unwrap()
        .is_ok());
}

#[test]
fn a_name_taken_by_another_device_is_refused_and_the_code_still_works() {
    let fx = Fixture::new();
    fx.link("PC du salon");
    let code = fx.code(Creator::Terminal);
    assert_eq!(
        fx.devices
            .exchange(&code, "  pc  DU salon ", NOW)
            .unwrap()
            .err(),
        Some(Refusal::NameTaken("PC du salon".into()))
    );
    assert!(fx.devices.exchange(&code, "Tablette", NOW).unwrap().is_ok());
}

#[test]
fn a_name_of_spaces_only_is_refused_as_empty() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    assert_eq!(
        fx.devices.exchange(&code, "   \t ", NOW).unwrap().err(),
        Some(Refusal::NameEmpty)
    );
    assert!(fx.devices.exchange(&code, "Tablette", NOW).unwrap().is_ok());
}

#[test]
fn a_name_longer_than_forty_characters_is_refused() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    assert_eq!(
        fx.devices
            .exchange(&code, &"é".repeat(41), NOW)
            .unwrap()
            .err(),
        Some(Refusal::NameTooLong)
    );
    let linked = fx
        .devices
        .exchange(&code, &format!("  {}  ", "é".repeat(40)), NOW)
        .unwrap()
        .expect("forty characters, eighty bytes");
    assert_eq!(linked.device.name, "é".repeat(40));
}

#[test]
fn a_name_with_a_control_character_is_refused() {
    // A name is printed by `device list` in the operator's terminal: an escape sequence in
    // it would rewrite that terminal. ESC, BEL and the C1 introducer CSI, each on its own.
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    for name in ["PC\u{1b}[2J", "a\u{7}", "\u{9b}x", "Tab\nlette"] {
        assert_eq!(
            fx.devices.exchange(&code, name, NOW).unwrap().err(),
            Some(Refusal::NameInvalid),
            "{name:?}"
        );
        assert_eq!(
            fx.devices.enroll(name, NOW).unwrap().err(),
            Some(Refusal::NameInvalid),
            "{name:?}"
        );
    }
    let linked = fx
        .devices
        .exchange(&code, "Tablette", NOW)
        .unwrap()
        .unwrap();
    assert_eq!(
        fx.devices
            .rename(&linked.device.id, "PC\u{1b}[2J")
            .unwrap()
            .err(),
        Some(Refusal::NameInvalid)
    );
}

#[test]
fn a_revoked_device_is_not_recognised_at_the_next_request() {
    let fx = Fixture::new();
    let linked = fx.link("PC du salon");
    assert!(fx.devices.recognise(&linked.key, NOW).unwrap().is_some());

    // The revocation comes from elsewhere — another connection to the same file, as the
    // `device revoke` command would be — and this instance is not told.
    let other = Devices::new(Arc::new(
        Db::open(&fx.dir.path().join("index.sqlite")).unwrap(),
    ));
    assert_eq!(
        other.revoke("pc du salon").unwrap(),
        Ok("PC du salon".to_string())
    );
    assert!(fx
        .devices
        .recognise(&linked.key, NOW + 1)
        .unwrap()
        .is_none());
}

#[test]
fn revoking_a_device_leaves_the_devices_it_linked() {
    let fx = Fixture::new();
    let a = fx.link("PC du salon");
    let code = fx.code(Creator::Device(&a.device.id));
    let b = fx
        .devices
        .exchange(&code, "Tablette", NOW)
        .unwrap()
        .unwrap();
    assert!(fx.devices.revoke("PC du salon").unwrap().is_ok());
    assert!(fx.devices.recognise(&a.key, NOW).unwrap().is_none());
    assert!(fx.devices.recognise(&b.key, NOW).unwrap().is_some());
}

#[test]
fn revoking_a_device_cancels_the_codes_it_had_not_served_yet() {
    let fx = Fixture::new();
    let a = fx.link("PC du salon");
    let code = fx.code(Creator::Device(&a.device.id));
    assert!(fx.devices.revoke("PC du salon").unwrap().is_ok());
    assert_eq!(
        fx.devices.exchange(&code, "Tablette", NOW).unwrap().err(),
        Some(Refusal::CodeNotValid)
    );
    assert!(fx.devices.list(NOW).unwrap().1.is_empty());
}

#[test]
fn a_code_served_to_a_revoked_device_does_not_link_again() {
    // `served_device` is `ON DELETE SET NULL`: a revocation that only deleted the device
    // would have put its code back to waiting, valid again for whatever was left of its ten
    // minutes.
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    fx.devices
        .exchange(&code, "PC du salon", NOW)
        .unwrap()
        .unwrap();
    assert!(fx.devices.revoke("PC du salon").unwrap().is_ok());
    assert_eq!(
        fx.devices
            .exchange(&code, "Tablette", NOW + 1)
            .unwrap()
            .err(),
        Some(Refusal::CodeNotValid)
    );
}

#[test]
fn a_pending_code_can_be_revoked_by_the_code_itself() {
    let fx = Fixture::new();
    let code = fx.code(Creator::Terminal);
    assert_eq!(fx.devices.list(NOW).unwrap().1.len(), 1);
    let typed = code.to_lowercase().replace('-', " ");
    assert_eq!(fx.devices.revoke(&typed).unwrap(), Ok(code.clone()));
    assert!(fx.devices.list(NOW).unwrap().1.is_empty());
    assert_eq!(
        fx.devices.exchange(&code, "Tablette", NOW).unwrap().err(),
        Some(Refusal::CodeNotValid)
    );
    assert_eq!(
        fx.devices.revoke("Personne").unwrap(),
        Err(Refusal::NoSuchDevice)
    );
}

#[test]
fn renaming_follows_the_same_rule_as_linking() {
    let fx = Fixture::new();
    let salon = fx.link("PC du salon");
    fx.link("Tablette");
    let id = &salon.device.id;
    assert_eq!(
        fx.devices.rename(id, " tablette ").unwrap().err(),
        Some(Refusal::NameTaken("Tablette".into()))
    );
    assert_eq!(
        fx.devices.rename(id, "  ").unwrap().err(),
        Some(Refusal::NameEmpty)
    );
    assert_eq!(
        fx.devices.rename(id, &"x".repeat(41)).unwrap().err(),
        Some(Refusal::NameTooLong)
    );
    let renamed = fx.devices.rename(id, "pc du salon").unwrap().unwrap();
    assert_eq!(renamed.name, "pc du salon");
    assert_eq!(
        fx.devices.rename("nobody", "Liseuse").unwrap().err(),
        Some(Refusal::NoSuchDevice)
    );
}

#[test]
fn enrolling_follows_the_same_name_rule_as_linking() {
    let fx = Fixture::new();
    fx.link("PC du salon");
    assert_eq!(
        fx.devices.enroll("pc du SALON", NOW).unwrap().err(),
        Some(Refusal::NameTaken("PC du salon".into()))
    );
    assert_eq!(
        fx.devices.enroll(" ", NOW).unwrap().err(),
        Some(Refusal::NameEmpty)
    );
    assert_eq!(
        fx.devices.enroll(&"x".repeat(41), NOW).unwrap().err(),
        Some(Refusal::NameTooLong)
    );
    let enrolled = fx.devices.enroll(" Tablette ", NOW).unwrap().unwrap();
    assert_eq!(enrolled.device.name, "Tablette");
    assert_eq!(enrolled.device.linked_by, "terminal");
    assert_eq!(enrolled.key.len(), 43);
    assert_eq!(
        fx.devices
            .recognise(&enrolled.key, NOW)
            .unwrap()
            .map(|(d, _)| d.id),
        Some(enrolled.device.id)
    );
}

#[test]
fn a_device_sees_whether_the_code_it_made_was_served_and_to_whom() {
    let fx = Fixture::new();
    let salon = fx.link("PC du salon");
    let other = fx.link("Liseuse");
    let issued = fx
        .devices
        .issue(Creator::Device(&salon.device.id), NOW)
        .unwrap();
    let me = &salon.device.id;
    assert_eq!(fx.devices.served(&issued.id, me, NOW).unwrap(), Some(None));
    assert_eq!(
        fx.devices
            .served(&issued.id, &other.device.id, NOW)
            .unwrap(),
        None
    );
    fx.devices
        .exchange(&issued.code, "Tablette", NOW)
        .unwrap()
        .unwrap();
    assert_eq!(
        fx.devices.served(&issued.id, me, NOW).unwrap(),
        Some(Some("Tablette".to_string()))
    );
    assert_eq!(
        fx.devices
            .served(&issued.id, &other.device.id, NOW)
            .unwrap(),
        None
    );

    // Never served and past its ten minutes: as good as unknown.
    let late = fx.devices.issue(Creator::Device(me), NOW).unwrap();
    assert_eq!(
        fx.devices
            .served(&late.id, me, NOW + CODE_LIFETIME)
            .unwrap(),
        None
    );
}

#[test]
fn a_device_cancels_only_the_codes_it_made() {
    let fx = Fixture::new();
    let salon = fx.link("PC du salon");
    let other = fx.link("Liseuse");
    let issued = fx
        .devices
        .issue(Creator::Device(&salon.device.id), NOW)
        .unwrap();
    assert!(!fx.devices.cancel(&issued.id, &other.device.id).unwrap());
    assert!(fx.devices.cancel(&issued.id, &salon.device.id).unwrap());
    assert_eq!(
        fx.devices
            .exchange(&issued.code, "Tablette", NOW)
            .unwrap()
            .err(),
        Some(Refusal::CodeNotValid)
    );
}

#[test]
fn the_list_names_who_linked_each_device() {
    let fx = Fixture::new();
    let salon = fx.link("PC du salon");
    let by_device = fx.code(Creator::Device(&salon.device.id));
    fx.devices
        .exchange(&by_device, "Tablette", NOW)
        .unwrap()
        .unwrap();
    let by_old_key = fx.code(Creator::OldKey("desktop"));
    fx.devices
        .exchange(&by_old_key, "Liseuse", NOW)
        .unwrap()
        .unwrap();
    let waiting = fx.devices.issue(Creator::Terminal, NOW).unwrap();

    let (devices, pending) = fx.devices.list(NOW).unwrap();
    let by = |name: &str| {
        devices
            .iter()
            .find(|d| d.name == name)
            .map(|d| d.linked_by.clone())
    };
    assert_eq!(devices.len(), 3);
    assert_eq!(by("PC du salon").as_deref(), Some("terminal"));
    assert_eq!(by("Tablette").as_deref(), Some("PC du salon"));
    assert_eq!(by("Liseuse").as_deref(), Some("ancienne clé desktop"));
    assert_eq!(pending.len(), 1);
    assert_eq!(pending[0].id, waiting.id);
    assert_eq!(pending[0].created_by, "terminal");
    assert_eq!(pending[0].expires_at, NOW + CODE_LIFETIME);
}

#[test]
fn a_device_is_marked_seen_at_most_once_a_minute() {
    let fx = Fixture::new();
    let linked = fx.link("PC du salon");
    let seen = |at| {
        fx.devices
            .recognise(&linked.key, at)
            .unwrap()
            .unwrap()
            .0
            .seen_at
    };
    assert_eq!(seen(NOW), Some(NOW));
    assert_eq!(seen(NOW + 60_000), Some(NOW));
    assert_eq!(seen(NOW + 60_001), Some(NOW + 60_001));
    let stored: Option<i64> = fx
        .db
        .read(|cx| {
            cx.query_one("SELECT seen_at FROM device", [], |r| r.get(0))
                .map(Option::unwrap)
        })
        .unwrap();
    assert_eq!(stored, Some(NOW + 60_001));
}

#[test]
fn a_library_is_closed_by_its_first_device_and_stays_closed_once_it_is_revoked() {
    let fx = Fixture::new();
    assert!(!fx.devices.closed().unwrap());
    fx.link("PC du salon");
    assert!(fx.devices.closed().unwrap());
    fx.devices.revoke("PC du salon").unwrap().expect("revoked");
    assert!(fx.devices.closed().unwrap());
}

#[test]
fn unlinking_the_device_that_closed_the_library_leaves_it_closed() {
    let fx = Fixture::new();
    let first = fx.link("PC du salon");
    fx.link("Liseuse");
    assert!(fx.devices.unlink(&first.device.id).unwrap());
    assert!(fx.devices.closed().unwrap());
    let (devices, _) = fx.devices.list(NOW).unwrap();
    assert_eq!(1, devices.len());
}

/// A `{:?}` in a log line, or a `tracing` field, is the commonest way a secret leaves a
/// process. The key and the code are the two values here that open the library.
#[test]
fn debugging_a_linked_device_never_shows_its_key() {
    let fx = Fixture::new();
    let linked = fx.link("PC du salon");
    let shown = format!("{linked:?}");
    assert!(!shown.contains(&linked.key), "{shown}");
    assert!(shown.contains("PC du salon"), "{shown}");
}

#[test]
fn debugging_an_issued_code_never_shows_the_code() {
    let fx = Fixture::new();
    let issued = fx.devices.issue(Creator::Terminal, NOW).unwrap();
    let shown = format!("{issued:?}");
    assert!(!shown.contains(&issued.code), "{shown}");
    assert!(!shown.contains(&issued.code.replace('-', "")), "{shown}");
}
