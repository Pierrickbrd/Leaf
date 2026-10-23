//! Profiles, the code, reading preferences and the devices: the routes of « Qui lit ? », of
//! Réglages › Profils and of the Devices tab.
//!
//! The design's tables of rights are the reference, and they are not one guard per route:
//! adding a reader is the principal's, a reader's with the code when the principal chose so,
//! and nobody else's; changing a name is the principal's for anyone and a reader's for
//! itself. So the routes that the tables split take a [`Caller`] and decide from its
//! [`Role`]; the ones that are the principal's alone take [`Principal`]; the two that a
//! device that must choose has to reach take [`Unseated`].
//!
//! Every wrong code counts on `AppState::code_throttle`, apart from the brake on keys: a
//! device whose key is good would wipe that one with every request it made, and guess the
//! code for ever ten at a time.

use std::sync::Arc;

use axum::extract::{Path, State};
use axum::http::StatusCode;
use axum::response::IntoResponse;
use axum::Json;

use super::dto::{
    CodeDto, DeviceEntryDto, DevicesDto, ErrorDto, NewProfileDto, PlacementDto, ProfileDto,
    ProfilePatchDto, ProfilesDto, RightsDto, SeatDto,
};
use super::linking::{say, Answer};
use super::routes::{
    blocking, is_principal, now_in_millis, AppState, Caller, Principal, Unseated, Who,
};
use crate::store::devices::Device;
use crate::store::profiles::{
    CodeScope, Kind, NewProfile, Profile, ProfilePatch, ProfileRefusal, Rights, SwitchRefusal,
};
use crate::store::schema::DEFAULT_PROFILE;

/// What the tables of the design call the caller. An old key is the principal when it may
/// import — the keys that could scan before profiles existed — and a reader otherwise, with
/// no profile of its own to rename. An open library is the principal.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Role {
    Principal,
    Reader,
    Restricted,
}

fn role(who: &Who) -> Role {
    if is_principal(who) {
        return Role::Principal;
    }
    match who {
        Who::Device(_, profile) if profile.kind == Kind::Restricted => Role::Restricted,
        _ => Role::Reader,
    }
}

/// Who the brake on codes counts against: the device, since a code is guessed from one. An
/// old key and an open library have no device, and are counted under their own name.
fn brake_of(who: &Who) -> String {
    match who {
        Who::Device(device, _) => format!("device {}", device.id),
        Who::OldKey(key) => format!("key {}", key.name),
        Who::Open => "open".to_string(),
    }
}

fn kind_name(kind: Kind) -> &'static str {
    match kind {
        Kind::Principal => "principal",
        Kind::Reader => "reader",
        Kind::Restricted => "restricted",
    }
}

fn profile_dto(profile: Profile) -> ProfileDto {
    ProfileDto {
        kind: kind_name(profile.kind),
        id: profile.id,
        name: profile.name,
        color: profile.color,
        max_age: profile.max_age,
        rights: RightsDto {
            import: profile.rights.import,
            edit: profile.rights.edit,
            delete: profile.rights.delete,
        },
    }
}

fn rights_of(dto: RightsDto) -> Rights {
    Rights {
        import: dto.import,
        edit: dto.edit,
        delete: dto.delete,
    }
}

fn code_name(code: Option<CodeScope>) -> &'static str {
    match code {
        None => "none",
        Some(CodeScope::Restricted) => "restricted",
        Some(CodeScope::Everything) => "everything",
    }
}

/// `reader` or `restricted`, the only two a request may make: there is one principal, the
/// migration made it, and nothing makes another.
fn restricted_from(kind: &str) -> Result<bool, Answer> {
    match kind {
        "reader" => Ok(false),
        "restricted" => Ok(true),
        _ => Err(say(
            StatusCode::BAD_REQUEST,
            "a profile is a reader or restricted",
        )),
    }
}

/// An age as the request wrote it. Read as any JSON number and judged here, so that 300, -1
/// and 3.5 are the 400 of an age out of range the contract promises, and not the 422 a field
/// typed `u8` gave them. The range itself is the store's to say.
fn age_from(age: Option<&serde_json::Number>) -> Result<Option<u8>, Answer> {
    match age {
        None => Ok(None),
        Some(age) => age
            .as_u64()
            .and_then(|age| u8::try_from(age).ok())
            .map(Some)
            .ok_or_else(|| refused(ProfileRefusal::BadAge)),
    }
}

fn refused(refusal: ProfileRefusal) -> Answer {
    let (status, message) = match refusal {
        ProfileRefusal::NameTaken => (StatusCode::CONFLICT, "name taken"),
        ProfileRefusal::ColorTaken => (StatusCode::CONFLICT, "colour taken"),
        ProfileRefusal::UnknownColor => (StatusCode::BAD_REQUEST, "unknown colour"),
        ProfileRefusal::BadAge => (
            StatusCode::BAD_REQUEST,
            "a restricted profile is given an age from 3 to 17",
        ),
        ProfileRefusal::NotFound => (StatusCode::NOT_FOUND, "no such profile"),
        ProfileRefusal::PrincipalIsFixed => (
            StatusCode::CONFLICT,
            "the principal profile is neither deleted nor restricted",
        ),
        // A state of the household the client can mend — make the code, in the same window —
        // rather than a malformed request: the same body succeeds once there is a code.
        ProfileRefusal::CodeRequired => (StatusCode::CONFLICT, "code required"),
        ProfileRefusal::CodeStillNeeded => (StatusCode::CONFLICT, "a profile is restricted"),
        ProfileRefusal::BadCode => (StatusCode::BAD_REQUEST, "the code is 4 to 8 characters"),
        ProfileRefusal::BadName => (StatusCode::BAD_REQUEST, "a name cannot be empty"),
    };
    say(status, message)
}

/// The one answer to a code that is missing or wrong: which of the two is not worth telling
/// whoever is guessing.
fn wrong_code() -> Answer {
    say(StatusCode::FORBIDDEN, "code")
}

fn too_many(wait: std::time::Duration) -> Answer {
    Answer::Said(Box::new(
        (
            StatusCode::TOO_MANY_REQUESTS,
            [("Retry-After", wait.as_secs().max(1).to_string())],
            Json(ErrorDto::new("too many wrong codes — try again later")),
        )
            .into_response(),
    ))
}

/// The two codes typed when one is made. Apart, they would be a slip of the finger the
/// principal finds out about the day a child asks to leave its profile.
fn twice(code: &str, again: &str) -> Result<(), Answer> {
    if code == again {
        Ok(())
    } else {
        Err(say(StatusCode::BAD_REQUEST, "the two codes differ"))
    }
}

/// Checks the code against the brake: refused before Argon2 runs while the brake holds, so
/// a blocked device costs the server nothing; counted when wrong, forgiven when right.
async fn check_code(state: &AppState, brake: &str, code: Option<String>) -> Result<(), Answer> {
    let attempt = state.code_throttle.attempt(brake).map_err(too_many)?;
    let Some(code) = code else {
        // Nothing offered, nothing guessed: the slot goes back uncounted.
        attempt.void();
        return Err(wrong_code());
    };
    let profiles = Arc::clone(&state.profiles);
    if blocking(move || profiles.check_code(&code)).await? {
        attempt.succeeded();
        Ok(())
    } else {
        attempt.failed();
        Err(wrong_code())
    }
}

/// Where a caller sits, as « Qui lit ? » shows it. An old key and an open library have no
/// device: they are the principal, and keep it.
struct Sitting {
    active: String,
    kept: bool,
    choosing: bool,
    held: bool,
}

fn sitting_of(who: &Who) -> Sitting {
    match who {
        Who::Device(device, _) => Sitting {
            active: device.profile_id.clone(),
            kept: device.kept,
            choosing: device.choosing,
            held: device.held,
        },
        Who::Open | Who::OldKey(_) => Sitting {
            active: DEFAULT_PROFILE.to_string(),
            kept: true,
            choosing: false,
            held: false,
        },
    }
}

async fn who_reads(state: &AppState, sitting: Sitting) -> Result<ProfilesDto, Answer> {
    let profiles = Arc::clone(&state.profiles);
    let (list, code) = blocking(move || Ok((profiles.list()?, profiles.code()?))).await?;
    Ok(ProfilesDto {
        profiles: list.into_iter().map(profile_dto).collect(),
        active: sitting.active,
        kept: sitting.kept,
        choosing: sitting.choosing,
        held: sitting.held,
        code: code_name(code),
    })
}

// ------------------------------------------------------------------ « Qui lit ? »

/// Every profile, and where the calling device sits — to any device, one that must choose
/// included, since this is how it chooses.
pub async fn list_profiles(
    Unseated(who): Unseated,
    State(state): State<AppState>,
) -> Result<Json<ProfilesDto>, Answer> {
    Ok(Json(who_reads(&state, sitting_of(&who)).await?))
}

/// The calling device reads as another profile, with the code where the table asks it.
pub async fn switch_profile(
    Unseated(who): Unseated,
    State(state): State<AppState>,
    Json(request): Json<SeatDto>,
) -> Result<Json<ProfilesDto>, Answer> {
    let Who::Device(device, _) = &who else {
        return Err(say(StatusCode::CONFLICT, "no device"));
    };
    let brake = brake_of(&who);
    // Whatever the switch would need: a device that guessed ten codes waits, and checking
    // first what this one needs would cost the same reads the wait exists to spare. A code
    // offered is a guess reserved before Argon2 runs, so guesses sent together are counted
    // together (`Throttle::attempt`); with none offered, nothing is hashed, and the block is
    // all there is to ask.
    let attempt = match &request.code {
        Some(_) => Some(state.code_throttle.attempt(&brake).map_err(too_many)?),
        None => {
            if let Some(wait) = state.code_throttle.blocked_for(&brake) {
                return Err(too_many(wait));
            }
            None
        }
    };
    let profiles = Arc::clone(&state.profiles);
    let device_id = device.id.clone();
    let SeatDto { id, code, keep } = request;
    let outcome =
        blocking(move || profiles.switch_checked(&device_id, &id, code.as_deref(), keep)).await?;
    // Settled for every outcome: forgiven only for a code checked and found right, counted
    // only for one checked and found wrong, given back for a switch that checked nothing.
    let settle = |judged: Option<bool>| {
        if let Some(attempt) = attempt {
            match judged {
                Some(true) => attempt.succeeded(),
                Some(false) => attempt.failed(),
                None => attempt.void(),
            }
        }
    };
    let seat = match outcome {
        Ok((seat, checked)) => {
            settle(checked.then_some(true));
            seat
        }
        Err(SwitchRefusal::WrongCode) => {
            settle(Some(false));
            return Err(wrong_code());
        }
        Err(refusal) => {
            settle(None);
            return Err(switch_refused(refusal));
        }
    };
    let sitting = Sitting {
        active: seat.profile_id,
        kept: seat.kept,
        choosing: seat.choosing,
        held: seat.held,
    };
    Ok(Json(who_reads(&state, sitting).await?))
}

fn switch_refused(refusal: SwitchRefusal) -> Answer {
    match refusal {
        // Not a guess: nothing was offered to be wrong.
        SwitchRefusal::CodeNeeded | SwitchRefusal::WrongCode => wrong_code(),
        SwitchRefusal::NoSuchProfile => say(StatusCode::NOT_FOUND, "no such profile"),
        SwitchRefusal::NotYours => say(
            StatusCode::FORBIDDEN,
            "only the principal profile has a device keep a restricted profile",
        ),
        SwitchRefusal::NoSuchDevice => say(StatusCode::NOT_FOUND, "no such device"),
    }
}

// ------------------------------------------------------------------ the profiles

/// A new profile: anything for the principal; a reader without rights for a reader, with the
/// code when the principal chose that it guards everything; nothing for a restricted one.
pub async fn create_profile(
    Caller(who): Caller,
    State(state): State<AppState>,
    Json(request): Json<NewProfileDto>,
) -> Result<(StatusCode, Json<ProfileDto>), Answer> {
    let restricted = restricted_from(&request.kind)?;
    let max_age = age_from(request.max_age.as_ref())?;
    let role = role(&who);
    if role == Role::Restricted {
        return Err(say(
            StatusCode::FORBIDDEN,
            "a restricted profile adds no profile",
        ));
    }
    if role == Role::Reader && restricted {
        return Err(say(
            StatusCode::FORBIDDEN,
            "only the principal profile may restrict a profile",
        ));
    }
    let profiles = Arc::clone(&state.profiles);
    let code = blocking(move || profiles.code()).await?;
    let new = match role {
        Role::Reader => {
            if code == Some(CodeScope::Everything) {
                check_code(&state, &brake_of(&who), request.principal_code).await?;
            }
            // The boxes are the principal's to tick: whatever the form sent, none.
            NewProfile {
                name: request.name,
                color: request.color,
                restricted: false,
                max_age: None,
                rights: Rights::default(),
            }
        }
        Role::Principal | Role::Restricted => NewProfile {
            name: request.name,
            color: request.color,
            restricted,
            max_age,
            rights: request.rights.map(rights_of).unwrap_or_default(),
        },
    };
    // « Le code, à créer »: the first restricted profile and the code it needs, in one window.
    // Only judged when there is no code: with one already there the two fields are ignored,
    // so they cannot be refused for a mismatch nothing would have used.
    let first_code = match (role, restricted && code.is_none()) {
        (Role::Principal, true) => match (request.code, request.code_again) {
            (Some(code), Some(again)) => {
                twice(&code, &again)?;
                Some(code)
            }
            (None, None) => None,
            _ => return Err(say(StatusCode::BAD_REQUEST, "the code is typed twice")),
        },
        _ => None,
    };
    let profiles = Arc::clone(&state.profiles);
    let made =
        blocking(move || profiles.create_with_code(new, first_code.as_deref(), now_in_millis()))
            .await?;
    match made {
        Ok(profile) => Ok((StatusCode::CREATED, Json(profile_dto(profile)))),
        Err(refusal) => Err(refused(refusal)),
    }
}

/// The principal changes anything of any profile but the principal's kind; a reader its own
/// name and colour; a restricted profile nothing.
pub async fn patch_profile(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(request): Json<ProfilePatchDto>,
) -> Result<Json<ProfileDto>, Answer> {
    let patch = match role(&who) {
        Role::Principal => ProfilePatch {
            name: request.name,
            color: request.color,
            restricted: request.kind.as_deref().map(restricted_from).transpose()?,
            max_age: age_from(request.max_age.as_ref())?,
            rights: request.rights.map(rights_of),
        },
        Role::Reader if matches!(&who, Who::Device(_, own) if own.id == id) => {
            if request.kind.is_some() || request.max_age.is_some() || request.rights.is_some() {
                return Err(say(
                    StatusCode::FORBIDDEN,
                    "a profile changes only its own name and colour",
                ));
            }
            ProfilePatch {
                name: request.name,
                color: request.color,
                ..ProfilePatch::default()
            }
        }
        Role::Reader | Role::Restricted => {
            return Err(say(
                StatusCode::FORBIDDEN,
                "only the principal profile may do this",
            ))
        }
    };
    let profiles = Arc::clone(&state.profiles);
    match blocking(move || profiles.update(&id, patch)).await? {
        Ok(profile) => Ok(Json(profile_dto(profile))),
        Err(refusal) => Err(refused(refusal)),
    }
}

pub async fn delete_profile(
    Principal(_): Principal,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Answer> {
    let profiles = Arc::clone(&state.profiles);
    match blocking(move || profiles.delete(&id)).await? {
        Ok(()) => Ok(StatusCode::NO_CONTENT),
        Err(refusal) => Err(refused(refusal)),
    }
}

// ------------------------------------------------------------------ the code

/// Makes the code, or changes it, and says what it is for. No old code is asked: the route is
/// the principal's, and a device on the principal has already passed whatever guards it.
pub async fn set_code(
    Principal(_): Principal,
    State(state): State<AppState>,
    Json(request): Json<CodeDto>,
) -> Result<StatusCode, Answer> {
    let scope = match request.scope.as_str() {
        "restricted" => CodeScope::Restricted,
        "everything" => CodeScope::Everything,
        _ => {
            return Err(say(
                StatusCode::BAD_REQUEST,
                "the code is for restricted or everything",
            ))
        }
    };
    twice(&request.code, &request.code_again)?;
    let profiles = Arc::clone(&state.profiles);
    // Hashed before the write by the store itself: Argon2 never holds the library's writer.
    match blocking(move || profiles.set_code(&request.code, scope)).await? {
        Ok(()) => Ok(StatusCode::NO_CONTENT),
        Err(refusal) => Err(refused(refusal)),
    }
}

pub async fn clear_code(
    Principal(_): Principal,
    State(state): State<AppState>,
) -> Result<StatusCode, Answer> {
    let profiles = Arc::clone(&state.profiles);
    match blocking(move || profiles.clear_code()).await? {
        Ok(()) => Ok(StatusCode::NO_CONTENT),
        Err(refusal) => Err(refused(refusal)),
    }
}

// ------------------------------------------------------------------ reading preferences

/// The active profile's reading preferences: an object the server keeps and never reads.
pub async fn get_reading(
    Caller(who): Caller,
    State(state): State<AppState>,
) -> Result<Json<serde_json::Value>, Answer> {
    let profiles = Arc::clone(&state.profiles);
    let profile = who.profile().to_string();
    Ok(Json(blocking(move || profiles.reading(&profile)).await?))
}

pub async fn put_reading(
    Caller(who): Caller,
    State(state): State<AppState>,
    Json(value): Json<serde_json::Value>,
) -> Result<StatusCode, Answer> {
    // An object, so the client that reads it back can always look a key up in it: a stored
    // `[1, 2]` would be preferences every later version has to special-case.
    if !value.is_object() {
        return Err(say(
            StatusCode::BAD_REQUEST,
            "reading preferences are an object",
        ));
    }
    let profiles = Arc::clone(&state.profiles);
    let profile = who.profile().to_string();
    blocking(move || profiles.set_reading(&profile, &value)).await?;
    Ok(StatusCode::NO_CONTENT)
}

// ------------------------------------------------------------------ the devices

fn entry_of(device: Device, here: Option<&str>) -> DeviceEntryDto {
    DeviceEntryDto {
        here: here == Some(device.id.as_str()),
        id: device.id,
        name: device.name,
        seen_at: device.seen_at,
        profile: device.profile_id,
        kept: device.kept,
    }
}

fn here_of(who: &Who) -> Option<String> {
    match who {
        Who::Device(device, _) => Some(device.id.clone()),
        Who::Open | Who::OldKey(_) => None,
    }
}

pub async fn list_devices(
    Principal(who): Principal,
    State(state): State<AppState>,
) -> Result<Json<DevicesDto>, Answer> {
    let devices = Arc::clone(&state.devices);
    let now = now_in_millis();
    let (list, _) = blocking(move || devices.list(now)).await?;
    let here = here_of(&who);
    Ok(Json(DevicesDto {
        devices: list
            .into_iter()
            .map(|device| entry_of(device, here.as_deref()))
            .collect(),
    }))
}

/// The principal seats a device on a profile, and decides whether it keeps it — a restricted
/// one included, which is the one way a child's tablet opens on the child's profile.
pub async fn place_device(
    Principal(who): Principal,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(request): Json<PlacementDto>,
) -> Result<Json<DeviceEntryDto>, Answer> {
    let profiles = Arc::clone(&state.profiles);
    let devices = Arc::clone(&state.devices);
    let now = now_in_millis();
    let placed = blocking(move || {
        if let Err(refusal) = profiles.place(&id, &request.profile, request.kept)? {
            return Ok(Err(refusal));
        }
        let (list, _) = devices.list(now)?;
        Ok(list
            .into_iter()
            .find(|device| device.id == id)
            .ok_or(SwitchRefusal::NoSuchDevice))
    })
    .await?;
    match placed {
        Ok(device) => Ok(Json(entry_of(device, here_of(&who).as_deref()))),
        Err(SwitchRefusal::NoSuchProfile) => Err(say(StatusCode::NOT_FOUND, "no such profile")),
        Err(_) => Err(say(StatusCode::NOT_FOUND, "no such device")),
    }
}

/// Removes a device: its key stops working with this answer, because the door asks the index
/// on every request.
pub async fn remove_device(
    Principal(_): Principal,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Answer> {
    let devices = Arc::clone(&state.devices);
    if blocking(move || devices.unlink(&id)).await? {
        Ok(StatusCode::NO_CONTENT)
    } else {
        Err(say(StatusCode::NOT_FOUND, "no such device"))
    }
}
