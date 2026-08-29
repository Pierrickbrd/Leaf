//! Running a scan behind the request that asked for it.
//!
//! A full library takes tens of seconds, which no client should be asked to hold a
//! connection open for. So `/scan` answers at once and the work goes on behind — and
//! `/scan` asked the other way round says where it got to.
//!
//! One at a time. Two scans in the same transaction would deadlock on the writer; two in
//! sequence would be work done twice. A second request while one is running is told so
//! rather than queued: there is nothing to queue, since the second would read the same disk.

use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};

use serde::Serialize;

use crate::scan::report::ScanReport;

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScanStatus {
    /// IDLE, RUNNING or DONE.
    pub state: &'static str,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub started_at: Option<i64>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub finished_at: Option<i64>,
    /// The last report, once there is one.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub summary: Option<String>,
}

impl Default for ScanStatus {
    fn default() -> Self {
        ScanStatus {
            state: "IDLE",
            started_at: None,
            finished_at: None,
            summary: None,
        }
    }
}

#[derive(Default)]
pub struct ScanRunner {
    status: Mutex<ScanStatus>,
    running: AtomicBool,
}

impl ScanRunner {
    pub fn status(&self) -> ScanStatus {
        self.status
            .lock()
            .map(|s| s.clone())
            .unwrap_or_else(|_| ScanStatus::default())
    }

    /// Starts a scan, or says that one is already going.
    ///
    /// Returns false when refused, which is what turns into a 409 — a client that asked
    /// twice learns that its first request is still working rather than that nothing
    /// happened.
    pub fn start<F>(self: &Arc<Self>, label: &'static str, scan: F) -> bool
    where
        F: FnOnce() -> anyhow::Result<ScanReport> + Send + 'static,
    {
        if self
            .running
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_err()
        {
            return false;
        }

        let started_at = now();
        self.set(ScanStatus {
            state: "RUNNING",
            started_at: Some(started_at),
            finished_at: None,
            summary: None,
        });

        let runner = Arc::clone(self);
        std::thread::Builder::new()
            .name("leaf-scan".into())
            .spawn(move || {
                // The flag comes back on the way out, whatever the way out is. Released at
                // the end of the closure instead, a panic anywhere in the scanner left it
                // set for the life of the process: /scan answered RUNNING for ever and no
                // scan could be started again. A destructor is the only thing an unwinding
                // thread still runs.
                let _release = Release(&runner);
                let summary = match scan() {
                    Ok(report) => {
                        tracing::info!(
                            label,
                            seconds = (now() - started_at) / 1000,
                            "scan finished"
                        );
                        report.summary()
                    }
                    Err(e) => {
                        tracing::error!(label, error = format!("{e:#}"), "scan failed");
                        // The failure reaches the client rather than only the log: a scan
                        // that quietly did nothing is worse than one that says why.
                        format!("failed: {e:#}")
                    }
                };
                runner.set(ScanStatus {
                    state: "DONE",
                    started_at: Some(started_at),
                    finished_at: Some(now()),
                    summary: Some(summary),
                });
            })
            .expect("spawning the scan thread");

        true
    }

    fn set(&self, status: ScanStatus) {
        if let Ok(mut held) = self.status.lock() {
            *held = status;
        }
    }
}

/// Hands the runner back, on the way out or on the way down.
struct Release<'a>(&'a Arc<ScanRunner>);

impl Drop for Release<'_> {
    fn drop(&mut self) {
        if std::thread::panicking() {
            // A scan that died leaves a status saying so, or the last DONE would stand and
            // read as if this one had finished.
            self.0.set(ScanStatus {
                state: "DONE",
                started_at: None,
                finished_at: Some(now()),
                summary: Some("failed: the scan thread died".to_string()),
            });
            tracing::error!("the scan thread panicked");
        }
        self.0.running.store(false, Ordering::Release);
    }
}

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}
