//! Slows down whoever keeps presenting a key that does not work.
//!
//! A 32-byte key is not guessable, so this is not what stands between the library and an
//! attacker — the key is. But an endpoint that answers unlimited attempts at full speed is
//! an invitation, it makes a mistake indistinguishable from an attack in the log, and it
//! costs nothing to close.
//!
//! Failures are counted per address and forgotten as they age out, so a device that gets it
//! wrong once on Monday is not one attempt closer to being locked out on Friday.
//!
//! And the record of an address is forgotten with them. What is counted is whatever the
//! request appears to come from, which behind a trusted proxy is a header the caller writes:
//! remembering every distinct value for ever would turn the defence into a way of filling
//! the server's memory one request at a time.

use std::collections::{HashMap, VecDeque};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

/// The most addresses kept on the books at once.
///
/// A household never approaches it. What does is the case this exists for: behind a trusted
/// proxy the address is a header the caller writes, so an attacker rotating it would
/// otherwise add a record per request for ever. Past the cap the least interesting records
/// go — never a blocked one, and the oldest first.
const MAX_ADDRESSES: usize = 1024;

pub struct Throttle {
    /// What is being guessed, for the log: « wrong keys », « wrong codes ». Every block was
    /// logged as keys, and a child guessing the household's code read in the journal as a
    /// device with a broken key.
    what: &'static str,
    max_failures: usize,
    window: Duration,
    block: Duration,
    records: Mutex<HashMap<String, Record>>,
}

#[derive(Default)]
struct Record {
    failures: VecDeque<Instant>,
    blocked_until: Option<Instant>,
    /// Attempts let through and not yet answered. See [`Throttle::attempt`].
    in_flight: usize,
}

impl Default for Throttle {
    fn default() -> Self {
        Throttle::new(
            10,
            Duration::from_secs(5 * 60),
            Duration::from_secs(15 * 60),
        )
    }
}

impl Throttle {
    pub fn new(max_failures: usize, window: Duration, block: Duration) -> Self {
        Throttle {
            what: "wrong keys",
            max_failures,
            window,
            block,
            records: Mutex::new(HashMap::new()),
        }
    }

    /// The same brake, logged as guessing `what`.
    pub fn counting(mut self, what: &'static str) -> Self {
        self.what = what;
        self
    }

    /// How long the caller must wait, or nothing when it may proceed.
    pub fn blocked_for(&self, address: &str) -> Option<Duration> {
        let records = self.records.lock().ok()?;
        let until = records.get(address)?.blocked_until?;
        until.checked_duration_since(Instant::now())
    }

    pub fn record_failure(&self, address: &str) {
        let Ok(mut records) = self.records.lock() else {
            return;
        };
        self.fail_locked(&mut records, address);
    }

    fn fail_locked(&self, records: &mut HashMap<String, Record>, address: &str) {
        let now = Instant::now();
        if records.len() >= MAX_ADDRESSES && !records.contains_key(address) {
            self.make_room(records, now);
        }

        let record = records.entry(address.to_string()).or_default();
        record.failures.push_back(now);
        while record
            .failures
            .front()
            .is_some_and(|first| now.duration_since(*first) > self.window)
        {
            record.failures.pop_front();
        }
        if record.failures.len() >= self.max_failures {
            record.blocked_until = Some(now + self.block);
            record.failures.clear();
            tracing::warn!(
                address,
                attempts = self.max_failures,
                minutes = self.block.as_secs() / 60,
                "presented {}: refused",
                self.what
            );
        }
    }

    /// Drops what is no longer worth remembering, and then, if that was not enough, the
    /// least recent of what is left.
    ///
    /// Sweeping the stale ones alone does not bound anything: under an attack every record
    /// is fresh, which is precisely when the list must stop growing.
    fn make_room(&self, records: &mut HashMap<String, Record>, now: Instant) {
        records.retain(|_, r| {
            r.in_flight > 0
                || r.blocked_until.is_some_and(|until| until > now)
                || r.failures
                    .back()
                    .is_some_and(|last| now.duration_since(*last) <= self.window)
        });
        if records.len() < MAX_ADDRESSES {
            return;
        }
        // A block is the thing actually being enforced, so it outlives a bare count.
        let mut ages: Vec<(bool, Instant, String)> = records
            .iter()
            .map(|(address, r)| {
                (
                    r.blocked_until.is_some() || r.in_flight > 0,
                    r.failures.back().copied().unwrap_or(now),
                    address.clone(),
                )
            })
            .collect();
        ages.sort_by(|a, b| a.0.cmp(&b.0).then(a.1.cmp(&b.1)));
        for (_, _, address) in ages.into_iter().take(records.len() - MAX_ADDRESSES / 2) {
            records.remove(&address);
        }
        tracing::warn!(
            kept = records.len(),
            "too many callers presenting {} — the oldest are forgotten",
            self.what
        );
    }

    /// How many addresses are on the books.
    ///
    /// Exposed so a test can state that the list does not grow without bound — which is the
    /// only thing about it that is not self-evident.
    pub fn remembered(&self) -> usize {
        self.records.lock().map(|r| r.len()).unwrap_or(0)
    }

    /// A key that works clears the slate: the device was simply misconfigured.
    pub fn record_success(&self, address: &str) {
        if let Ok(mut records) = self.records.lock() {
            records.remove(address);
        }
    }

    /// Lets one guess through, or says how long to wait.
    ///
    /// For a guess that takes time to judge — the household's code, behind Argon2. Asking
    /// [`Throttle::blocked_for`] first and recording the outcome after was a check and an act
    /// a quarter of a second apart: fifteen wrong codes sent at once all passed the check
    /// before the first was recorded, and a right one among them wiped the block the others
    /// had just earned. So the attempt is counted **when it is let through**, under the same
    /// lock as the check: guesses still in flight count against the ten as if they had
    /// already failed, and the eleventh is refused before it costs a hash.
    ///
    /// Reserving was chosen over one check at a time per caller: that would also hold the
    /// line, but it would refuse a person who pressed Entrée twice, and it changes what the
    /// brake promises — ten wrong codes — into something else.
    ///
    /// The attempt is settled by [`Attempt::failed`], [`Attempt::succeeded`] or
    /// [`Attempt::void`]; one dropped unsettled counts as failed, so a request abandoned
    /// mid-hash — a client hanging up — neither keeps its slot nor escapes the count.
    pub fn attempt(self: &Arc<Self>, key: &str) -> Result<Attempt, std::time::Duration> {
        let Ok(mut records) = self.records.lock() else {
            // A poisoned lock is a panic elsewhere; refusing is the side that holds.
            return Err(self.block);
        };
        let now = Instant::now();
        if records.len() >= MAX_ADDRESSES && !records.contains_key(key) {
            self.make_room(&mut records, now);
        }
        let record = records.entry(key.to_string()).or_default();
        if let Some(wait) = record
            .blocked_until
            .and_then(|until| until.checked_duration_since(now))
        {
            return Err(wait);
        }
        while record
            .failures
            .front()
            .is_some_and(|first| now.duration_since(*first) > self.window)
        {
            record.failures.pop_front();
        }
        if record.failures.len() + record.in_flight >= self.max_failures {
            // Not blocked yet: the guesses in flight have not all failed. Asked again in a
            // moment, the answer is either a block or room.
            return Err(Duration::from_secs(1));
        }
        record.in_flight += 1;
        Ok(Attempt {
            throttle: Arc::clone(self),
            key: key.to_string(),
            settled: false,
        })
    }

    fn settle(&self, key: &str, outcome: Outcome) {
        let Ok(mut records) = self.records.lock() else {
            return;
        };
        let Some(record) = records.get_mut(key) else {
            return;
        };
        record.in_flight = record.in_flight.saturating_sub(1);
        match outcome {
            Outcome::Failed => self.fail_locked(&mut records, key),
            // A right guess clears the count, never a block: a block earned by the guesses
            // around it would otherwise be wiped by the one among them that was right.
            Outcome::Succeeded => {
                let blocked = record
                    .blocked_until
                    .is_some_and(|until| until > Instant::now());
                if !blocked {
                    record.failures.clear();
                    record.blocked_until = None;
                }
            }
            Outcome::Void => {}
        }
    }
}

enum Outcome {
    Failed,
    Succeeded,
    /// Nothing was judged: the request turned out not to need the guess.
    Void,
}

/// A guess let through by [`Throttle::attempt`], counted until it is settled.
pub struct Attempt {
    throttle: Arc<Throttle>,
    key: String,
    settled: bool,
}

impl Attempt {
    pub fn failed(mut self) {
        self.settle(Outcome::Failed);
    }

    pub fn succeeded(mut self) {
        self.settle(Outcome::Succeeded);
    }

    /// Gives the slot back uncounted, for a request that judged nothing after all.
    pub fn void(mut self) {
        self.settle(Outcome::Void);
    }

    fn settle(&mut self, outcome: Outcome) {
        self.settled = true;
        self.throttle.settle(&self.key, outcome);
    }
}

impl Drop for Attempt {
    fn drop(&mut self) {
        if !self.settled {
            self.settle(Outcome::Failed);
        }
    }
}
