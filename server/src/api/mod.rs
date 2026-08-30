//! The endpoints, and the shapes they answer with.

pub mod bulk_import;
pub mod cache_budget;
pub mod dto;
pub mod format;
pub mod intake;
pub mod keys;
pub mod local_drop;
pub mod pages;
pub mod progress;
pub mod records;
pub mod routes;
pub mod throttle;

/// A fault in the request rather than in the server.
///
/// Carried inside an `anyhow::Error` like anything else and pulled back out at the edge —
/// in one place rather than in every handler. A malformed request is
/// the caller's problem, and it has to come back as a 400 carrying JSON rather than as an
/// internal error that says nothing.
#[derive(Debug, thiserror::Error)]
#[error("{0}")]
pub struct Invalid(pub String);

/// A thing that is not there: an import that expired, a series that was renamed. Not a
/// fault in the request, and not a server failure either.
#[derive(Debug, thiserror::Error)]
#[error("{0}")]
pub struct Absent(pub String);

pub fn invalid(what: impl Into<String>) -> anyhow::Error {
    anyhow::Error::new(Invalid(what.into()))
}

pub fn absent(what: impl Into<String>) -> anyhow::Error {
    anyhow::Error::new(Absent(what.into()))
}
