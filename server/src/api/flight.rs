//! One piece of work per key at a time, and the others wait for its result.
//!
//! The reader asks for the next two screens at the very moment the server warms the next
//! four pages, so on every page turn the same page was likely decoded and encoded twice at
//! once — on a server with two warming workers, the second copy is what the reader waits
//! behind. Whoever takes off first does the work; whoever arrives while it is in the air
//! waits for it to land and then reads its result from the cache.
//!
//! Waiting happens on the blocking pool, never on the async runtime: every caller is
//! already inside `spawn_blocking`.

use std::collections::HashSet;
use std::sync::{Condvar, Mutex, MutexGuard};
use std::time::{Duration, Instant};

/// How long a caller waits for someone else's work before doing it itself.
///
/// Bounded, because what it waits on can be slow for reasons of its own — a strip of fifty
/// thousand rows, a busy disk — and a reader is better served by a second copy of the work
/// than by a wait with no end.
pub const PATIENCE: Duration = Duration::from_secs(5);

#[derive(Default)]
pub struct Flights {
    airborne: Mutex<HashSet<String>>,
    landed: Condvar,
}

/// What asking to take off came to.
pub enum Takeoff<'a> {
    /// Nobody else was doing it: do it, and the guard lands it when dropped.
    Mine(Flight<'a>),
    /// Someone else was, and has finished — or given up, panic included. Look for the
    /// result; if there is none, the work is yours to do.
    Landed,
    /// Someone else still is, and the wait ran out.
    TimedOut,
    /// Someone else is, and the caller said it would rather not wait.
    Busy,
}

/// Work in the air. Dropped — at the end of the work, or by a panic unwinding through it —
/// it lands, and everyone waiting for it is woken.
pub struct Flight<'a> {
    flights: &'a Flights,
    key: String,
}

impl Drop for Flight<'_> {
    fn drop(&mut self) {
        self.flights.lock().remove(&self.key);
        self.flights.landed.notify_all();
    }
}

impl Flights {
    /// A panic elsewhere while the set was held leaves it as it was: a set of keys has no
    /// invariant a half-finished insert could break.
    fn lock(&self) -> MutexGuard<'_, HashSet<String>> {
        self.airborne.lock().unwrap_or_else(|e| e.into_inner())
    }

    /// Takes off with `key`, or waits up to `patience` for whoever already has. A caller
    /// that would rather skip than wait — the warming, which has other pages to prepare —
    /// passes no patience and is told `Busy`.
    pub fn take_off(&self, key: &str, patience: Option<Duration>) -> Takeoff<'_> {
        let mut airborne = self.lock();
        if airborne.insert(key.to_string()) {
            return Takeoff::Mine(Flight {
                flights: self,
                key: key.to_string(),
            });
        }
        let Some(patience) = patience else {
            return Takeoff::Busy;
        };
        let deadline = Instant::now() + patience;
        while airborne.contains(key) {
            let left = deadline.saturating_duration_since(Instant::now());
            if left.is_zero() {
                return Takeoff::TimedOut;
            }
            airborne = self
                .landed
                .wait_timeout(airborne, left)
                .unwrap_or_else(|e| e.into_inner())
                .0;
        }
        Takeoff::Landed
    }

    /// Takes off with `key` if nobody has it, without waiting.
    pub fn try_take_off(&self, key: &str) -> Option<Flight<'_>> {
        match self.take_off(key, None) {
            Takeoff::Mine(flight) => Some(flight),
            _ => None,
        }
    }
}
