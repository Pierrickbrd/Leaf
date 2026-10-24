//! Linking: a code typed on a new device is exchanged for a key of its own, and the device
//! that holds the key may rename itself or leave.
//!
//! `POST /link` is the one route besides `/health` that answers without a key — the
//! caller has none yet, it is here to get one. What stands in for the key is the code, and
//! the throttle: a code nobody can guess in ten minutes is still an endpoint answering
//! attempts, and the attempts are counted the way wrong keys are.
//!
//! Every other route here is the principal profile's: linking another device, renaming this
//! one or unlinking it lives in the Devices tab, which nobody else is shown. A reader's
//! tablet that could mint a code would link a device sitting on the principal, which is
//! every right the reader was not given.

use std::sync::Arc;

use axum::extract::{FromRequestParts, Path, State};
use axum::http::request::Parts;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::Json;

use super::dto::{
    CodeStateDto, DeviceDto, DeviceRenameDto, ErrorDto, IssuedCodeDto, LinkRequest, LinkedDto,
    NameTakenDto,
};
use super::keys::{Key, Permission};
use super::routes::{blocking, caller, now_in_millis, AppState, Failure, Principal, Refused, Who};
use crate::net::link::Relay;
use crate::store::devices::{Creator, Device, Refusal};

/// What the old-key callers are told when a route is about a device and they are not one.
const FROM_LEAF_KEYS: &str = "this key comes from LEAF_KEYS: rename or remove it in server.env";

/// The address a request is counted against, for the one route that has no key to count.
pub struct Address(String);

impl FromRequestParts<AppState> for Address {
    type Rejection = std::convert::Infallible;
    async fn from_request_parts(
        parts: &mut Parts,
        state: &AppState,
    ) -> Result<Self, Self::Rejection> {
        Ok(Address(caller(parts, state.trust_proxy)))
    }
}

/// Everything a linking route can answer besides its success.
pub enum Answer {
    Refused(Refused),
    Failure(Failure),
    /// A refusal with its own status and body.
    Said(Box<Response>),
}

impl From<Failure> for Answer {
    fn from(failure: Failure) -> Self {
        Answer::Failure(failure)
    }
}

impl From<Refused> for Answer {
    fn from(refused: Refused) -> Self {
        Answer::Refused(refused)
    }
}

impl IntoResponse for Answer {
    fn into_response(self) -> Response {
        match self {
            Answer::Refused(refused) => refused.into_response(),
            Answer::Failure(failure) => failure.into_response(),
            Answer::Said(response) => *response,
        }
    }
}

pub(super) fn say(status: StatusCode, message: &str) -> Answer {
    Answer::Said(Box::new(
        (status, Json(ErrorDto::new(message))).into_response(),
    ))
}

fn name_taken(holder: String) -> Answer {
    let status = StatusCode::CONFLICT;
    let body = NameTakenDto {
        error: "name",
        name: holder,
    };
    Answer::Said(Box::new((status, Json(body)).into_response()))
}

/// What a bad name is told, shared by the two routes that take one.
fn bad_name(refusal: Refusal) -> Answer {
    match refusal {
        Refusal::NameTaken(holder) => name_taken(holder),
        Refusal::NameEmpty => say(StatusCode::BAD_REQUEST, "a name cannot be empty"),
        Refusal::NameTooLong => say(StatusCode::BAD_REQUEST, "a name is 40 characters at most"),
        Refusal::NameInvalid => say(
            StatusCode::BAD_REQUEST,
            "a name cannot hold a control character",
        ),
        Refusal::NoSuchDevice | Refusal::CodeNotValid => {
            say(StatusCode::NOT_FOUND, "no such device")
        }
    }
}

/// The device behind a request, for the routes that are about one.
fn a_device(who: Who) -> Result<Device, Answer> {
    match who {
        Who::Device(device, _) => Ok(device),
        Who::OldKey(_) => Err(say(StatusCode::CONFLICT, FROM_LEAF_KEYS)),
        // No device exists while the library is open, so this caller is nobody's.
        Who::Open => Err(say(
            StatusCode::FORBIDDEN,
            "no device is linked to this library yet",
        )),
    }
}

fn carries_every_right(key: &Key) -> bool {
    [Permission::Read, Permission::Import, Permission::Delete]
        .iter()
        .all(|right| key.permissions.contains(right))
}

/// Exchanges a code for a key.
pub async fn link(
    State(state): State<AppState>,
    Address(address): Address,
    Json(request): Json<LinkRequest>,
) -> Result<(StatusCode, Json<LinkedDto>), Answer> {
    if let Some(wait) = state.link_throttle.blocked_for(&address) {
        return Err(Refused::TooMany(wait.as_secs().max(1)).into());
    }
    let devices = Arc::clone(&state.devices);
    let now = now_in_millis();
    let outcome = blocking(move || devices.exchange(&request.code, &request.name, now)).await?;
    match outcome {
        Ok(linked) => {
            state.link_throttle.record_success(&address);
            Ok((
                StatusCode::CREATED,
                Json(LinkedDto {
                    key: linked.key,
                    device: DeviceDto {
                        id: linked.device.id,
                        name: linked.device.name,
                    },
                }),
            ))
        }
        // Something was guessed — a code, or a name that cannot be one — and it is counted.
        Err(Refusal::CodeNotValid) => {
            state.link_throttle.record_failure(&address);
            Err(say(StatusCode::GONE, "code"))
        }
        Err(refusal @ (Refusal::NameEmpty | Refusal::NameTooLong | Refusal::NameInvalid)) => {
            state.link_throttle.record_failure(&address);
            Err(bad_name(refusal))
        }
        // Not counted: the name is only told to someone who held a valid code.
        Err(refusal) => Err(bad_name(refusal)),
    }
}

/// A new code, made by a device that is linked (or by an old key, which is how a client
/// linked through `LEAF_KEYS` links its first device), and the link that carries it.
///
/// The link is the server's to build. The client used to build it from its own address, and
/// on the machine that runs the server that address is the loopback: a person there opened
/// « Relier un autre appareil » and got a link no other device could follow, explained by a
/// sentence about 127.0.0.1, tunnels and sudo. Through an SSH tunnel it named the tunnel.
pub async fn issue_code(
    Principal(who): Principal,
    State(state): State<AppState>,
) -> Result<(StatusCode, Json<IssuedCodeDto>), Answer> {
    let maker = match who {
        // Nobody to say who made it, and nobody to hold it against: the first device of an
        // open library is linked from the server's terminal.
        Who::Open => {
            return Err(say(
                StatusCode::FORBIDDEN,
                "an open library issues no codes: link the first device from the server's terminal",
            ))
        }
        Who::Device(device, _) => Maker::Device(device.id),
        // A linked device sits on the principal and carries every right, so a key may only
        // make one if it holds them all: a read-only key from LEAF_KEYS could otherwise mint
        // itself a device that imports and deletes.
        Who::OldKey(key) if !carries_every_right(&key) => {
            return Err(say(
                StatusCode::FORBIDDEN,
                "this key does not carry every right a linked device gets: only a key with read, import and delete may link a device",
            ))
        }
        Who::OldKey(key) => Maker::OldKey(key.name),
    };

    // The address before the code, as `device add` does it: a code made for a link that
    // cannot be built is a code nobody can use, waiting ten minutes in `device list`.
    let Relay::To {
        target,
        local_network,
    } = &*state.relay
    else {
        // Refused rather than answered without a link: the client never asks here — `/health`
        // told it `relay: false` — and a code alone is what the client stopped building
        // links from.
        return Err(say(StatusCode::CONFLICT, "unreachable"));
    };
    let (target, local_network) = (target.clone(), *local_network);

    let devices = Arc::clone(&state.devices);
    let now = now_in_millis();
    let issued = blocking(move || {
        let creator = match &maker {
            Maker::Device(id) => Creator::Device(id),
            Maker::OldKey(name) => Creator::OldKey(name),
        };
        devices.issue(creator, now)
    })
    .await?;
    let link = target.link(&issued.code).to_string();
    Ok((
        StatusCode::CREATED,
        Json(IssuedCodeDto {
            id: issued.id,
            code: issued.code,
            expires_at: issued.expires_at,
            link,
            local_network,
        }),
    ))
}

/// Who a code is made by, owned: it waits for the address to be decided, then crosses to
/// the thread the index is asked on, where `Creator` borrows from it.
enum Maker {
    Device(String),
    OldKey(String),
}

/// Whether a code this device made has been used.
pub async fn follow_code(
    Principal(who): Principal,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<CodeStateDto>, Answer> {
    let device = a_device(who)?;
    let devices = Arc::clone(&state.devices);
    let now = now_in_millis();
    let found = blocking(move || devices.served(&id, &device.id, now)).await?;
    match found {
        None => Err(Failure::Missing("unknown code").into()),
        Some(None) => Ok(Json(CodeStateDto::Waiting)),
        Some(Some(name)) => Ok(Json(CodeStateDto::Served { name })),
    }
}

/// Withdraws a code this device made.
pub async fn cancel_code(
    Principal(who): Principal,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Answer> {
    let device = a_device(who)?;
    let devices = Arc::clone(&state.devices);
    let cancelled = blocking(move || devices.cancel(&id, &device.id)).await?;
    if cancelled {
        Ok(StatusCode::NO_CONTENT)
    } else {
        Err(Failure::Missing("unknown code").into())
    }
}

/// The calling device takes another name.
pub async fn rename_device(
    Principal(who): Principal,
    State(state): State<AppState>,
    Json(request): Json<DeviceRenameDto>,
) -> Result<Json<DeviceDto>, Answer> {
    let device = a_device(who)?;
    let devices = Arc::clone(&state.devices);
    let renamed = blocking(move || devices.rename(&device.id, &request.name)).await?;
    match renamed {
        Ok(device) => Ok(Json(DeviceDto {
            id: device.id,
            name: device.name,
        })),
        Err(refusal) => Err(bad_name(refusal)),
    }
}

/// The calling device leaves: its key stops working with this answer.
pub async fn unlink_device(
    Principal(who): Principal,
    State(state): State<AppState>,
) -> Result<StatusCode, Answer> {
    let device = a_device(who)?;
    let devices = Arc::clone(&state.devices);
    blocking(move || devices.unlink(&device.id)).await?;
    Ok(StatusCode::NO_CONTENT)
}
