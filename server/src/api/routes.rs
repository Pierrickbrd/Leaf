//! The endpoints.
//!
//! Two kinds of key open the door. A linked device's own key carries the rights of the
//! profile it sits on — all of them for the principal, the ticked ones for a reader, none for
//! a restricted profile; an old key from `LEAF_KEYS` keeps exactly the rights written beside
//! it, and is the principal for everything else a profile decides — which is what still
//! makes "import from the desktop only" a rule the server enforces for those keys, rather
//! than a button missing from the mobile interface.
//!
//! A library with neither is open on the loopback, as it always was, and closed everywhere
//! else; the first linked device closes it on the loopback too, or a library behind a proxy
//! on the same machine would stay readable by everyone who reaches the proxy. For good:
//! revoking the last device leaves it closed.
//!
//! The guard is an **extractor**, so it appears in the handler's signature: a route that
//! reads takes a [`Reader`] (or a [`Caller`], when it needs to know who), one that writes
//! takes the guard of its right: [`Importer`], [`Editor`], [`Eraser`], [`Principal`] or
//! [`Exporter`]. A handler missing one is a handler that reads as unguarded — the property a
//! check spelled out on every route would buy, without spelling anything out. Every one of
//! them refuses a device that must choose a profile, but [`Unseated`].

use std::net::SocketAddr;
use std::sync::Arc;

use axum::body::Body;
use axum::extract::{ConnectInfo, FromRequestParts, Path, State};
use axum::http::request::Parts;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{delete, get, patch, post, put};
use axum::{Json, Router};
use http_body_util::BodyExt;

use super::bulk_import::{BulkImport, CleanupRequest, ImportRequest, ReceiveError};
use super::dto::{
    ArcDto, ChapterDto, EntryDto, ErrorDto, FacetsDto, HealthDto, PageDto, ReadingOrderDto,
    SeriesFilter, SeriesPageDto, SeriesSort, SortDirection, UniverseDto, API_VERSION,
    FORMAT_VERSION,
};
use super::erase::Erase;
use super::intake::{Collision, FileRequest, Intake, Proposal};
use super::keys::{Key, Keys, Permission, HEADER};
use super::linking;
use super::local_drop::{DropListing, DropRequest, LocalDrop};
use super::pages::{Pages, Served};
use super::profiles;
use super::progress::{Progress, ProgressDto, ProgressPatch, SeriesProgressPatch, UpNextDto};
use super::records::{EntryPatch, Records, SeriesPatch};
use super::relocate::{MoveRequest, Relocate};
use super::throttle::Throttle;
use crate::metadata::sidecars::ArcJson;
use crate::net::link::Relay;
use crate::scan::layout;
use crate::scan::runner::{ScanRunner, ScanStatus};
use crate::scan::scanner::Scanner;
use crate::store::devices::{Device, Devices};
use crate::store::profiles::{Kind, Profile, Profiles};
use crate::store::repository::Ceiling;
use crate::store::schema::DEFAULT_PROFILE;
use crate::store::{Db, Repository};

/// Enough for any grid, small enough that a thousand series never cross the wire at once.
const DEFAULT_PAGE: i64 = 100;
const MAX_PAGE: i64 = 500;

#[derive(Clone)]
pub struct AppState {
    pub db: Arc<Db>,
    pub keys: Arc<Keys>,
    /// The linked devices, asked on every request so that a removal made by another
    /// process — `leaf-server device remove` in a terminal — holds from the next one.
    pub devices: Arc<Devices>,
    /// The profiles, for the routes that manage them. The profile a device reads as is not
    /// asked here: `Devices::recognise` reads it with the key, in the same statement.
    pub profiles: Arc<Profiles>,
    /// Whether the server listens only on this machine. Only there may a library with no
    /// key and no device be open; see [`AppState::bound_to`].
    pub loopback: bool,
    /// Where a device's relay points, settled once at start (`Reach::settle`): the server
    /// knows its own addresses, and a client only the one it happens to use.
    pub relay: Arc<Relay>,
    pub throttle: Arc<Throttle>,
    /// Wrong codes on `/link`, counted apart: a recognised key wipes `throttle`, and a
    /// caller holding any key could otherwise guess a code, wipe, and guess again for ever.
    pub link_throttle: Arc<Throttle>,
    /// Wrong codes of the household, counted per device and apart from both: a device whose
    /// key is good clears `throttle` with every request, and would otherwise guess the code
    /// that keeps a child in its profile ten at a time, for ever.
    pub code_throttle: Arc<Throttle>,
    pub pages: Arc<Pages>,
    pub scanner: Arc<Scanner>,
    pub runner: Arc<ScanRunner>,
    pub roots: Arc<Vec<std::path::PathBuf>>,
    pub intake: Arc<Intake>,
    pub bulk: Arc<BulkImport>,
    pub drop: Arc<LocalDrop>,
    /// The ceiling on a single upload. Nothing is streamed to disk without one: an
    /// unbounded upload is a way to fill a disk that needs no bug at all, only patience.
    pub max_upload_bytes: u64,
    /// Whether `X-Forwarded-For` may be believed.
    ///
    /// Behind a proxy every request otherwise appears to come from the proxy, and one
    /// misconfigured device would throttle the whole household. Off by default and
    /// deliberately so: trusting the header when nothing sets it lets a caller claim any
    /// address it likes and walk straight past the throttle.
    pub trust_proxy: bool,
}

impl AppState {
    pub fn new(db: Arc<Db>, keys: Keys) -> Self {
        let pages = Pages::new(
            Arc::clone(&db),
            std::env::temp_dir().join("leaf-cache"),
            85,
            4096 * 1024 * 1024,
        );
        AppState::with_pages(db, keys, pages)
    }

    pub fn with_pages(db: Arc<Db>, keys: Keys, pages: Pages) -> Self {
        pages.prepare();
        let pages = Arc::new(pages);
        // A quarter of the cores, and a queue four pages deep per worker: enough to stay
        // ahead of a reader, small enough that falling behind drops the page furthest away
        // rather than growing without bound.
        let workers = (std::thread::available_parallelism()
            .map(|n| n.get() / 4)
            .unwrap_or(2))
        .clamp(2, 4);
        pages.start_warming(workers, workers * 4);
        // Somewhere that exists and belongs to nobody. A state built without an inbox is a
        // state whose write routes are never called — every test that does calls
        // `with_import` and names one.
        let inbox = std::env::temp_dir().join("leaf-inbox");
        let intake = Arc::new(Intake::new(&inbox, Arc::clone(&db)));
        AppState {
            scanner: Arc::new(Scanner::new(Arc::clone(&db), true)),
            runner: Arc::new(ScanRunner::default()),
            roots: Arc::new(Vec::new()),
            bulk: Arc::new(BulkImport::new(
                &inbox,
                std::path::Path::new("library"),
                Arc::clone(&db),
            )),
            drop: Arc::new(LocalDrop::new(None, Arc::clone(&intake))),
            max_upload_bytes: 2048 * 1024 * 1024,
            trust_proxy: false,
            intake,
            devices: Arc::new(Devices::new(Arc::clone(&db))),
            profiles: Arc::new(Profiles::new(Arc::clone(&db))),
            // The safe guess would be `false`, but every state built without saying where it
            // listens is a test's, and the binary always says: `main.rs` calls `bound_to`.
            loopback: true,
            // Nowhere another device can reach, for the same reason: the binary says where
            // it is reached (`reached_at`), and a test that relays says so too.
            relay: Arc::new(Relay::Nowhere),
            db,
            keys: Arc::new(keys),
            throttle: Arc::new(Throttle::default()),
            link_throttle: Arc::new(Throttle::default().counting("wrong link codes")),
            code_throttle: Arc::new(Throttle::default().counting("wrong codes")),
            pages,
        }
    }

    /// Where files arrive, where they end up, and the shared folder that skips the wire.
    pub fn with_import(
        mut self,
        inbox: &std::path::Path,
        library: &std::path::Path,
        drop_folder: Option<std::path::PathBuf>,
        max_upload_bytes: u64,
    ) -> Self {
        let intake = Arc::new(Intake::new(inbox, Arc::clone(&self.db)));
        self.bulk = Arc::new(BulkImport::new(inbox, library, Arc::clone(&self.db)));
        self.drop = Arc::new(LocalDrop::new(drop_folder, Arc::clone(&intake)));
        self.intake = intake;
        self.max_upload_bytes = max_upload_bytes;
        self
    }

    /// Where the server listens, which decides whether a library with no key and no device
    /// is open (the loopback) or closed (anywhere else, where only `/health` answers).
    pub fn bound_to(mut self, host: &str) -> Self {
        self.loopback = crate::boot::is_loopback(host);
        self
    }

    /// Where a device's relay points, as `Reach::settle` decided it.
    pub fn relaying(mut self, relay: Relay) -> Self {
        self.relay = Arc::new(relay);
        self
    }

    /// Believe `X-Forwarded-For`. Only correct when a proxy actually sets it.
    pub fn trusting_proxy(mut self, trust: bool) -> Self {
        if trust {
            tracing::info!("Trusting X-Forwarded-For — only correct when a proxy sets it");
        }
        self.trust_proxy = trust;
        self
    }

    /// The roots a background scan sweeps, and the scanner that does it.
    pub fn with_library(mut self, roots: Vec<std::path::PathBuf>, all_dimensions: bool) -> Self {
        self.scanner = Arc::new(Scanner::new(Arc::clone(&self.db), all_dimensions));
        self.roots = Arc::new(roots);
        self
    }
}

pub fn router(state: AppState) -> Router {
    Router::new()
        .route("/health", get(health))
        // No key: the caller has none yet. The code, and the throttle, stand in for it.
        .route("/link", post(linking::link))
        .route("/link/codes", post(linking::issue_code))
        .route(
            "/link/codes/{id}",
            get(linking::follow_code).delete(linking::cancel_code),
        )
        .route(
            "/device",
            delete(linking::unlink_device).patch(linking::rename_device),
        )
        // The two routes of « Qui lit ? » come first: a device that must choose reaches these
        // and nothing else.
        .route(
            "/profiles",
            get(profiles::list_profiles).post(profiles::create_profile),
        )
        .route("/profiles/active", put(profiles::switch_profile))
        // Reachable by a device that must choose: a forgotten code is found out there.
        .route("/profiles/code/recover", post(profiles::recover_code))
        .route(
            "/profiles/code",
            put(profiles::set_code).delete(profiles::clear_code),
        )
        .route("/profiles/code/recovery", post(profiles::new_recovery))
        .route(
            "/profiles/{id}",
            patch(profiles::patch_profile).delete(profiles::delete_profile),
        )
        .route(
            "/profile/reading",
            get(profiles::get_reading).put(profiles::put_reading),
        )
        .route("/devices", get(profiles::list_devices))
        .route(
            "/devices/{id}",
            patch(profiles::place_device).delete(profiles::remove_device),
        )
        .route("/series", get(list_series))
        .route("/filters", get(get_filters))
        .route("/universes", get(list_universes))
        .route("/universes/{id}/orders", get(list_universe_orders))
        .route("/format", get(get_format))
        .route("/series/{id}/entries", get(list_series_entries))
        .route("/series/{id}/chapters", get(list_series_chapters))
        .route("/entries/{id}/chapters", get(list_entry_chapters))
        .route("/entries/{id}/pages", get(list_entry_pages))
        .route("/entries/{id}/pages/{number}", get(get_page))
        .route("/entries/{id}/cover", get(entry_cover))
        .route("/series/{id}/cover", get(series_cover))
        .route("/entries/{id}/file", get(download_entry_file))
        .route("/scan", get(scan_status).post(start_scan))
        .route(
            "/series/{id}",
            get(get_series).patch(patch_series).delete(erase_series),
        )
        .route("/series/{id}/arcs", get(list_series_arcs).patch(patch_arcs))
        .route(
            "/entries/{id}",
            get(get_entry).patch(patch_entry).delete(erase_entry),
        )
        .route("/drop", get(list_drop).post(take_from_drop))
        .route("/entries", post(receive_entry))
        // A staged file is not an entry: it is a proposal awaiting a confirmation, and
        // /intake/{id} says so. The old spelling shared a shape with /entries/{id}/file,
        // which only ever worked by the router matching the literal segment first.
        .route("/intake/{id}", get(staged_entry).delete(abandon_entry))
        // POST confirms, PUT carries bytes — the same pair the bulk path uses, so that one
        // reading of either explains the other.
        .route(
            "/intake/{id}/file",
            post(file_entry).put(receive_staged_file),
        )
        .route("/preflight", post(preflight))
        .route("/import", get(list_imports).post(open_import))
        .route("/intake", get(list_intake))
        .route("/import/{id}", get(import_state).delete(abandon_import))
        .route("/import/{id}/file", axum::routing::put(receive_import_file))
        .route("/import/{id}/commit", post(commit_import))
        .route("/cleanup", post(cleanup))
        .route("/works/{id}/move", post(move_work))
        .route("/search", get(search))
        .route("/next", get(up_next))
        .route(
            "/series/{id}/progress",
            get(series_progress)
                .patch(record_series_progress)
                .delete(forget_series_progress),
        )
        .route(
            "/series/{id}/reading-direction",
            axum::routing::put(choose_reading_direction),
        )
        .route(
            "/entries/{id}/progress",
            get(entry_progress)
                .patch(record_progress)
                .delete(forget_progress),
        )
        .with_state(state)
}

// -------------------------------------------------------------------- guards

/// Who a request is, once it is past the door.
pub enum Who {
    /// A library with no key and no device, reached on the loopback.
    Open,
    /// A linked device, by its own key, and the profile it sits on — read with the key, in
    /// the same statement, so what the door decides on is one moment's state.
    Device(Device, Profile),
    /// A key from `LEAF_KEYS`, with the rights written beside it.
    OldKey(Key),
}

impl Who {
    /// The profile whose progress this caller reads and writes: the device's own, and the
    /// default one for an old key or an open library, which have no profile to carry.
    pub fn profile(&self) -> &str {
        match self {
            Who::Device(device, _) => &device.profile_id,
            Who::Open | Who::OldKey(_) => DEFAULT_PROFILE,
        }
    }

    /// The oldest rating this caller may see. An old key and an open library are the
    /// principal, and see everything; a device sees what the profile it sits on allows.
    ///
    /// The profile was read with the key, on this request: lowering a child's age from the
    /// parent's laptop holds from the tablet's next request, and asks nothing more of the
    /// index than recognising the tablet already did.
    pub fn ceiling(&self) -> Ceiling {
        let Who::Device(device, profile) = self else {
            return Ceiling::NONE;
        };
        // Held: its restricted profile was deleted under it, and it sits on the principal
        // only to hold its key until somebody chooses. Every route but `/health` refuses it
        // until then; `/health` counts under this, so the number it says is not the whole
        // library's because a parent deleted the child's profile.
        if device.held {
            return Ceiling(Some(0));
        }
        match profile.kind {
            Kind::Restricted => Ceiling(Some(profile.max_age.unwrap_or(0))),
            Kind::Principal | Kind::Reader => Ceiling::NONE,
        }
    }

    /// A device whose profile was deleted under it: it must say who reads before anything
    /// else. It sits on the principal meanwhile only to hold its key.
    fn must_choose(&self) -> bool {
        matches!(self, Who::Device(device, _) if device.choosing || device.held)
    }
}

/// What a route asks of its caller, beyond being recognised.
#[derive(Debug, Clone, Copy)]
enum Need {
    Read,
    Import,
    Edit,
    Delete,
    /// The principal profile: scanning, cleaning up, linking and the devices.
    Principal,
    /// The whole file of a volume: any profile but a restricted one, whose limit and code a
    /// file outside Leaf would escape.
    Export,
}

/// Whether a route waits for a device that must choose a profile to have chosen.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum Seating {
    Required,
    /// Only the routes that let it choose: who reads, and the change of profile.
    Waived,
}

/// Any recognised caller, and who it is — for the routes whose answer depends on it.
pub struct Caller(pub Who);

/// Any caller the door lets through: a key, or none while the library is open.
pub struct Reader;

/// A caller whose profile — or old key — carries the import right.
pub struct Importer(pub Who);

/// A caller whose profile carries the edit right. An old key has no such right of its own:
/// it edits with the import right, which is what it always did.
pub struct Editor(pub Who);

/// A caller whose profile carries the delete right, which no old key has unless it says so.
pub struct Eraser(pub Who);

/// The principal profile, an open library, or an old key that may import — the keys that
/// could already scan before profiles existed.
pub struct Principal(pub Who);

/// Any profile but a restricted one; an old key that may read.
pub struct Exporter(pub Who);

/// Any recognised caller, including a device that must still choose who reads. Only for the
/// routes of « Qui lit ? »: every other guard refuses such a device.
pub struct Unseated(pub Who);

/// Each guard is the same three words — which right, whether seated, what it carries — so
/// they are written once, and a new one cannot forget the seat.
macro_rules! guard {
    ($name:ident, $need:expr, $seating:expr, $wrap:expr) => {
        impl FromRequestParts<AppState> for $name {
            type Rejection = Refused;
            async fn from_request_parts(
                parts: &mut Parts,
                state: &AppState,
            ) -> Result<Self, Refused> {
                allowed(parts, state, $need, $seating).await.map($wrap)
            }
        }
    };
}

guard!(Caller, Need::Read, Seating::Required, Caller);
guard!(Reader, Need::Read, Seating::Required, |_| Reader);
guard!(Importer, Need::Import, Seating::Required, Importer);
guard!(Editor, Need::Edit, Seating::Required, Editor);
guard!(Eraser, Need::Delete, Seating::Required, Eraser);
guard!(Principal, Need::Principal, Seating::Required, Principal);
guard!(Exporter, Need::Export, Seating::Required, Exporter);
guard!(Unseated, Need::Read, Seating::Waived, Unseated);

/// Whether a request offered a key the door recognises — for `/health`, which answers
/// everyone and says a little more to a caller it knows. Never a refusal: a key that is
/// wrong, or an address the throttle holds back, is simply not recognised. Offering no key
/// asks nothing, so an open library is not recognising anybody.
///
/// Who it is when it is: `/health` counts the library under that caller's ceiling. A device
/// that must choose a profile is recognised here like any other: `/health` is how a client
/// learns the server is there at all, and a 409 there would read as a server that is not.
pub struct Recognised(pub Option<Who>);

impl FromRequestParts<AppState> for Recognised {
    type Rejection = Failure;
    async fn from_request_parts(parts: &mut Parts, state: &AppState) -> Result<Self, Failure> {
        let offered = parts
            .headers
            .get(HEADER)
            .is_some_and(|v| !v.as_bytes().trim_ascii().is_empty());
        if !offered {
            return Ok(Recognised(None));
        }
        match door(parts, state).await {
            Ok(who) => Ok(Recognised(Some(who))),
            Err(Refused::Failed(failure)) => Err(failure),
            Err(Refused::Forbidden(_) | Refused::TooMany(_) | Refused::Choose) => {
                Ok(Recognised(None))
            }
        }
    }
}

/// The door, then the seat, then the right — in that order, so a device that must choose is
/// told so whatever it asked, rather than told it lacks a right of a profile it is not on.
async fn allowed(
    parts: &Parts,
    state: &AppState,
    need: Need,
    seating: Seating,
) -> Result<Who, Refused> {
    let who = door(parts, state).await?;
    if seating == Seating::Required && who.must_choose() {
        return Err(Refused::Choose);
    }
    grant(&who, need)?;
    Ok(who)
}

/// Whether a recognised caller carries what the route needs. Never counted by the throttle:
/// a key that works asking for a right it has not got is guessing nothing.
fn grant(who: &Who, need: Need) -> Result<(), Refused> {
    match who {
        Who::Open => Ok(()),
        Who::OldKey(key) => {
            // An old key keeps exactly the rights written beside it. Editing and the
            // principal's routes were the import right's before profiles, and stay so.
            let permission = match need {
                Need::Read | Need::Export => Permission::Read,
                Need::Import | Need::Edit | Need::Principal => Permission::Import,
                Need::Delete => Permission::Delete,
            };
            if key.permissions.contains(&permission) {
                return Ok(());
            }
            Err(Refused::Forbidden(format!(
                "this key does not carry the \"{}\" right",
                permission.name()
            )))
        }
        Who::Device(_, profile) => {
            let rights = profile.rights;
            let (carried, refusal) = match need {
                Need::Read => (true, String::new()),
                Need::Import => (rights.import, lacking("import")),
                Need::Edit => (rights.edit, lacking("edit")),
                Need::Delete => (rights.delete, lacking("delete")),
                Need::Principal => (
                    profile.kind == Kind::Principal,
                    "only the principal profile may do this".to_string(),
                ),
                Need::Export => (
                    profile.kind != Kind::Restricted,
                    "a restricted profile cannot take the whole file".to_string(),
                ),
            };
            if carried {
                Ok(())
            } else {
                Err(Refused::Forbidden(refusal))
            }
        }
    }
}

/// Whether a caller is the principal, by the same rule [`Principal`] applies: the routes that
/// split a row of the table between the principal and the others ask this rather than
/// restate it, so the two cannot come apart.
pub(super) fn is_principal(who: &Who) -> bool {
    grant(who, Need::Principal).is_ok()
}

fn lacking(right: &str) -> String {
    format!("this profile does not carry the \"{right}\" right")
}

/// Who is at the door, without asking what they want: a key the server knows, a device, or
/// nobody while the library is open.
async fn door(parts: &Parts, state: &AppState) -> Result<Who, Refused> {
    // The bytes, decoded as UTF-8. `to_str` admits visible ASCII and nothing else, so a
    // secret with an accent in it would be configurable — `Keys::parse` measures it in
    // characters — and then never recognised at the door.
    let offered = parts
        .headers
        .get(HEADER)
        .and_then(|v| std::str::from_utf8(v.as_bytes()).ok())
        .map(str::to_owned);

    // Open only while nothing could close it: no old key configured, no device ever linked,
    // and nobody but this machine able to connect. Ever, not now: a library whose last
    // device was revoked stays closed. And only to a request that offers no key — one that
    // offers a key is asking to be recognised, and a key this server does not know is
    // refused, so a revoked key never reads through an open door. The keys are asked first
    // because they cost nothing; the marker is a query.
    let keyless = offered.as_deref().is_none_or(|k| k.trim().is_empty());
    if keyless && state.loopback && state.keys.open() {
        let devices = Arc::clone(&state.devices);
        if !blocking(move || devices.closed()).await? {
            return Ok(Who::Open);
        }
    }
    let address = caller(parts, state.trust_proxy);

    // Before either lookup, so that a blocked address costs no query at all.
    if let Some(wait) = state.throttle.blocked_for(&address) {
        return Err(Refused::TooMany(wait.as_secs().max(1)));
    }

    if let Some(key) = state.keys.recognise(offered.as_deref()) {
        // A key that works clears the slate — the device was misconfigured, not hostile.
        state.throttle.record_success(&address);
        return Ok(Who::OldKey(key.clone()));
    }

    let device = match offered.filter(|k| !k.trim().is_empty()) {
        Some(key) => {
            let devices = Arc::clone(&state.devices);
            let now = now_in_millis();
            blocking(move || devices.recognise(&key, now)).await?
        }
        None => None,
    };
    let Some((device, profile)) = device else {
        state.throttle.record_failure(&address);
        return Err(Refused::Forbidden("unknown key".into()));
    };
    state.throttle.record_success(&address);
    Ok(Who::Device(device, profile))
}

/// Who the throttle counts a request against.
///
/// The socket, unless a proxy is trusted — in which case the **rightmost entry of the last**
/// `X-Forwarded-For` header, and both halves of that are the whole point of the function.
///
/// The header is a list a caller starts and every hop appends to: sending
/// `X-Forwarded-For: 1.2.3.4` makes the proxy write `1.2.3.4, <the real address>`, so the
/// leftmost entry is whatever the caller cared to type. Counting wrong keys against it meant
/// a fresh made-up address per guess, ten of them never landing on one key, and the throttle
/// never firing at all. The rightmost is the one the trusted hop wrote itself, about the
/// connection it actually accepted.
///
/// And the *last* header, because a header may arrive more than once. Some proxies append to
/// the list already there, others add a second `X-Forwarded-For:` line of their own, and
/// `HeaderMap::get` returns only the first — which, against a caller who sent one, is the
/// caller's. That reopened the same hole from the other side: the rightmost entry of a line
/// the attacker wrote is still a value the attacker chose. The trusted hop writes last.
///
/// **Exactly one proxy in front**, which is what `trust_proxy` says and all it says. Behind
/// two hops — a CDN in front of a reverse proxy — the last entry is the proxy's view of the
/// CDN, identical for every reader, and one device guessing keys would shut out the whole
/// deployment. Reading any of it untrusted would hand the forgery to everybody, so it is
/// opt-in and the socket is what stands otherwise.
pub(super) fn caller(parts: &Parts, trust_proxy: bool) -> String {
    if trust_proxy {
        if let Some(written) = parts
            .headers
            .get_all("x-forwarded-for")
            .into_iter()
            .next_back()
            // Cut out of the bytes, and only then read as text. `to_str` refuses a header
            // value holding anything in 0x80–0xFF, which hyper accepts and passes on — and
            // with a proxy that appends to the list, the caller's own bytes are in that same
            // value. So `X-Forwarded-For: 1.2.3.\xff` threw away the entry the *proxy* had
            // written and dropped the whole deployment onto the socket address, the one key
            // every request without the header already lands on. The last entry is the
            // trusted hop's, whatever the caller put before the comma.
            .and_then(|v| v.as_bytes().rsplit(|b| *b == b',').next())
            .map(<[u8]>::trim_ascii)
            .filter(|v| !v.is_empty() && v.is_ascii())
            .and_then(|v| std::str::from_utf8(v).ok())
        {
            return written.to_string();
        }
    }
    parts
        .extensions
        .get::<ConnectInfo<SocketAddr>>()
        .map(|ConnectInfo(a)| a.ip().to_string())
        .unwrap_or_else(|| "unknown".to_string())
}

pub enum Refused {
    /// No key, an unrecognised one, or one without the right. Deliberately one answer:
    /// telling a caller which of the three it was is telling it how to get closer.
    Forbidden(String),
    TooMany(u64),
    /// A device whose profile was deleted under it, asking for anything but the choice.
    Choose,
    /// The index could not be asked. Not a refusal of the caller, and answered as the
    /// failure it is rather than as a 403 that would send someone hunting for a wrong key.
    Failed(Failure),
}

impl From<Failure> for Refused {
    fn from(failure: Failure) -> Self {
        Refused::Failed(failure)
    }
}

impl IntoResponse for Refused {
    fn into_response(self) -> Response {
        match self {
            Refused::Forbidden(reason) => {
                (StatusCode::FORBIDDEN, Json(ErrorDto::new(reason))).into_response()
            }
            Refused::TooMany(seconds) => (
                StatusCode::TOO_MANY_REQUESTS,
                [("Retry-After", seconds.to_string())],
                Json(ErrorDto::new("too many wrong keys — try again later")),
            )
                .into_response(),
            Refused::Choose => (
                StatusCode::CONFLICT,
                Json(ErrorDto::new("choose a profile")),
            )
                .into_response(),
            Refused::Failed(failure) => failure.into_response(),
        }
    }
}

// -------------------------------------------------------------------- routes

/// What the server is, and whether it is up. With `POST /link`, the only route that answers without a key.
///
/// To a key it recognises, and only then, it also says `relay`: whether another device can
/// reach this server, which is the settings screen's one question before it offers to link
/// another device or to unlink this one. Behind the key because it is a fact about the
/// deployment, which a stranger at the door has no business learning, and judged by the same
/// door as every other route — a key tried here is counted, or `/health` would be the one
/// place to guess keys without the throttle noticing.
async fn health(
    State(state): State<AppState>,
    Recognised(recognised): Recognised,
) -> Result<Json<HealthDto>, Failure> {
    let relay = recognised.is_some().then(|| state.relay.possible());
    let local_drop = state.drop.enabled();
    let could_be_open = state.loopback && state.keys.open();
    let (library, closed) = blocking(move || {
        // A restricted profile's connection line reads « N séries pour vous »: the count is
        // the caller's shelf, not the library's — a child told « 212 séries » while shown
        // eight would learn that the rest is being kept from them, and how much of it.
        let ceiling = match &recognised {
            Some(who) => who.ceiling(),
            None => Ceiling::NONE,
        };
        // Counted, not built: reading the length of a list of every series was a real
        // defect on the one route anyone can call.
        // One statement for both answers: `linked` rides along rather than costing a
        // second trip on the route a container polls every few seconds.
        let sql = format!(
            "SELECT (SELECT COUNT(*) FROM edition e WHERE 1 = 1{}),
                    EXISTS (SELECT 1 FROM closed)",
            ceiling.and_edition("e.id")
        );
        state.db.read(|cx| {
            Ok(cx
                .query_one(&sql, [], |r| {
                    Ok((r.get::<_, i64>(0)?, r.get::<_, bool>(1)?))
                })?
                .unwrap_or((0, false)))
        })
    })
    .await?;

    Ok(Json(HealthDto {
        status: "ok",
        api: API_VERSION,
        format: FORMAT_VERSION,
        library,
        local_drop,
        linked: !could_be_open || closed,
        relay,
    }))
}

/// The shelf, filtered, sorted and paged.
/// The level a grid can draw, and the ceiling on how many of them a search may reduce it to.
/// Five hundred is `MAX_PAGE`: past the point where a page could show them, the number stops
/// being the shelf's problem and starts being the search's.
const EDITIONS: &str = "EDITION";
const SEARCH_CEILING: i64 = MAX_PAGE;

async fn list_series(
    Caller(who): Caller,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<SeriesPageDto>, Failure> {
    let filter = query.filter;
    let sort = SeriesSort::of(query.sort.as_deref());
    let direction = SortDirection::of(query.direction.as_deref(), sort);
    // A default that bounds the answer rather than one that hides part of it: `total`
    // always says how many there are, so a client can see it has only part and ask for the
    // rest. size=0 asks for everything on purpose.
    let size = query.size.unwrap_or(DEFAULT_PAGE).clamp(0, MAX_PAGE);
    let page = query.page.unwrap_or(0).max(0);
    // Both of these come off the query string, and `page * size` overflowed on the way in:
    // `?page=9223372036854775807&size=500` panicked the handler's task in a debug build and
    // wrapped to a negative offset in a release one, which the repository then clamped back
    // to zero — a request for the last page of the shelf quietly answered with the first.
    // Past the end is an empty page, and that is what an offset nothing can reach gives.
    let offset = page.checked_mul(size).unwrap_or(i64::MAX);

    // What was typed, when anything was. Blank is not a search: a client clearing its field
    // sends `q=` and means "the whole shelf", the same way a cleared chip does.
    let wanted = query.q.unwrap_or_default().trim().to_string();

    let page_dto = blocking(move || {
        let ceiling = who.ceiling();
        let repository = Repository::new(&state.db);
        let mut filter = filter;

        if !wanted.is_empty() {
            // The same query the search route runs, restricted to the level a grid can draw:
            // an edition is a tile, a chapter whose title matches is a line and belongs to
            // `/search`. The chips go in with it, so the search happens inside what is
            // showing rather than beside it, and the ids it finds become the filter the
            // shelf already knows how to apply — which is why there is no second query here.
            let found = repository.search(
                &wanted,
                SEARCH_CEILING,
                &[EDITIONS.to_string()],
                &filter,
                who.profile(),
                &ceiling,
            )?;
            let ids: Vec<String> = found.into_iter().map(|hit| hit.id).collect();
            if ids.is_empty() {
                // An empty shelf, and not the whole one. `ids` empty means "no restriction"
                // to the query builder, so nothing matching has to be answered here or a
                // search for a word nobody has would hand back the entire library.
                return Ok(SeriesPageDto {
                    items: Vec::new(),
                    total: 0,
                    page,
                    size,
                });
            }
            filter.ids = ids;
        }

        Ok(SeriesPageDto {
            items: repository.series_ordered(
                &filter,
                sort,
                direction,
                size,
                offset,
                who.profile(),
                &ceiling,
            )?,
            total: repository.count_series(&filter, who.profile(), &ceiling)?,
            page,
            size,
        })
    })
    .await?;
    Ok(Json(page_dto))
}

/// The universes this library holds, and how many ways through each one it declares.
async fn list_universes(
    Caller(who): Caller,
    State(state): State<AppState>,
) -> Result<Json<Vec<UniverseDto>>, Failure> {
    Ok(Json(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).universes(&ceiling)
        })
        .await?,
    ))
}

/// The named ways through one universe, in the order its file writes them.
///
/// An empty list and not a 404 for a universe that declares none: declaring no order is the
/// ordinary case, and a client asking has learnt something true either way. A 404 for one
/// that is not there — or holds nothing the caller may see, which must read the same.
async fn list_universe_orders(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<ReadingOrderDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).orders_of_universe(&id, &ceiling)
        })
        .await?,
        "unknown universe",
    )
}

/// The values the filters can take, so the application can offer them rather than make you
/// spell them.
async fn get_filters(
    Caller(who): Caller,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<FacetsDto>, Failure> {
    // `over=files` counts the two axes a row of pills draws over files rather than over
    // series, narrowed by `q` when one is given. A row above sixty file rows saying « Non
    // lues 5 » is counting series, which is not what the reader is looking at and not what
    // the pill beneath their finger would filter.
    let over_files = query.over.as_deref() == Some("files");
    let wanted = query.q.unwrap_or_default();
    let facets = blocking(move || {
        let ceiling = who.ceiling();
        let repository = Repository::new(&state.db);
        if over_files {
            repository.file_facets(&wanted, who.profile(), &ceiling)
        } else {
            repository.facets(who.profile(), &ceiling)
        }
    })
    .await?;
    Ok(Json(facets))
}

/// The rules of the on-disk format: where files go, and what may be written in them.
///
/// Served rather than written into the applications, because a page baked into a client
/// drifts from the server the first time the scanner learns something — and drifts in
/// silence, since nothing compares the two. These rules changed three times in one
/// afternoon.
async fn get_format(_: Reader) -> Json<super::format::Format> {
    Json(super::format::describe())
}

async fn get_series(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).one_series(&id, who.profile(), &ceiling)
        })
        .await?,
        "unknown series",
    )
}

// The four lists of a series below answer 404 for a series that is not there, where they
// used to answer `[]`. An empty list for an unknown id was harmless while every caller saw
// every series; under a ceiling it is the one difference between « hidden » and « absent »
// that a 404 on the series itself would not cover, so both now answer what `/series/{id}`
// answers.

/// Volumes and standalone chapters, mixed, in reading order.
async fn list_series_entries(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<EntryDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).entries(&id, &ceiling)
        })
        .await?,
        "unknown series",
    )
}

/// The whole edition sequence, whatever the materialisation.
async fn list_series_chapters(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<ChapterDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).chapters_of_edition(&id, &ceiling)
        })
        .await?,
        "unknown series",
    )
}

async fn list_series_arcs(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<ArcDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).arcs(&id, &ceiling)
        })
        .await?,
        "unknown series",
    )
}

async fn get_entry(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).entry(&id, &ceiling)
        })
        .await?,
        "unknown entry",
    )
}

/// A volume's markers — what no tool can read today.
async fn list_entry_chapters(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<ChapterDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).chapters_of_entry(&id, &ceiling)
        })
        .await?,
        "unknown entry",
    )
}

/// The pages of an entry.
///
/// `?width` tells the server what the pages will be asked for at, so the first of them can
/// be prepared while the client is still drawing the list. Without it nothing is prepared:
/// a guessed width is work thrown away.
async fn list_entry_pages(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
) -> Result<Response, Failure> {
    let width = query.width;
    or_missing::<Vec<PageDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            let pages = Repository::new(&state.db).pages(&id, &ceiling)?;
            // Only for an entry this caller may open: warming one it may not would be work
            // done for a reader who is about to be told it is not there.
            if pages.is_some() {
                state.pages.warm_opening(&id, width);
            }
            Ok(pages)
        })
        .await?,
        "unknown entry",
    )
}

// -------------------------------------------------------------- maintenance

/// Where the scan is, and what the last one found.
///
/// Under a ceiling, where it is and nothing of what it found: the findings name folders and
/// series — « Nuit/Ombre : ageRating » — and a restricted profile reading them would be
/// reading the shelf it is kept from.
async fn scan_status(
    Caller(who): Caller,
    State(state): State<AppState>,
) -> Result<Json<ScanStatus>, Failure> {
    let mut status = state.runner.status();
    if who.ceiling() != Ceiling::NONE {
        status.report = None;
    }
    Ok(Json(status))
}

/// Starts a scan.
///
/// Answers at once and scans behind: a full library takes tens of seconds, which no client
/// should hold a connection open for. One at a time — a second request is told so rather
/// than queued, because the second would read the same disk.
async fn start_scan(_: Principal, State(state): State<AppState>) -> Response {
    let scanner = Arc::clone(&state.scanner);
    let roots = Arc::clone(&state.roots);
    let pages = Arc::clone(&state.pages);
    let started = state.runner.start("Scan", move || {
        let report = scanner.scan(&roots)?;
        // The covers of whatever it found are prepared afterwards, once the library is
        // already browsable — the scan stays the fast thing.
        pages.warm_covers();
        Ok(report)
    });

    let status = state.runner.status();
    let code = if started {
        StatusCode::ACCEPTED
    } else {
        StatusCode::CONFLICT
    };
    (code, Json(status)).into_response()
}

// -------------------------------------------------------------------- pages

/// A page, at the width you ask for.
///
/// Without `?width` the original bytes are served untouched. With it the page comes back
/// downscaled — never upscaled: asking for 4000 on a 1500-wide scan returns the original
/// rather than a blurred enlargement.
async fn get_page(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path((id, number)): Path<(String, String)>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
) -> Result<Response, Failure> {
    let Ok(number) = number.parse::<i64>() else {
        return Ok((
            StatusCode::BAD_REQUEST,
            Json(ErrorDto::new("page number expected")),
        )
            .into_response());
    };
    let width = query.width;
    let held = held_tag(&headers);
    let served = blocking(move || {
        let ceiling = who.ceiling();
        let served =
            state
                .pages
                .page_or_unchanged(&id, number, width, held.as_deref(), &ceiling)?;
        // Only when a width was asked for: serving the source costs nothing to begin with.
        // And never from a cover or from the warming itself — without that distinction the
        // preparation fed itself, and fetching a grid of covers quietly resized the opening
        // pages of every volume in the library. Nor for a page this caller was refused.
        if let (Some(width), Some(_)) = (width, &served) {
            state.pages.warm_ahead(&id, number, width);
        }
        Ok(served)
    })
    .await?;
    Ok(image_response(served))
}

/// The cover, small by default. A thumbnail is a page asked for narrow.
async fn entry_cover(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
) -> Result<Response, Failure> {
    let width = query.width;
    let held = held_tag(&headers);
    let served = blocking(move || {
        let ceiling = who.ceiling();
        state
            .pages
            .cover_or_unchanged(&id, width, held.as_deref(), &ceiling)
    })
    .await?;
    Ok(image_response(served))
}

/// The tile of a grid: one request, not one to find the first entry and another for its
/// cover.
async fn series_cover(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
) -> Result<Response, Failure> {
    let width = query.width;
    let held = held_tag(&headers);
    let served = blocking(move || {
        let ceiling = who.ceiling();
        state
            .pages
            .series_cover_or_unchanged(&id, width, held.as_deref(), &ceiling)
    })
    .await?;
    Ok(image_response(served))
}

/// The original file: this is how a volume comes back down to be retouched.
async fn download_entry_file(
    Exporter(who): Exporter,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let path = blocking(move || {
        let ceiling = who.ceiling();
        // Asked before the stamp, not after: the stamp writes into the file, and a request
        // for a volume the caller may not see must not touch it.
        let Some(path) = Repository::new(&state.db).entry_path(&id, &ceiling)? else {
            return Ok(None);
        };
        // It leaves stamped — work, edition, number — so it can find its own way home when
        // it comes back. A GET that writes: deliberate,
        // because the stamp has to be in the copy that leaves rather than the one that
        // stays.
        Records::new(&state.db).stamp_entry(&id)?;
        Ok(Some(path))
    })
    .await?;
    let Some(path) = path else {
        return Err(Failure::Missing("unknown entry"));
    };

    let name = std::path::Path::new(&path)
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| "volume.cbz".to_string());

    // Opened and handed to the wire frame by frame. Read whole first, one download of a
    // 130 MB volume held 130 MB before a byte left, three at once held four hundred, and a
    // volume larger than what is free took the server down. `receive_import_file` streams
    // the other way for exactly this reason.
    let file = match tokio::fs::File::open(&path).await {
        Ok(it) => it,
        // A volume taken off the disk between two scans is a thing that is not there, which
        // is exactly what the line above answers for an entry the index has never heard of.
        // As a 500 it read as "the server is broken" for a library that had merely moved a
        // file, and it put an error-level line in the log for a condition that is routine.
        Err(e) if e.kind() == std::io::ErrorKind::NotFound => {
            return Err(Failure::Missing("this entry's file is not there any more"));
        }
        Err(e) => return Err(Failure::Unhandled(e.into())),
    };
    let facts = file
        .metadata()
        .await
        .map_err(|e| Failure::Unhandled(e.into()))?;
    // Opening a folder succeeds on Linux; reading one does not. Whole, the failure came back
    // as a 500 before a byte left. Streamed, the answer had already gone out 200 with the
    // folder's own size as `Content-Length` and died on the first read — a truncated file and
    // no error document, which is the one shape a client cannot tell from a short download.
    if !facts.is_file() {
        return Err(Failure::Unhandled(anyhow::anyhow!(
            "{path} is not a file — the entry points at something else"
        )));
    }

    let answer = Response::builder()
        .header(axum::http::header::CONTENT_TYPE, "application/octet-stream")
        .header(axum::http::header::CONTENT_DISPOSITION, attachment(&name))
        // Streamed, so nothing counts the bytes on the way out: without this the answer is
        // chunked and no client can say how far along it is.
        .header(axum::http::header::CONTENT_LENGTH, facts.len());
    answer
        // 64 KiB, not the 4 KiB `new` defaults to: a 130 MB volume is thirty-two thousand
        // chunks at the default, each one an allocation and a trip through the body stream,
        // for the exact case the comment above is about.
        .body(Body::from_stream(
            tokio_util::io::ReaderStream::with_capacity(file, 64 * 1024),
        ))
        .map_err(|e| Failure::Unhandled(e.into()))
}

/// `Content-Disposition` for a file name, in both of the spellings that exist.
///
/// A header value is bytes, and `filename="…"` is read as Latin-1: "Tome 1 — Été.cbz" put
/// there whole is saved as "Tome 1 â€” Ã‰tÃ©.cbz", a `"` in a name closes the string early
/// and truncates what follows, and a control byte makes the value unbuildable — a download
/// turned into a 500 by the name of the file it was for. A library in French is mostly
/// accented file names.
///
/// So `filename*=UTF-8''…` from RFC 8187, which every browser prefers when both are there,
/// and the plain one left behind for whatever does not — carrying only what a quoted string
/// can hold, and an underscore everywhere else.
fn attachment(name: &str) -> String {
    let plain: String = name
        .chars()
        .map(|c| {
            if c.is_ascii_alphanumeric() || " .-_()[]".contains(c) {
                c
            } else {
                '_'
            }
        })
        .collect();
    let mut encoded = String::new();
    for byte in name.as_bytes() {
        match byte {
            b'A'..=b'Z'
            | b'a'..=b'z'
            | b'0'..=b'9'
            | b'!'
            | b'#'
            | b'$'
            | b'&'
            | b'+'
            | b'-'
            | b'.'
            | b'^'
            | b'_'
            | b'`'
            | b'|'
            | b'~' => encoded.push(char::from(*byte)),
            // Written into the buffer rather than through a `format!` per byte: an accented
            // name escapes a dozen of them, and this runs on every download.
            other => {
                use std::fmt::Write;
                let _ = write!(encoded, "%{other:02X}");
            }
        }
    }
    format!("attachment; filename=\"{plain}\"; filename*=UTF-8''{encoded}")
}

/// The tag the client says it already holds, without its quotes.
///
/// Read before the work rather than after it: the tag of an answer is known from the index
/// alone, so a client that already holds it is told so before the archive is opened.
fn held_tag(headers: &axum::http::HeaderMap) -> Option<String> {
    headers
        .get(axum::http::header::IF_NONE_MATCH)
        .and_then(|v| v.to_str().ok())
        .map(|v| v.trim_matches('"').to_string())
}

/// The tag carries the file's modification time, so a cached page can be trusted for as
/// long as the client cares to keep it.
fn image_response(served: Option<Served>) -> Response {
    let cache_control = (
        axum::http::header::CACHE_CONTROL.as_str(),
        "private, max-age=31536000".to_string(),
    );
    match served {
        None => (StatusCode::NOT_FOUND, Json(ErrorDto::new("unknown page"))).into_response(),
        // The freshness headers go on the 304 too: a client that revalidates and gets a
        // bare 304 has learnt nothing about how long it may now keep what it has.
        Some(Served::Unchanged(tag)) => (
            StatusCode::NOT_MODIFIED,
            [(axum::http::header::ETAG.as_str(), tag), cache_control],
        )
            .into_response(),
        Some(Served::Image(image)) => (
            [
                (axum::http::header::ETAG.as_str(), image.tag),
                cache_control,
                (axum::http::header::CONTENT_TYPE.as_str(), image.media_type),
            ],
            image.bytes,
        )
            .into_response(),
    }
}

// ------------------------------------------------------------------- search

/// Find something by name.
///
/// The shelf's own filters apply: a search runs *inside* what is showing, not beside it. A
/// lit chip is a statement about what you are looking at, and a search that ignored it would
/// hand back series the shelf behind it is hiding.
async fn search(
    Caller(who): Caller,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Response, Failure> {
    // A client opts into the page envelope by naming either page coordinate. With neither,
    // the historical bare array remains byte-for-byte the shape older clients understand.
    let paged = query.page.is_some() || query.size.is_some();
    let wanted = query.q.unwrap_or_default();
    let size = query.size.or(query.limit).unwrap_or(40);
    let page = query.page.unwrap_or(0);
    let kinds = query.kind;
    let filter = query.filter;
    let explicitly_sorted = query.sort.is_some() || query.direction.is_some();
    let sort = SeriesSort::of(query.sort.as_deref());
    let direction = SortDirection::of(query.direction.as_deref(), sort);
    let order = explicitly_sorted.then_some((sort, direction));

    if paged {
        let found = blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).search_page_ordered(
                &wanted,
                size,
                page,
                &kinds,
                &filter,
                order,
                who.profile(),
                &ceiling,
            )
        })
        .await?;
        Ok(Json(found).into_response())
    } else if order.is_some() {
        let found = blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db)
                .search_page_ordered(
                    &wanted,
                    size,
                    0,
                    &kinds,
                    &filter,
                    order,
                    who.profile(),
                    &ceiling,
                )
                .map(|page| page.items)
        })
        .await?;
        Ok(Json(found).into_response())
    } else {
        let found = blocking(move || {
            let ceiling = who.ceiling();
            Repository::new(&state.db).search(
                &wanted,
                size,
                &kinds,
                &filter,
                who.profile(),
                &ceiling,
            )
        })
        .await?;
        Ok(Json(found).into_response())
    }
}

// ----------------------------------------------------------------- progress

/// The clock the recording handlers and the door stamp with. Written once because two
/// copies of the same expression drift the day one of them is fixed.
pub(super) fn now_in_millis() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0)
}

/// Where the reader stands in one entry.
///
/// 404 for an entry that is not there, where it used to be the 204 of one never opened: under
/// a ceiling, « never opened » about a hidden volume would say that it exists.
async fn entry_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let found = blocking(move || {
        let ceiling = who.ceiling();
        Progress::new(&state.db).of_entry(&id, who.profile(), &ceiling)
    })
    .await?;
    match found {
        Some(Some(progress)) => Ok(Json(progress).into_response()),
        // Never opened. Not an error, and not an empty object either.
        Some(None) => Ok(StatusCode::NO_CONTENT.into_response()),
        None => Err(Failure::Missing("unknown entry")),
    }
}

/// Records a position.
///
/// Recording progress is part of reading, so a read-only key is allowed to do it —
/// otherwise the phone could not remember where you stopped.
async fn record_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(patch): Json<ProgressPatch>,
) -> Result<Response, Failure> {
    let now = now_in_millis();
    let profile = who.profile().to_owned();
    or_missing(
        blocking(move || {
            let ceiling = who.ceiling();
            Progress::new(&state.db).record(&profile, &id, &patch, now, &ceiling)
        })
        .await?,
        "unknown entry",
    )
}

async fn forget_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Failure> {
    let profile = who.profile().to_owned();
    blocking(move || Progress::new(&state.db).forget(&profile, &id)).await?;
    Ok(StatusCode::NO_CONTENT)
}

/// Where the reader stands in every entry of a series — a 404 for a series that is not
/// there for this caller, as on every other route of a series.
async fn series_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing::<Vec<ProgressDto>>(
        blocking(move || {
            let ceiling = who.ceiling();
            Progress::new(&state.db).of_series(&id, who.profile(), &ceiling)
        })
        .await?,
        "unknown series",
    )
}

/// Marks a whole series read, or unread — one statement rather than one call per volume.
async fn record_series_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(patch): Json<SeriesProgressPatch>,
) -> Result<Response, Failure> {
    let now = now_in_millis();
    let profile = who.profile().to_owned();
    or_missing(
        blocking(move || {
            let ceiling = who.ceiling();
            Progress::new(&state.db).record_series(&profile, &id, patch.finished, now, &ceiling)
        })
        .await?,
        "unknown series",
    )
}

async fn forget_series_progress(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Failure> {
    let profile = who.profile().to_owned();
    blocking(move || Progress::new(&state.db).forget_series(&profile, &id)).await?;
    Ok(StatusCode::NO_CONTENT)
}

/// The way a reader chose to read a series, which is not what its files say.
///
/// Behind `Reader` like progress, and for the same reason: it is the reader's own state, and a
/// phone with a read-only key could not remember that a manga is read the other way round.
/// Not a `PATCH /series/{id}`, which writes into the sidecars and would put a reader's choice
/// in a file somebody else describes the work in.
async fn choose_reading_direction(
    Caller(who): Caller,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(body): Json<ReadingChoice>,
) -> Result<Response, Failure> {
    let series = blocking(move || {
        let said = body.reading_direction.ok_or_else(|| {
            crate::api::invalid(
                "readingDirection is required — null goes back to what the files declare",
            )
        })?;
        let chosen = match said.as_deref() {
            None => None,
            Some(spelled) => Some(
                crate::metadata::sidecars::reading_direction(spelled).ok_or_else(|| {
                    crate::api::invalid(format!(
                        "\"{spelled}\" is not a reading direction — {}",
                        crate::metadata::sidecars::READING_DIRECTIONS.join(", ")
                    ))
                })?,
            ),
        };
        let repository = Repository::new(&state.db);
        // A series above this profile's age is not there for it, to be told or to be chosen:
        // the same 404 as for one that does not exist.
        let ceiling = who.ceiling();
        if repository
            .one_series(&id, who.profile(), &ceiling)?
            .is_none()
            || !repository.choose_reading_direction(who.profile(), &id, chosen)?
        {
            return Ok(None);
        }
        repository.one_series(&id, who.profile(), &ceiling)
    })
    .await?;
    or_missing(series, "unknown series")
}

/// `null` is a value here and not an absence: it says "go back to what the files declare".
/// The two are told apart, outer `None` for a field left out and `Some(None)` for `null`: a
/// plain `Option` read both as `None`, and a body of `{}` answered 200 and erased the choice
/// the reader had made, where the contract says the field is required.
#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct ReadingChoice {
    #[serde(default, deserialize_with = "present")]
    reading_direction: Option<Option<String>>,
}

/// Called only when the field is in the body, so whatever it holds — `null` included — is
/// `Some`; `default` leaves an absent field `None`.
fn present<'de, D, T>(deserializer: D) -> Result<Option<T>, D::Error>
where
    D: serde::Deserializer<'de>,
    T: serde::Deserialize<'de>,
{
    T::deserialize(deserializer).map(Some)
}

// -------------------------------------------------------------------- erasing

/// Takes a whole edition off the disk.
///
/// Behind `Eraser` and not `Importer`: filling a shelf and emptying it are not the same
/// power, and a key written before this right existed cannot do it.
async fn erase_series(
    _: Eraser,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let roots = Arc::clone(&state.roots);
    or_missing(
        blocking(move || Erase::new(&state.db, &roots).series(&id)).await?,
        "unknown series",
    )
}

async fn erase_entry(
    _: Eraser,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let roots = Arc::clone(&state.roots);
    or_missing(
        blocking(move || Erase::new(&state.db, &roots).entry(&id)).await?,
        "unknown entry",
    )
}

/// What to open next: what you are in the middle of, then what follows it.
async fn up_next(
    Caller(who): Caller,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<Vec<UpNextDto>>, Failure> {
    let limit = query.limit.unwrap_or(20);
    Ok(Json(
        blocking(move || {
            let ceiling = who.ceiling();
            Progress::new(&state.db).up_next(limit, who.profile(), &ceiling)
        })
        .await?,
    ))
}

// ------------------------------------------------- records, no file transfer

/// Edits what a series says about itself.
///
/// The edit goes into a sidecar on the disk, never into the index; the rescan that follows
/// reads it back. See [`super::records`] for why.
async fn patch_series(
    Editor(who): Editor,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(patch): Json<SeriesPatch>,
) -> Result<Response, Failure> {
    let series = blocking(move || {
        if !Records::new(&state.db).patch_series(&id, &patch)? {
            return Ok(None);
        }
        rescan_around(
            &state,
            Repository::new(&state.db).work_folder_of_series(&id)?,
        );
        // Read back as whoever edited: their own read status, under their own ceiling. A
        // restricted profile has no right to edit and never gets this far, but an answer
        // read as the principal is the principal's answer whoever it is handed to.
        Repository::new(&state.db).one_series(&id, who.profile(), &who.ceiling())
    })
    .await?;
    or_missing(series, "unknown series")
}

/// Replaces a series' arcs outright: an arc list is a whole, and patching one entry of it
/// would need a name for each, which the sidecar deliberately does not have.
async fn patch_arcs(
    Editor(who): Editor,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(arcs): Json<Vec<ArcJson>>,
) -> Result<Response, Failure> {
    let arcs = blocking(move || {
        if !Records::new(&state.db).set_arcs(&id, arcs)? {
            return Ok(None);
        }
        rescan_around(
            &state,
            Repository::new(&state.db).work_folder_of_series(&id)?,
        );
        Repository::new(&state.db).arcs(&id, &who.ceiling())
    })
    .await?;
    or_missing(arcs, "unknown series")
}

async fn patch_entry(
    Editor(who): Editor,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(patch): Json<EntryPatch>,
) -> Result<Response, Failure> {
    let entry = blocking(move || {
        if !Records::new(&state.db).patch_entry(&id, &patch)? {
            return Ok(None);
        }
        rescan_around(
            &state,
            Repository::new(&state.db).work_folder_of_entry(&id)?,
        );
        Repository::new(&state.db).entry(&id, &who.ceiling())
    })
    .await?;
    or_missing(entry, "unknown entry")
}

/// Moves a work's folder into a universe, or out to the library root it lives under.
///
/// A `rename` and a rescan. It is only that because an identity no longer comes from a path
/// — see [`super::relocate`] — and the rescan is aimed at the work rather than swept over the
/// library, which would cost as much as the library is big.
async fn move_work(
    _: Editor,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(request): Json<MoveRequest>,
) -> Result<Response, Failure> {
    let moved = blocking(move || {
        let relocate = Relocate::new(&state.db, &state.roots);
        let Some(to) = relocate.into_universe(&id, request.universe_id.as_deref())? else {
            return Ok(None);
        };
        let Some(moved) = relocate.work(&id, &to)? else {
            return Ok(None);
        };
        // Only what moved is read again. A work asked to go where it already is has changed
        // nothing on the disk, and rescanning it would be a second answer to a first one.
        if moved.moved {
            state
                .scanner
                .rescan_work(std::path::Path::new(&moved.path))?;
        }
        Ok(Some(moved))
    })
    .await?;
    or_missing(moved, "unknown work")
}

// ------------------------------------------------ the short path, same machine

/// What is sitting in the shared folder, waiting to be taken in.
async fn list_drop(_: Importer, State(state): State<AppState>) -> Json<DropListing> {
    Json(state.drop.list())
}

/// Takes a file from the shared folder rather than through the loopback. Same proposal,
/// same confirmation afterwards — only the bytes travelled differently.
async fn take_from_drop(
    _: Importer,
    State(state): State<AppState>,
    Json(request): Json<DropRequest>,
) -> Result<Json<Proposal>, Failure> {
    Ok(Json(blocking(move || state.drop.receive(&request)).await?))
}

// -------------------------------------------- one file at a time: where does it go?

/// What the client could read cheaply, so the server can propose before the bytes move.
#[derive(serde::Deserialize)]
#[serde(rename_all = "camelCase")]
struct PreflightRequest {
    name: String,
    size: u64,
    /// The `entry.json` found inside the archive, as it stands. Absent when the client
    /// could not open the file — the proposal is then weaker, and the screen says so.
    #[serde(default)]
    sidecar: Option<String>,
}

/// Holds a place for a file and says where it would go, before a byte has moved.
async fn preflight(
    _: Importer,
    State(state): State<AppState>,
    Json(request): Json<PreflightRequest>,
) -> Result<Json<super::intake::Reserved>, Failure> {
    let bytes = request.sidecar.map(String::into_bytes);
    // Refused here rather than at the last byte. The ceiling is the same one `stream_to`
    // enforces while receiving; without this the client announced a file, held a place for
    // it, sent it, and was refused at the end — a whole transfer spent to learn something
    // the first request already said.
    if request.size > state.max_upload_bytes {
        return Err(Failure::Unhandled(super::invalid(format!(
            "{} is larger than this server accepts in one upload",
            request.name
        ))));
    }
    Ok(Json(
        blocking(move || {
            state
                .intake
                .reserve(&request.name, request.size, bytes.as_deref())
        })
        .await?,
    ))
}

/// How much of a reserved file the server holds. Enough for a broken transfer to resume.
async fn staged_entry(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<super::intake::Staged>, Failure> {
    blocking(move || state.intake.staged(&id))
        .await?
        .map(Json)
        .ok_or_else(|| Failure::Unhandled(crate::api::absent("unknown intake".to_string())))
}

/// The bytes of a reserved file, from an offset.
async fn receive_staged_file(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
    headers: axum::http::HeaderMap,
    body: Body,
) -> Result<Response, Failure> {
    let from = headers
        .get(axum::http::header::CONTENT_RANGE)
        .and_then(|v| v.to_str().ok())
        .and_then(range_start)
        .unwrap_or(0);

    let target = {
        let state = state.clone();
        match blocking(move || Ok(state.intake.writing_at(&id, from))).await? {
            Ok((path, _)) => path,
            Err(ReceiveError::BadOffset(offset)) => {
                return Ok((
                    StatusCode::CONFLICT,
                    Json(serde_json::json!({
                        "error": format!(
                            "impossible offset, the server holds {} byte(s)",
                            offset.received
                        ),
                        "received": offset.received,
                    })),
                )
                    .into_response())
            }
            Err(ReceiveError::Unknown(what)) => {
                return Err(Failure::Unhandled(crate::api::absent(what)))
            }
            Err(ReceiveError::Other(e)) => return Err(Failure::Unhandled(e)),
        }
    };

    let name = target
        .file_name()
        .unwrap_or_default()
        .to_string_lossy()
        .to_string();
    let received = stream_to(body, target, from, state.max_upload_bytes).await?;
    Ok(Json(serde_json::json!({ "path": name, "received": received })).into_response())
}

/// Drop it, the server proposes a destination. Nothing has moved yet.
async fn receive_entry(
    _: Importer,
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    body: Body,
) -> Result<Json<Proposal>, Failure> {
    let Some(name) = headers
        .get("X-Leaf-Name")
        // The bytes, decoded as UTF-8, not `to_str`: that admits visible ASCII only, and
        // reported "Tome 1 — Été.cbz" as no header at all rather than as a name it could
        // not read. A library in French is mostly accented file names.
        .and_then(|v| std::str::from_utf8(v.as_bytes()).ok())
        .map(str::to_string)
    else {
        return Err(Failure::Unhandled(crate::api::invalid(
            "header \"X-Leaf-Name\" expected",
        )));
    };

    let staging = {
        let state = state.clone();
        blocking(move || state.intake.staging_for(&name)).await?
    };
    // Half a file left in the inbox says nothing and cannot be resumed — this path has no
    // offset, unlike the bulk one. The staging clears itself if anything below fails, an
    // interrupted connection included.
    stream_to(
        body,
        staging.path().to_path_buf(),
        0,
        state.max_upload_bytes,
    )
    .await?;

    Ok(Json(
        blocking(move || {
            let proposal = state.intake.propose_for(staging.path())?;
            staging.keep();
            Ok(proposal)
        })
        .await?,
    ))
}

/// You confirm the destination, and only then is the file filed.
async fn file_entry(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(request): Json<FileRequest>,
) -> Result<Response, Failure> {
    let filed = blocking(move || {
        let filed = state.intake.file(&id, &request)?;
        // Only the series that received it can have changed.
        rescan_around(
            &state,
            Repository::new(&state.db).work_folder_of_series(&request.series_id)?,
        );
        Ok(filed)
    })
    .await?;
    Ok(Json(filed).into_response())
}

async fn abandon_entry(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Failure> {
    blocking(move || state.intake.abandon(&id)).await?;
    Ok(StatusCode::NO_CONTENT)
}

// --------------------------------------------------------------- bulk import

async fn open_import(
    _: Importer,
    State(state): State<AppState>,
    Json(request): Json<ImportRequest>,
) -> Result<Response, Failure> {
    let opened = blocking(move || state.bulk.open(&request)).await?;
    Ok(Json(opened).into_response())
}

/// Every file staged and waiting for a decision.
///
/// Nothing sweeps the inbox, and the inbox sits on the library's own filesystem — so a
/// proposal made and never answered holds the library's own space for good. Without this
/// there was no way to reach one: every other route takes an id, and a desktop that crashed
/// took the only copy of it with it.
async fn list_intake(
    _: Importer,
    State(state): State<AppState>,
) -> Result<Json<Vec<super::intake::Waiting>>, Failure> {
    Ok(Json(blocking(move || state.intake.waiting()).await?))
}

/// Every bulk session opened and not yet finished.
async fn list_imports(
    _: Importer,
    State(state): State<AppState>,
) -> Result<Json<Vec<super::bulk_import::Open>>, Failure> {
    Ok(Json(blocking(move || state.bulk.waiting()).await?))
}

/// How much of each file the server holds, so a broken transfer knows where to resume.
async fn import_state(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing(
        blocking(move || state.bulk.state(&id)).await?,
        "unknown import",
    )
}

/// One file of an import, at an offset.
///
/// The body goes to the disk as it arrives. Buffering it first would mean holding a whole
/// volume in memory — a hundred and thirty megabytes for one, and an import sends them back
/// to back.
async fn receive_import_file(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
    body: Body,
) -> Result<Response, Failure> {
    let Some(path) = query.path else {
        return Err(Failure::Unhandled(crate::api::invalid(
            "query parameter \"path\" expected",
        )));
    };
    let from = headers
        .get(axum::http::header::CONTENT_RANGE)
        .and_then(|v| v.to_str().ok())
        .and_then(range_start)
        .unwrap_or(0);

    let ceiling = state.max_upload_bytes;
    let target = {
        let (state, path) = (state.clone(), path.clone());
        match blocking(move || Ok(state.bulk.writing_at(&id, &path, from, ceiling))).await? {
            Ok(target) => target,
            // 409 carrying what the server holds: the client knows exactly where to resume.
            Err(ReceiveError::BadOffset(offset)) => {
                return Ok((
                    StatusCode::CONFLICT,
                    Json(serde_json::json!({
                        "error": format!(
                            "impossible offset, the server holds {} byte(s)",
                            offset.received
                        ),
                        "received": offset.received,
                    })),
                )
                    .into_response())
            }
            Err(ReceiveError::Unknown(id)) => {
                return Err(Failure::Unhandled(crate::api::absent(format!(
                    "unknown import: {id}"
                ))))
            }
            Err(ReceiveError::Other(e)) => return Err(Failure::Unhandled(e)),
        }
    };

    let received = stream_to(body, target, from, ceiling).await?;
    // A number, like every other count in this API. Built from a map of strings it would
    // come back as text, which is an accident of the type rather than a decision.
    Ok(Json(serde_json::json!({ "path": path, "received": received })).into_response())
}

/// Moves the inbox into the library, then rescans.
///
/// Nothing is deleted: orphans are handed back to the client, which shows them to you
/// before you decide.
async fn commit_import(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
    request: Option<Json<CommitRequest>>,
) -> Result<Response, Failure> {
    let asked = request.map(|Json(it)| it).unwrap_or_default();
    let wanted = asked.move_these;
    let replacing = asked.replace_these;
    let declaring = asked.declare_these;
    let result = blocking(move || {
        // **Before the install, not after.** The commit writes the folder's declarations
        // into the library, so the place a work is being filed under exists by the time it
        // finishes — and a move onto a folder that is already there is refused, rightly.
        // Moving first is also what the announcement promised: install this *here*.
        //
        // Only what the request named. Moving what nobody asked to move rearranges a
        // library that was fine, and it is the one thing this route must never decide by
        // itself.
        let relocate = Relocate::new(&state.db, &state.roots);
        let mut filed = Vec::new();
        for work_id in &wanted {
            let Some(to) = state.bulk.place_for(&id, work_id)? else {
                continue;
            };
            if let Some(moved) = relocate.work(work_id, &to)? {
                if moved.moved {
                    filed.push(work_id.clone());
                }
            }
        }

        let mut result = state.bulk.commit(&id, &replacing, &declaring)?;
        result.moved = filed;

        // Aimed at what has just arrived rather than at the whole library. A full scan here
        // costs as much as the library is big — fifteen seconds at two hundred series — and
        // the request held the connection open for all of it, to index one folder.
        //
        // Never aimed once something moved: a work filed under the folder that just arrived
        // is a second place that changed, and one aimed rescan cannot read both.
        let target = state.bulk.target_of(&result.root);
        if result.moved.is_empty() && can_be_aimed_at(&target) {
            state.scanner.rescan_work(&target)?;
            state.pages.warm_covers();
        } else {
            // A whole shelf at once is too broad to aim: it goes behind, like any scan.
            background_scan(&state, "Scan after import");
        }
        Ok(result)
    })
    .await?;
    Ok(Json(result).into_response())
}

/// What a commit may be told to do besides installing. Optional: a commit with no body is
/// the ordinary one, and was the only one there was.
#[derive(Debug, Default, serde::Deserialize)]
#[serde(default, rename_all = "camelCase")]
pub struct CommitRequest {
    /// The works to file under the folder being installed, by identity — the ones the
    /// announcement listed under `moves` and a reader ticked.
    #[serde(rename = "move")]
    pub move_these: Vec<String>,
    /// The paths this commit may install on top of a file the library already holds — the
    /// ones the announcement listed under `replaces` and a reader ticked. Its twin above,
    /// and for the same reason: overwriting a volume somebody already has is a decision,
    /// and a commit must never take it on its own.
    #[serde(rename = "replace")]
    pub replace_these: Vec<String>,
    /// The sidecars this commit may write over one the library already holds — the ones the
    /// announcement listed under `declarations` and a reader ticked. A declaration with no
    /// counterpart is installed whatever this says: `creates` announced it.
    #[serde(rename = "declare")]
    pub declare_these: Vec<String>,
}

async fn abandon_import(
    _: Importer,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Failure> {
    blocking(move || state.bulk.abandon(&id)).await?;
    Ok(StatusCode::NO_CONTENT)
}

/// Deletion on an explicit, by-name order, never inferred from a manifest.
async fn cleanup(
    _: Principal,
    State(state): State<AppState>,
    Json(request): Json<CleanupRequest>,
) -> Result<Response, Failure> {
    let removed = blocking(move || {
        let removed = state.bulk.cleanup(&request)?;
        // Behind, through the runner. It used to be a full scan inside the request: fifteen
        // seconds with the connection held open, invisible to /scan, and able to meet a
        // scan already under way.
        background_scan(&state, "Scan after a cleanup");
        Ok(removed)
    })
    .await?;
    Ok(Json(serde_json::json!({ "removed": removed })).into_response())
}

// ------------------------------------------------------- rescanning, and bytes

/// Rescans only the work something belongs to.
///
/// Reading the whole library after one edited field is five seconds on six series and a
/// minute on sixty. When the thing cannot be placed — the only case where something outside
/// it might have moved — the sweep goes **behind**, through the runner, rather than being
/// run inside the request. That last part is the fix for wart 6.
fn rescan_around(state: &AppState, work_folder: Option<String>) {
    match work_folder {
        Some(folder) => {
            if let Err(e) = state.scanner.rescan_work(std::path::Path::new(&folder)) {
                tracing::warn!(folder, error = %e, "could not rescan the work");
            }
        }
        None => background_scan(state, "Scan after an edit"),
    }
}

/// A full sweep, behind the request, with the covers of whatever it found prepared after —
/// so the library is browsable first and the tiles catch up.
fn background_scan(state: &AppState, label: &'static str) {
    let scanner = Arc::clone(&state.scanner);
    let roots = Arc::clone(&state.roots);
    let pages = Arc::clone(&state.pages);
    if !state.runner.start(label, move || {
        let report = scanner.scan(&roots)?;
        pages.warm_covers();
        Ok(report)
    }) {
        // Not an error: the scan that is already running will see the same disk.
        tracing::info!(label, "a scan is already running");
    }
}

/// Whether a rescan can be aimed at this folder rather than at the library.
///
/// Only at a **work**, because `rescan_work` is what it would be aimed with, and that reads
/// the folder it is given as a work: its sub-folders become editions and its archives become
/// entries. Aimed at a universe, it filed the universe as a work and each of its works as an
/// edition — the whole hierarchy off by one, `universeId` gone, and `?universe=` answering
/// nothing until the next full scan happened to repair it. And importing into a universe
/// folder is an ordinary thing to do: `root: "Terres d'Arran"` is how a new work joins one.
///
/// So the classification the scanner itself uses decides, rather than a hand-rolled guess at
/// the same question. It errs towards the sweep — a folder of folders with no `work.json` is
/// taken for a universe here exactly as it is there, and the two agree by construction. The
/// cost of not aiming is seconds; the cost of aiming wrongly was the model.
pub fn can_be_aimed_at(target: &std::path::Path) -> bool {
    layout::kind(target).0 == layout::Kind::Work
}

/// Streams a request body to a file, under a ceiling, without ever holding it whole.
///
/// The ceiling is checked as the bytes arrive rather than from a declared length: a body
/// that lies about its size is stopped at the limit instead of after it.
async fn stream_to(
    body: Body,
    target: std::path::PathBuf,
    from: u64,
    max_bytes: u64,
) -> Result<u64, Failure> {
    use tokio::io::AsyncWriteExt;

    let file = tokio::task::spawn_blocking(move || super::bulk_import::open_at(&target, from))
        .await
        .map_err(|e| Failure::Unhandled(anyhow::anyhow!("the task did not finish: {e}")))?
        .map_err(|e| Failure::Unhandled(e.into()))?;
    let mut file = tokio::fs::File::from_std(file);

    let mut written = from;
    let mut body = body;
    while let Some(frame) = body.frame().await {
        let frame = frame.map_err(|e| Failure::Unhandled(anyhow::anyhow!("{e}")))?;
        let Some(chunk) = frame.data_ref() else {
            continue;
        };
        written += chunk.len() as u64;
        if written > max_bytes {
            return Err(Failure::Unhandled(super::bulk_import::over(max_bytes)));
        }
        file.write_all(chunk)
            .await
            .map_err(|e| Failure::Unhandled(e.into()))?;
    }
    file.flush()
        .await
        .map_err(|e| Failure::Unhandled(e.into()))?;
    Ok(written)
}

/// `bytes 104857600-134217727/134217728` → 104857600
fn range_start(header: &str) -> Option<u64> {
    let rest = header.trim().strip_prefix("bytes")?.trim_start();
    let digits: String = rest.chars().take_while(char::is_ascii_digit).collect();
    // Only when a range was actually spelled out: "bytes */134217728" declares no start.
    rest[digits.len()..]
        .starts_with('-')
        .then(|| digits.parse().ok())?
}

// --------------------------------------------------------------- the plumbing

/// Reads the filters off the query string: `?medium=manga&medium=bd&author=Ohba`.
///
/// Repeating a parameter widens the choice, naming a second one narrows it. Blanks are
/// dropped so that an application clearing a chip and sending an empty value asks for
/// everything rather than for series whose author is the empty string.
///
/// Parsed by hand rather than through `Query`: `serde_urlencoded`, which is what axum's
/// extractor uses, keeps only the last value of a repeated key. `?author=a&author=b` would
/// silently mean `author=b`, and the widening half of the whole filter design would be
/// gone without an error to notice.
#[derive(Debug, Default)]
pub struct ListQuery {
    pub filter: SeriesFilter,
    pub sort: Option<String>,
    pub direction: Option<String>,
    /// `series` or `files`: what `/filters` counts. Absent is `series`, which is what every
    /// client asking before this existed means.
    pub over: Option<String>,
    pub page: Option<i64>,
    pub size: Option<i64>,
    /// What was typed, for /search.
    pub q: Option<String>,
    /// Which levels to answer with; empty means all three.
    pub kind: Vec<String>,
    pub limit: Option<i64>,
    /// The width an image is wanted at. Never upscales beyond the original.
    pub width: Option<u32>,
    /// Which file of an import is being sent.
    pub path: Option<String>,
}

impl ListQuery {
    pub fn parse(query: Option<&str>) -> Self {
        let mut out = ListQuery::default();
        for (key, value) in form_urlencoded::parse(query.unwrap_or_default().as_bytes()) {
            let value = value.trim().to_string();
            match key.as_ref() {
                "sort" => out.sort = Some(value),
                "direction" => out.direction = Some(value),
                "over" => out.over = Some(value),
                "page" => out.page = value.parse().ok(),
                "size" => out.size = value.parse().ok(),
                "limit" => out.limit = value.parse().ok(),
                "width" => out.width = value.parse().ok(),
                "q" => out.q = Some(value),
                "path" if !value.is_empty() => out.path = Some(value),
                "kind" if !value.is_empty() => out.kind.push(value),
                _ if value.is_empty() => {}
                "read" => out.filter.read_statuses.push(value),
                "work" => out.filter.works.push(value),
                "universe" => out.filter.universes.push(value),
                "author" => out.filter.authors.push(value),
                "genre" => out.filter.genres.push(value),
                "medium" => out.filter.media.push(value),
                "status" => out.filter.statuses.push(value),
                "language" => out.filter.languages.push(value),
                "publisher" => out.filter.publishers.push(value),
                // An unknown parameter is not an error: a client from a later version may
                // send one this server has never heard of, and a shelf is the right answer.
                _ => {}
            }
        }
        out
    }
}

impl<S: Send + Sync> FromRequestParts<S> for ListQuery {
    type Rejection = std::convert::Infallible;
    async fn from_request_parts(parts: &mut Parts, _: &S) -> Result<Self, Self::Rejection> {
        Ok(ListQuery::parse(parts.uri.query()))
    }
}

/// A thing, or a 404 naming what was not there.
///
/// The most common answer this server gives after the thing itself, and it was written
/// seven times in two different spellings. One is enough.
fn or_missing<T: serde::Serialize>(
    found: Option<T>,
    what: &'static str,
) -> Result<Response, Failure> {
    match found {
        Some(value) => Ok(Json(value).into_response()),
        None => Err(Failure::Missing(what)),
    }
}

/// Runs database work off the runtime. See `store::db` for why the database is not async.
pub async fn blocking<T, F>(f: F) -> Result<T, Failure>
where
    F: FnOnce() -> anyhow::Result<T> + Send + 'static,
    T: Send + 'static,
{
    match tokio::task::spawn_blocking(f).await {
        Ok(Ok(value)) => Ok(value),
        Ok(Err(e)) => Err(Failure::Unhandled(e)),
        Err(e) => Err(Failure::Unhandled(anyhow::anyhow!(
            "the task did not finish: {e}"
        ))),
    }
}

pub enum Failure {
    /// A thing that is not there. Not a fault in the request.
    Missing(&'static str),
    /// Anything unforeseen: logged in full, answered with nothing. An error message can
    /// carry a path, a query, a piece of the schema.
    Unhandled(anyhow::Error),
}

impl IntoResponse for Failure {
    fn into_response(self) -> Response {
        match self {
            Failure::Missing(what) => {
                (StatusCode::NOT_FOUND, Json(ErrorDto::new(what))).into_response()
            }
            Failure::Unhandled(e) => {
                // A malformed request is the caller's problem, not a server failure — and
                // it comes back as JSON like everything else, or a client would need a
                // second way of reading errors.
                if let Some(bad) = e.downcast_ref::<crate::api::Invalid>() {
                    return (
                        StatusCode::BAD_REQUEST,
                        Json(ErrorDto::new(bad.to_string())),
                    )
                        .into_response();
                }
                // Asking about an import that has expired or been committed is not a fault
                // in the request: it is a thing that is not there.
                if let Some(gone) = e.downcast_ref::<crate::api::Absent>() {
                    return (StatusCode::NOT_FOUND, Json(ErrorDto::new(gone.to_string())))
                        .into_response();
                }
                // A name already taken in the series. Not a fault and not a thing missing:
                // a decision nobody has made, handed back with what each of the two files
                // says about itself so it can be made about the volumes.
                if let Some(collision) = e.downcast_ref::<Collision>() {
                    return (StatusCode::CONFLICT, Json(collision.clone())).into_response();
                }
                // Anything unforeseen is logged in full and answered with nothing: an error
                // message can carry a path, a query, a piece of the schema.
                tracing::error!(error = format!("{e:#}"), "unhandled failure");
                (
                    StatusCode::INTERNAL_SERVER_ERROR,
                    Json(ErrorDto::new("internal error")),
                )
                    .into_response()
            }
        }
    }
}
