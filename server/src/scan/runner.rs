//! Running a scan behind the request that asked for it.
//!
//! A full library takes tens of seconds, which no client should be asked to hold a
//! connection open for. So `/scan` answers at once and the work goes on behind — and
//! `/scan` asked the other way round says where it got to.
//!
//! One at a time. Two scans in the same transaction would deadlock on the writer; two in
//! sequence would be work done twice. A second request while one is running is told so
//! rather than queued — for `/scan`, whose caller can ask again. A scan that follows an
//! import is different: see `start_or_again`.

use crate::scan::report::Findings;
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
    /// The last report, once there is one — in counts and named lists, because the client
    /// words its own French and a paragraph from here would be the one string its
    /// vocabulary file could not reach.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub report: Option<Findings>,
}

impl Default for ScanStatus {
    fn default() -> Self {
        ScanStatus {
            state: "IDLE",
            started_at: None,
            finished_at: None,
            report: None,
        }
    }
}

#[derive(Default)]
pub struct ScanRunner {
    status: Mutex<ScanStatus>,
    running: AtomicBool,
    /// The one scan asked for while another ran. Also the lock that running is released
    /// under, so a request cannot slip in between the last look and the release.
    again: Mutex<Option<Job>>,
}

type Job = (
    &'static str,
    Box<dyn FnOnce() -> anyhow::Result<ScanReport> + Send>,
);

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
        self.launch(label, Box::new(scan))
    }

    /// Starts a scan, or — when one is going — makes sure another follows it.
    ///
    /// A scan that has already read the library folder does not see what arrives after: an
    /// import committed during one was installed on disk and never indexed, because the
    /// commit's own request was told "already running" and the running one had passed that
    /// folder. So the request is kept, once however many come (fifty commits in a row ask
    /// for one pass, not fifty), and the last function received is the one that runs.
    pub fn start_or_again<F>(self: &Arc<Self>, label: &'static str, scan: F)
    where
        F: FnOnce() -> anyhow::Result<ScanReport> + Send + 'static,
    {
        self.submit((label, Box::new(scan)));
    }

    /// Starts `job`, or keeps it for the scan that is running to follow with.
    fn submit(self: &Arc<Self>, mut job: Job) {
        loop {
            // `launch` hands the job back when it is refused, so nothing is built twice.
            match self.launch_or_return(job) {
                None => return,
                Some(back) => job = back,
            }
            let mut again = self
                .again
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            if self.running.load(Ordering::Acquire) {
                *again = Some(job);
                return;
            }
            // The scan ended between the refusal and the lock: start it ourselves.
        }
    }

    fn launch(
        self: &Arc<Self>,
        label: &'static str,
        scan: Box<dyn FnOnce() -> anyhow::Result<ScanReport> + Send>,
    ) -> bool {
        self.launch_or_return((label, scan)).is_none()
    }

    fn launch_or_return(self: &Arc<Self>, job: Job) -> Option<Job> {
        let (label, scan) = job;
        if self
            .running
            .compare_exchange(false, true, Ordering::AcqRel, Ordering::Acquire)
            .is_err()
        {
            return Some((label, scan));
        }

        let started_at = now();
        self.set(ScanStatus {
            state: "RUNNING",
            started_at: Some(started_at),
            finished_at: None,
            report: None,
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
                let _release = Release(&runner, std::cell::Cell::new(false));
                let mut label = label;
                let mut scan = scan;
                let mut started_at = started_at;
                loop {
                    let found = match scan() {
                        Ok(report) => {
                            tracing::info!(
                                label,
                                seconds = (now() - started_at) / 1000,
                                "scan finished"
                            );
                            // The paragraph still goes to the log, where a paragraph belongs.
                            tracing::info!(label, "{}", report.summary());
                            report.findings()
                        }
                        Err(e) => {
                            tracing::error!(label, error = format!("{e:#}"), "scan failed");
                            // The failure reaches the client rather than only the log: a scan
                            // that quietly did nothing is worse than one that says why.
                            Findings {
                                failure: Some(format!("{e:#}")),
                                ..Findings::default()
                            }
                        }
                    };
                    let mut again = runner
                        .again
                        .lock()
                        .unwrap_or_else(std::sync::PoisonError::into_inner);
                    if let Some((next_label, next)) = again.take() {
                        // Still RUNNING for whoever polls: DONE here would end their wait
                        // before the pass that matters.
                        started_at = now();
                        runner.set(ScanStatus {
                            state: "RUNNING",
                            started_at: Some(started_at),
                            finished_at: None,
                            report: None,
                        });
                        label = next_label;
                        scan = next;
                        continue;
                    }
                    runner.set(ScanStatus {
                        state: "DONE",
                        started_at: Some(started_at),
                        finished_at: Some(now()),
                        report: Some(found),
                    });
                    // Marked first, still under the lock of the kept request: the destructor
                    // must find the flag already given back, not give it back again.
                    _release.1.set(true);
                    runner.running.store(false, Ordering::Release);
                    break;
                }
            })
            .expect("spawning the scan thread");

        None
    }

    fn set(&self, status: ScanStatus) {
        if let Ok(mut held) = self.status.lock() {
            *held = status;
        }
    }
}

/// Hands the runner back, on the way out or on the way down.
///
/// The normal way out releases the flag itself, under the lock of the kept request, and
/// says so here: released a second time by this destructor, a scan started in between had
/// its flag cleared under it and a third could run beside it.
struct Release<'a>(&'a Arc<ScanRunner>, std::cell::Cell<bool>);

impl Drop for Release<'_> {
    fn drop(&mut self) {
        if std::thread::panicking() {
            // A scan that died leaves a status saying so, or the last DONE would stand and
            // read as if this one had finished.
            self.0.set(ScanStatus {
                state: "DONE",
                started_at: None,
                finished_at: Some(now()),
                report: Some(Findings {
                    failure: Some("the scan thread died".to_string()),
                    ..Findings::default()
                }),
            });
            tracing::error!("the scan thread panicked");
        }
        if !self.1.get() {
            // Only a scan that died gets here still holding the flag.
            let kept = {
                let mut again = self
                    .0
                    .again
                    .lock()
                    .unwrap_or_else(std::sync::PoisonError::into_inner);
                self.0.running.store(false, Ordering::Release);
                again.take()
            };
            // The scan asked for while this one ran is still owed: the import it follows
            // was installed on disk, and left unrun it stays out of the index until the
            // next scan happens to start.
            if let Some(job) = kept {
                self.0.submit(job);
            }
        }
    }
}

fn now() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}
