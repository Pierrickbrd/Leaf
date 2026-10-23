//! Profiles: who reads, what each may do, and the one code that guards the restricted ones.

use std::sync::Arc;

use leaf_server::store::profiles::{
    CodeScope, Kind, NewProfile, ProfilePatch, ProfileRefusal, Profiles, Rights, Seat,
    SwitchRefusal, COLORS,
};
use leaf_server::store::Db;

const NOW: i64 = 1_790_000_000_000;

struct Fixture {
    _dir: tempfile::TempDir,
    db: Arc<Db>,
    profiles: Profiles,
}

impl Fixture {
    fn new() -> Self {
        let dir = tempfile::tempdir().expect("a directory");
        let db = Arc::new(Db::open(&dir.path().join("index.sqlite")).expect("opening"));
        let profiles = Profiles::new(db.clone());
        Fixture {
            _dir: dir,
            db,
            profiles,
        }
    }

    fn reader(
        &self,
        name: &str,
        color: &str,
    ) -> Result<leaf_server::store::profiles::Profile, ProfileRefusal> {
        self.profiles
            .create(
                NewProfile {
                    name: name.into(),
                    color: color.into(),
                    restricted: false,
                    max_age: None,
                    rights: Rights {
                        import: false,
                        edit: false,
                        delete: false,
                    },
                },
                NOW,
            )
            .expect("creating")
    }

    /// A device on `profile`, as `Devices` would have linked it, with the three flags at rest.
    fn device_on(&self, id: &str, profile: &str) {
        self.db
            .write(|cx| {
                cx.execute(
                    "INSERT INTO device (id, name, name_key, key_digest, linked_at, linked_by, profile_id)
                     VALUES (?1, ?1, ?1, x'00', 1, 'terminal', ?2)",
                    [id, profile],
                )
            })
            .unwrap();
    }

    fn with_code(&self, scope: CodeScope) {
        self.profiles.set_code("2468", scope).unwrap().unwrap();
    }

    fn restricted(
        &self,
        name: &str,
        color: &str,
        age: Option<u8>,
    ) -> Result<leaf_server::store::profiles::Profile, ProfileRefusal> {
        self.profiles
            .create(
                NewProfile {
                    name: name.into(),
                    color: color.into(),
                    restricted: true,
                    max_age: age,
                    // Asked for, and refused to be given: a restricted profile has none.
                    rights: Rights {
                        import: true,
                        edit: true,
                        delete: true,
                    },
                },
                NOW,
            )
            .expect("creating")
    }
}

#[test]
fn the_migration_calls_the_first_profile_principal() {
    let fx = Fixture::new();
    let all = fx.profiles.list().expect("listing");
    assert_eq!(all.len(), 1);
    assert_eq!(all[0].id, "default");
    assert_eq!(all[0].name, "Principal");
    assert_eq!(all[0].color, "emeraude");
    assert_eq!(all[0].kind, Kind::Principal);
    assert_eq!(
        all[0].rights,
        Rights {
            import: true,
            edit: true,
            delete: true
        }
    );
}

#[test]
fn a_database_that_called_the_default_profile_par_defaut_is_renamed_on_the_way_in() {
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("index.sqlite");
    drop(Db::open(&file).unwrap());
    {
        let conn = rusqlite::Connection::open(&file).unwrap();
        conn.execute_batch(
            "UPDATE profile SET name = 'Par défaut', name_key = 'par défaut' WHERE id = 'default';
             PRAGMA user_version = 29;",
        )
        .unwrap();
    }
    let profiles = Profiles::new(Arc::new(Db::open(&file).unwrap()));
    assert_eq!(profiles.list().unwrap()[0].name, "Principal");
}

#[test]
fn a_database_at_the_twenty_ninth_migration_gains_the_columns_and_the_household() {
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("index.sqlite");
    drop(Db::open(&file).unwrap());
    {
        // The shape migration 29 left: none of what 31 adds, and the old name.
        let conn = rusqlite::Connection::open(&file).unwrap();
        conn.execute_batch(
            "ALTER TABLE profile DROP COLUMN color;
             ALTER TABLE profile DROP COLUMN can_import;
             ALTER TABLE profile DROP COLUMN can_edit;
             ALTER TABLE profile DROP COLUMN can_delete;
             ALTER TABLE profile DROP COLUMN reading;
             ALTER TABLE device DROP COLUMN kept;
             ALTER TABLE device DROP COLUMN choosing;
             ALTER TABLE device DROP COLUMN held;
             DROP TABLE household;
             UPDATE profile SET name = 'Par défaut', name_key = 'par défaut';
             PRAGMA user_version = 29;",
        )
        .unwrap();
    }
    let profiles = Profiles::new(Arc::new(Db::open(&file).unwrap()));
    let principal = &profiles.list().unwrap()[0];
    assert_eq!(
        (principal.name.as_str(), principal.color.as_str()),
        ("Principal", "emeraude")
    );
    assert_eq!(profiles.code().unwrap(), None);
    assert_eq!(profiles.reading("default").unwrap(), serde_json::json!({}));
}

#[test]
fn a_default_profile_already_named_by_its_owner_keeps_its_name() {
    let dir = tempfile::tempdir().unwrap();
    let file = dir.path().join("index.sqlite");
    drop(Db::open(&file).unwrap());
    {
        let conn = rusqlite::Connection::open(&file).unwrap();
        conn.execute_batch(
            "UPDATE profile SET name = 'Maison', name_key = 'maison' WHERE id = 'default';
             PRAGMA user_version = 29;",
        )
        .unwrap();
    }
    let profiles = Profiles::new(Arc::new(Db::open(&file).unwrap()));
    assert_eq!(profiles.list().unwrap()[0].name, "Maison");
}

#[test]
fn a_name_is_taken_whatever_its_case_and_accents() {
    let fx = Fixture::new();
    fx.reader("Inès", "bleu").expect("first");
    assert_eq!(fx.reader("INES", "rose"), Err(ProfileRefusal::NameTaken));
    assert_eq!(
        fx.reader("principal", "rose"),
        Err(ProfileRefusal::NameTaken)
    );
}

#[test]
fn a_colour_belongs_to_one_profile() {
    let fx = Fixture::new();
    fx.reader("Inès", "bleu").expect("first");
    assert_eq!(fx.reader("Léo", "bleu"), Err(ProfileRefusal::ColorTaken));
    assert_eq!(
        fx.reader("Léo", "violette"),
        Err(ProfileRefusal::UnknownColor)
    );
    assert_eq!(
        fx.reader("Léo", "emeraude"),
        Err(ProfileRefusal::ColorTaken)
    );
    assert_eq!(COLORS.len(), 16);
    assert_eq!(COLORS[0], "emeraude");
    assert_eq!(COLORS[15], "terre");
}

#[test]
fn a_profile_may_keep_its_own_name_and_colour_when_updated() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    let patch = ProfilePatch {
        name: Some("INÈS".into()),
        color: Some("bleu".into()),
        ..Default::default()
    };
    let updated = fx
        .profiles
        .update(&ines.id, patch)
        .unwrap()
        .expect("updated");
    assert_eq!(updated.name, "INÈS");
}

#[test]
fn a_restricted_profile_needs_the_code_first() {
    let fx = Fixture::new();
    assert_eq!(
        fx.restricted("Léo", "bleu", Some(10)),
        Err(ProfileRefusal::CodeRequired)
    );
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .expect("code");
    let leo = fx.restricted("Léo", "bleu", Some(10)).expect("created");
    assert_eq!(leo.kind, Kind::Restricted);
    assert_eq!(
        leo.rights,
        Rights {
            import: false,
            edit: false,
            delete: false
        }
    );
    assert_eq!(leo.max_age, Some(10));
    assert_eq!(fx.profiles.get(&leo.id).unwrap().unwrap(), leo);
}

#[test]
fn a_reader_made_restricted_loses_its_rights_and_regains_none_when_released() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    let ines = fx
        .profiles
        .create(
            NewProfile {
                name: "Inès".into(),
                color: "bleu".into(),
                restricted: false,
                max_age: None,
                rights: Rights {
                    import: true,
                    edit: false,
                    delete: true,
                },
            },
            NOW,
        )
        .unwrap()
        .unwrap();
    assert_eq!(
        ines.rights,
        Rights {
            import: true,
            edit: false,
            delete: true
        }
    );
    let restricted = fx
        .profiles
        .update(
            &ines.id,
            ProfilePatch {
                restricted: Some(true),
                max_age: Some(12),
                ..Default::default()
            },
        )
        .unwrap()
        .unwrap();
    assert_eq!(restricted.kind, Kind::Restricted);
    let released = fx
        .profiles
        .update(
            &ines.id,
            ProfilePatch {
                restricted: Some(false),
                ..Default::default()
            },
        )
        .unwrap()
        .unwrap();
    assert_eq!(released.kind, Kind::Reader);
    assert_eq!(released.max_age, None);
    assert_eq!(
        released.rights,
        Rights {
            import: false,
            edit: false,
            delete: false
        }
    );
}

#[test]
fn an_age_outside_three_to_seventeen_is_refused() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    assert_eq!(
        fx.restricted("A", "bleu", Some(2)),
        Err(ProfileRefusal::BadAge)
    );
    assert_eq!(
        fx.restricted("B", "rose", Some(18)),
        Err(ProfileRefusal::BadAge)
    );
    assert_eq!(
        fx.restricted("C", "azur", None),
        Err(ProfileRefusal::BadAge)
    );
    assert!(fx.restricted("D", "terre", Some(3)).is_ok());
    assert!(fx.restricted("E", "sapin", Some(17)).is_ok());
}

#[test]
fn the_principal_can_be_neither_deleted_nor_restricted() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    assert_eq!(
        fx.profiles.delete("default").unwrap(),
        Err(ProfileRefusal::PrincipalIsFixed)
    );
    let patch = ProfilePatch {
        restricted: Some(true),
        max_age: Some(10),
        ..Default::default()
    };
    assert_eq!(
        fx.profiles.update("default", patch).unwrap(),
        Err(ProfileRefusal::PrincipalIsFixed)
    );
    assert_eq!(
        fx.profiles.delete("nobody").unwrap(),
        Err(ProfileRefusal::NotFound)
    );
    // Renaming it is the one thing that is allowed.
    let renamed = fx
        .profiles
        .update(
            "default",
            ProfilePatch {
                name: Some("Papa".into()),
                ..Default::default()
            },
        )
        .unwrap()
        .unwrap();
    assert_eq!(renamed.name, "Papa");
}

#[test]
fn the_code_stays_while_a_profile_is_restricted() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    assert_eq!(
        fx.profiles.clear_code().unwrap(),
        Err(ProfileRefusal::CodeStillNeeded)
    );
    fx.profiles.delete(&leo.id).unwrap().expect("deleted");
    fx.profiles.clear_code().unwrap().expect("cleared");
    assert_eq!(fx.profiles.code().unwrap(), None);
}

#[test]
fn a_code_is_four_to_eight_characters() {
    let fx = Fixture::new();
    assert_eq!(
        fx.profiles.set_code("123", CodeScope::Restricted).unwrap(),
        Err(ProfileRefusal::BadCode)
    );
    assert_eq!(
        fx.profiles
            .set_code("123456789", CodeScope::Restricted)
            .unwrap(),
        Err(ProfileRefusal::BadCode)
    );
    assert_eq!(fx.profiles.code().unwrap(), None);
    // Characters, not bytes: this is four of them and eight bytes.
    fx.profiles
        .set_code("é€ab", CodeScope::Everything)
        .unwrap()
        .expect("accepted");
    assert_eq!(fx.profiles.code().unwrap(), Some(CodeScope::Everything));
    assert!(fx.profiles.check_code("é€ab").unwrap());
}

#[test]
fn the_code_is_kept_as_a_slow_digest_and_checked() {
    let fx = Fixture::new();
    assert!(
        !fx.profiles.check_code("2468").unwrap(),
        "no code, nothing matches"
    );
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    assert!(fx.profiles.check_code("2468").unwrap());
    assert!(!fx.profiles.check_code("2469").unwrap());
    let stored: String = fx
        .db
        .read(|cx| {
            Ok(cx
                .query_one("SELECT code_phc FROM household WHERE id = 1", [], |r| {
                    r.get(0)
                })?
                .expect("the row"))
        })
        .unwrap();
    assert!(!stored.contains("2468"));
    assert!(stored.starts_with("$argon2"), "{stored}");
}

#[test]
fn deleting_a_profile_takes_its_progress_with_it() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    fx.db
        .write(|cx| {
            cx.execute("INSERT INTO work (id, name, path) VALUES ('w', 'Essai', '/tmp/essai')", [])?;
            cx.execute(
                "INSERT INTO edition (id, work_id, name, path, implicit) VALUES ('e', 'w', NULL, '/tmp/essai', 1)",
                [],
            )?;
            cx.execute(
                "INSERT INTO entry (id, edition_id, type, file, size, modified_at)
                 VALUES ('x', 'e', 'VOLUME', '/tmp/essai/t1.cbz', 1, 1)",
                [],
            )?;
            for who in [ines.id.as_str(), "default"] {
                cx.execute(
                    "INSERT INTO progress (profile_id, entry_id, edition_id, page, finished, updated_at)
                     VALUES (?1, 'x', 'e', 4, 0, 1)",
                    [who],
                )?;
            }
            Ok(())
        })
        .unwrap();
    fx.profiles.delete(&ines.id).unwrap().expect("deleted");
    let left: Vec<String> = fx
        .db
        .read(|cx| cx.query("SELECT profile_id FROM progress", [], |r| r.get(0)))
        .unwrap();
    assert_eq!(left, vec!["default".to_string()]);
    assert_eq!(fx.profiles.get(&ines.id).unwrap(), None);
}

#[test]
fn a_device_on_a_deleted_profile_falls_back_to_the_principal() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    fx.db
        .write(|cx| {
            cx.execute(
                "INSERT INTO device (id, name, name_key, key_digest, linked_at, linked_by, profile_id)
                 VALUES ('d', 'Tablette', 'tablette', x'00', 1, 'terminal', ?1)",
                [&ines.id],
            )
        })
        .unwrap();
    fx.profiles.delete(&ines.id).unwrap().expect("deleted");
    let on: String = fx
        .db
        .read(|cx| {
            Ok(cx
                .query_one("SELECT profile_id FROM device", [], |r| r.get(0))?
                .unwrap())
        })
        .unwrap();
    assert_eq!(on, "default");
}

#[test]
fn a_profile_keeps_its_reading_preferences_apart() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    assert_eq!(
        fx.profiles.reading(&ines.id).unwrap(),
        serde_json::json!({})
    );
    fx.profiles
        .set_reading(&ines.id, &serde_json::json!({"direction": "rtl"}))
        .unwrap();
    fx.profiles
        .set_reading(
            "default",
            &serde_json::json!({"direction": "ltr", "fit": "width"}),
        )
        .unwrap();
    assert_eq!(
        fx.profiles.reading(&ines.id).unwrap(),
        serde_json::json!({"direction": "rtl"})
    );
    assert_eq!(
        fx.profiles.reading("default").unwrap(),
        serde_json::json!({"direction": "ltr", "fit": "width"})
    );
}

#[test]
fn listing_the_profiles_asks_one_question() {
    let fx = Fixture::new();
    fx.reader("Inès", "bleu").unwrap();
    fx.reader("Léo", "rose").unwrap();
    let before = fx.db.statements();
    let all = fx.profiles.list().unwrap();
    assert_eq!(all.len(), 3);
    assert_eq!(fx.db.statements() - before, 1);
}

#[test]
fn a_device_on_a_deleted_restricted_profile_must_choose_and_is_held() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    fx.db
        .write(|cx| {
            cx.execute(
                "INSERT INTO device (id, name, name_key, key_digest, linked_at, linked_by, profile_id)
                 VALUES ('d', 'Tablette', 'tablette', x'00', 1, 'terminal', ?1)",
                [&leo.id],
            )
        })
        .unwrap();
    fx.profiles.delete(&leo.id).unwrap().expect("deleted");
    let (choosing, held): (bool, bool) = fx
        .db
        .read(|cx| {
            Ok(cx
                .query_one("SELECT choosing, held FROM device", [], |r| {
                    Ok((r.get(0)?, r.get(1)?))
                })?
                .unwrap())
        })
        .unwrap();
    assert!(choosing && held);
}

#[test]
fn an_empty_or_blank_name_is_a_refusal_not_an_error() {
    let fx = Fixture::new();
    assert_eq!(fx.reader("", "bleu"), Err(ProfileRefusal::BadName));
    assert_eq!(fx.reader("  \t ", "bleu"), Err(ProfileRefusal::BadName));
    let patch = ProfilePatch {
        name: Some("   ".into()),
        ..Default::default()
    };
    assert_eq!(
        fx.profiles.update("default", patch).unwrap(),
        Err(ProfileRefusal::BadName)
    );
}

#[test]
fn unreadable_reading_preferences_read_as_nothing() {
    let fx = Fixture::new();
    fx.db
        .write(|cx| {
            cx.execute(
                "UPDATE profile SET reading = '{oops' WHERE id = 'default'",
                [],
            )
        })
        .unwrap();
    assert_eq!(
        fx.profiles.reading("default").unwrap(),
        serde_json::json!({})
    );
}

/// What `f` costs in statements, for the tests below: a cost, not a speed.
fn cost<T>(fx: &Fixture, f: impl FnOnce(&Profiles) -> T) -> u64 {
    let before = fx.db.statements();
    let _ = f(&fx.profiles);
    fx.db.statements() - before
}

#[test]
fn reading_one_profile_asks_one_question() {
    let fx = Fixture::new();
    assert_eq!(cost(&fx, |p| p.get("default")), 1);
    assert_eq!(cost(&fx, |p| p.reading("default")), 1);
}

#[test]
fn checking_the_code_asks_one_question() {
    let fx = Fixture::new();
    fx.profiles
        .set_code("2468", CodeScope::Restricted)
        .unwrap()
        .unwrap();
    assert_eq!(cost(&fx, |p| p.check_code("2468")), 1);
}

#[test]
fn creating_a_profile_costs_a_fixed_handful_of_statements() {
    let fx = Fixture::new();
    // Name, colour, the insert and the read back; the code is asked only for a restricted one.
    let a = cost(&fx, |p| {
        p.create(
            NewProfile {
                name: "A".into(),
                color: "bleu".into(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
    });
    let b = cost(&fx, |p| {
        p.create(
            NewProfile {
                name: "B".into(),
                color: "rose".into(),
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            },
            NOW,
        )
    });
    assert_eq!(a, 4);
    assert_eq!(a, b, "it does not grow with the number of profiles");
}

#[test]
fn updating_a_profile_costs_five_statements() {
    let fx = Fixture::new();
    let p = fx.reader("A", "bleu").unwrap();
    let patch = ProfilePatch {
        color: Some("rose".into()),
        ..Default::default()
    };
    // Find, name, colour, update, read back.
    assert_eq!(cost(&fx, |x| x.update(&p.id, patch)), 5);
}

#[test]
fn deleting_a_profile_costs_four_statements() {
    let fx = Fixture::new();
    let p = fx.reader("A", "bleu").unwrap();
    // Find, devices, progress, the profile itself.
    assert_eq!(cost(&fx, |x| x.delete(&p.id)), 4);
}

#[test]
fn setting_the_code_and_a_reading_each_cost_one_statement() {
    let fx = Fixture::new();
    assert_eq!(cost(&fx, |p| p.set_code("2468", CodeScope::Restricted)), 1);
    assert_eq!(
        cost(&fx, |p| p.set_reading("default", &serde_json::json!({}))),
        1
    );
}

fn seat(profile: &str, kept: bool) -> Seat {
    Seat {
        profile_id: profile.into(),
        kept,
        choosing: false,
        held: false,
    }
}

#[test]
fn leaving_a_restricted_profile_takes_the_code() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    let ines = fx.reader("Inès", "rose").unwrap();
    fx.device_on("d", &leo.id);
    assert_eq!(
        fx.profiles.switch("d", &ines.id, None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", &ines.id, Some("0000"), false)
            .unwrap(),
        Err(SwitchRefusal::WrongCode)
    );
    assert_eq!(fx.profiles.seat("d").unwrap(), seat(&leo.id, false));
    assert_eq!(
        fx.profiles
            .switch("d", &ines.id, Some("2468"), false)
            .unwrap(),
        Ok(seat(&ines.id, false))
    );
    assert_eq!(fx.profiles.seat("d").unwrap(), seat(&ines.id, false));
}

#[test]
fn the_principal_is_free_to_reach_unless_the_code_guards_it() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let ines = fx.reader("Inès", "rose").unwrap();
    fx.device_on("d", &ines.id);
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Ok(seat("default", false))
    );
    fx.profiles
        .set_code("2468", CodeScope::Everything)
        .unwrap()
        .unwrap();
    fx.profiles
        .switch("d", &ines.id, None, false)
        .unwrap()
        .unwrap();
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", "default", Some("2468"), false)
            .unwrap(),
        Ok(seat("default", false))
    );
    // On the principal without « Retenir »: staying on it is still the code's to allow, since
    // anybody may open the device next. Retained with the code, staying is free, and going to
    // a reader asks nothing either way.
    assert_eq!(
        fx.profiles.switch("d", "default", None, true).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", "default", Some("2468"), true)
            .unwrap(),
        Ok(seat("default", true))
    );
    assert_eq!(
        fx.profiles.switch("d", "default", None, true).unwrap(),
        Ok(seat("default", true))
    );
    assert_eq!(
        fx.profiles.switch("d", &ines.id, None, false).unwrap(),
        Ok(seat(&ines.id, false))
    );
}

#[test]
fn an_unretained_principal_seat_is_not_a_way_round_the_code() {
    // Left on the principal without « Retenir », a family laptop reopened on it for anyone,
    // and the code window took any code because none was checked.
    let fx = Fixture::new();
    fx.reader("Inès", "rose").unwrap();
    fx.device_on("d", "default");
    fx.with_code(CodeScope::Everything);
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", "default", Some("1357"), false)
            .unwrap(),
        Err(SwitchRefusal::WrongCode)
    );
    assert_eq!(
        fx.profiles
            .switch_checked("d", "default", Some("2468"), false)
            .unwrap(),
        Ok((seat("default", false), true))
    );
}

#[test]
fn a_restricted_profile_cannot_be_kept_from_who_reads() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    fx.device_on("d", "default");
    assert_eq!(
        fx.profiles.switch("d", &leo.id, None, true).unwrap(),
        Err(SwitchRefusal::NotYours)
    );
    assert_eq!(fx.profiles.seat("d").unwrap(), seat("default", false));
    assert_eq!(
        fx.profiles.place("d", &leo.id, true).unwrap(),
        Ok(seat(&leo.id, true))
    );
    assert_eq!(
        fx.profiles.switch("d", "nobody", None, false).unwrap(),
        Err(SwitchRefusal::NoSuchProfile)
    );
}

#[test]
fn deleting_the_profile_a_device_sits_on_makes_it_choose() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    fx.device_on("d", &ines.id);
    fx.profiles.place("d", &ines.id, true).unwrap().unwrap();
    fx.profiles.delete(&ines.id).unwrap().unwrap();
    assert_eq!(
        fx.profiles.seat("d").unwrap(),
        Seat {
            profile_id: "default".into(),
            kept: false,
            choosing: true,
            held: false,
        }
    );
}

#[test]
fn deleting_a_restricted_profile_leaves_its_device_held() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    let ines = fx.reader("Inès", "rose").unwrap();
    fx.device_on("d", &leo.id);
    fx.profiles.delete(&leo.id).unwrap().unwrap();
    let held = fx.profiles.seat("d").unwrap();
    assert!(held.held && held.choosing && !held.kept);
    // Parked on the principal, the device is still a child's: any way out asks the code.
    assert_eq!(
        fx.profiles.switch("d", &ines.id, None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", &ines.id, Some("2468"), false)
            .unwrap(),
        Ok(seat(&ines.id, false))
    );
}

#[test]
fn a_held_device_with_no_code_left_is_not_stuck() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    fx.device_on("d", &leo.id);
    fx.profiles.delete(&leo.id).unwrap().unwrap();
    fx.profiles.clear_code().unwrap().unwrap();
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Ok(seat("default", false))
    );
}

#[test]
fn placing_a_device_ends_its_choice_and_its_hold() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Everything);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    fx.device_on("d", &leo.id);
    fx.profiles.delete(&leo.id).unwrap().unwrap();
    assert_eq!(
        fx.profiles.place("d", "default", true).unwrap(),
        Ok(seat("default", true))
    );
    assert_eq!(fx.profiles.seat("d").unwrap(), seat("default", true));
}

#[test]
fn two_switches_at_once_leave_one_seat() {
    // The writer is one lock and one transaction per switch, so the two run in turn and the
    // seat is the whole of whichever ran last. Repeated, because a mixture would only show
    // on some interleavings.
    for _ in 0..20 {
        let fx = Fixture::new();
        let a = fx.reader("A", "bleu").unwrap();
        let b = fx.reader("B", "rose").unwrap();
        fx.device_on("d", "default");
        let gate = Arc::new(std::sync::Barrier::new(2));
        let handles: Vec<_> = [(a.id.clone(), true), (b.id.clone(), false)]
            .into_iter()
            .map(|(to, keep)| {
                let profiles = Profiles::new(fx.db.clone());
                let gate = gate.clone();
                std::thread::spawn(move || {
                    gate.wait();
                    profiles.switch("d", &to, None, keep).unwrap().unwrap()
                })
            })
            .collect();
        for h in handles {
            h.join().unwrap();
        }
        let end = fx.profiles.seat("d").unwrap();
        let expected_a = seat(&a.id, true);
        let expected_b = seat(&b.id, false);
        assert!(end == expected_a || end == expected_b, "a mixture: {end:?}");
        assert!(!end.choosing && !end.held);
    }
}

#[test]
fn a_device_made_to_choose_is_not_one_that_chooses_again_when_it_switches() {
    let fx = Fixture::new();
    let ines = fx.reader("Inès", "bleu").unwrap();
    fx.device_on("d", &ines.id);
    fx.profiles.delete(&ines.id).unwrap().unwrap();
    assert!(fx.profiles.seat("d").unwrap().choosing);
    fx.profiles
        .switch("d", "default", None, false)
        .unwrap()
        .unwrap();
    assert!(!fx.profiles.seat("d").unwrap().choosing);
}

#[test]
fn seating_costs_a_fixed_handful_of_statements() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Restricted);
    let leo = fx.restricted("Léo", "bleu", Some(10)).unwrap();
    let ines = fx.reader("Inès", "rose").unwrap();
    fx.device_on("d", "default");
    assert_eq!(cost(&fx, |p| p.seat("d")), 1);
    // Free switch: the facts read twice (device, target, current profile, household), and the update.
    assert_eq!(cost(&fx, |p| p.switch("d", &ines.id, None, false)), 9);
    // Leaving a restricted profile adds the one statement that reads the stored code.
    fx.profiles.place("d", &leo.id, false).unwrap().unwrap();
    assert_eq!(
        cost(&fx, |p| p.switch("d", &ines.id, Some("2468"), false)),
        10
    );
    assert_eq!(cost(&fx, |p| p.place("d", "default", false)), 2);
}

#[test]
fn a_device_that_must_choose_is_not_the_principal_when_the_code_guards_it() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Everything);
    let ines = fx.reader("Inès", "bleu").unwrap();
    fx.device_on("d", &ines.id);
    fx.profiles.delete(&ines.id).unwrap().unwrap();
    assert_eq!(
        fx.profiles.switch("d", "default", None, false).unwrap(),
        Err(SwitchRefusal::CodeNeeded)
    );
    assert_eq!(
        fx.profiles
            .switch("d", "default", Some("2468"), false)
            .unwrap(),
        Ok(seat("default", false))
    );
}

#[test]
fn a_switch_says_whether_it_checked_the_code() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Everything);
    let ines = fx.reader("Inès", "bleu").unwrap();
    let anna = fx.reader("Anna", "rose").unwrap();
    fx.device_on("d", &ines.id);
    // Free, with text in the field anyway: nothing was checked, and the route must not take
    // the success for a right code.
    let (_, checked) = fx
        .profiles
        .switch_checked("d", &anna.id, Some("faux"), false)
        .unwrap()
        .unwrap();
    assert!(!checked);
    let (seat, checked) = fx
        .profiles
        .switch_checked("d", "default", Some("2468"), false)
        .unwrap()
        .unwrap();
    assert!(checked);
    assert_eq!(seat.profile_id, "default");
}

#[test]
fn placing_a_device_that_is_not_there_is_a_refusal() {
    let fx = Fixture::new();
    assert_eq!(
        fx.profiles.place("gone", "default", false).unwrap(),
        Err(SwitchRefusal::NoSuchDevice)
    );
}

fn restricted_one(name: &str, color: &str) -> NewProfile {
    NewProfile {
        name: name.into(),
        color: color.into(),
        restricted: true,
        max_age: Some(10),
        rights: Rights::default(),
    }
}

#[test]
fn the_first_restricted_profile_and_its_code_are_made_together_or_not_at_all() {
    let fx = Fixture::new();
    fx.reader("Inès", "bleu").unwrap();
    assert_eq!(
        fx.profiles
            .create_with_code(restricted_one("inès", "rose"), Some("2468"), NOW)
            .unwrap(),
        Err(ProfileRefusal::NameTaken)
    );
    assert_eq!(None, fx.profiles.code().unwrap(), "no code left behind");
    assert_eq!(
        fx.profiles
            .create_with_code(restricted_one("Léo", "rose"), Some("24"), NOW)
            .unwrap(),
        Err(ProfileRefusal::BadCode)
    );
    let leo = fx
        .profiles
        .create_with_code(restricted_one("Léo", "rose"), Some("2468"), NOW)
        .unwrap()
        .unwrap();
    assert_eq!(leo.kind, Kind::Restricted);
    assert_eq!(Some(CodeScope::Restricted), fx.profiles.code().unwrap());
    assert!(fx.profiles.check_code("2468").unwrap());
}

#[test]
fn a_code_already_there_wins_over_the_one_given_with_a_profile() {
    let fx = Fixture::new();
    fx.with_code(CodeScope::Everything);
    fx.profiles
        .create_with_code(restricted_one("Léo", "rose"), Some("1357"), NOW)
        .unwrap()
        .unwrap();
    assert!(fx.profiles.check_code("2468").unwrap());
    assert_eq!(Some(CodeScope::Everything), fx.profiles.code().unwrap());
}

#[test]
fn creating_a_restricted_profile_with_its_code_costs_six_statements() {
    let fx = Fixture::new();
    // The household's code, name, colour, the code written, the insert, the read back.
    assert_eq!(
        cost(&fx, |p| p.create_with_code(
            restricted_one("Léo", "rose"),
            Some("2468"),
            NOW
        )),
        6
    );
    // With the code already there, it is not written again.
    assert_eq!(
        cost(&fx, |p| p.create_with_code(
            restricted_one("Mia", "bleu"),
            Some("2468"),
            NOW
        )),
        5
    );
}
