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
use std::sync::Mutex;
use std::time::{Duration, Instant};

/// The most addresses kept on the books at once.
///
/// A household never approaches it. What does is the case this exists for: behind a trusted
/// proxy the address is a header the caller writes, so an attacker rotating it would
/// otherwise add a record per request for ever. Past the cap the least interesting records
/// go — never a blocked one, and the oldest first.
const MAX_ADDRESSES: usize = 1024;

pub struct Throttle {
    max_failures: usize,
    window: Duration,
    block: Duration,
    records: Mutex<HashMap<String, Record>>,
}

#[derive(Default)]
struct Record {
    failures: VecDeque<Instant>,
    blocked_until: Option<Instant>,
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
            max_failures,
            window,
            block,
            records: Mutex::new(HashMap::new()),
        }
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
        let now = Instant::now();
        if records.len() >= MAX_ADDRESSES && !records.contains_key(address) {
            self.make_room(&mut records, now);
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
                "presented wrong keys: refused"
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
            r.blocked_until.is_some_and(|until| until > now)
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
                    r.blocked_until.is_some(),
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
            "too many addresses presenting wrong keys — the oldest are forgotten"
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
}
