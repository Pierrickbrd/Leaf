//! The endpoints.
//!
//! Reading is open to any recognised key; importing requires a key that carries that right
//! — which is what makes "import from the desktop only" a rule the server enforces, rather
//! than a button missing from the mobile interface.
//!
//! The guard is an **extractor**, so it appears in the handler's signature: a route that
//! reads takes a [`Reader`], one that writes takes an [`Importer`]. A handler missing one
//! is a handler that reads as unguarded, which is the property the Kotlin got from spelling
//! the check out on every route — without spelling anything out.

use std::net::SocketAddr;
use std::sync::Arc;

use axum::body::Body;
use axum::extract::{ConnectInfo, FromRequestParts, Path, State};
use axum::http::request::Parts;
use axum::http::StatusCode;
use axum::response::{IntoResponse, Response};
use axum::routing::{get, post};
use axum::{Json, Router};
use http_body_util::BodyExt;

use super::bulk_import::{BulkImport, CleanupRequest, ImportRequest, ReceiveError};
use super::dto::{
    ArcDto, ChapterDto, EntryDto, ErrorDto, FacetsDto, HealthDto, PageDto, SearchHitDto,
    SeriesFilter, SeriesPageDto, SeriesSort, API_VERSION, FORMAT_VERSION,
};
use super::intake::{Collision, FileRequest, Intake, Proposal};
use super::keys::{Keys, Permission, HEADER};
use super::local_drop::{DropListing, DropRequest, LocalDrop};
use super::pages::{Pages, ServedImage};
use super::progress::{Progress, ProgressDto, ProgressPatch, UpNextDto};
use super::records::{EntryPatch, Records, SeriesPatch};
use super::throttle::Throttle;
use crate::metadata::sidecars::ArcJson;
use crate::scan::layout;
use crate::scan::runner::{ScanRunner, ScanStatus};
use crate::scan::scanner::Scanner;
use crate::store::{Db, Repository};

/// Enough for any grid, small enough that a thousand series never cross the wire at once.
const DEFAULT_PAGE: i64 = 100;
const MAX_PAGE: i64 = 500;

#[derive(Clone)]
pub struct AppState {
    pub db: Arc<Db>,
    pub keys: Arc<Keys>,
    pub throttle: Arc<Throttle>,
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
            bulk: Arc::new(BulkImport::new(&inbox, std::path::Path::new("library"))),
            drop: Arc::new(LocalDrop::new(None, Arc::clone(&intake))),
            max_upload_bytes: 2048 * 1024 * 1024,
            trust_proxy: false,
            intake,
            db,
            keys: Arc::new(keys),
            throttle: Arc::new(Throttle::default()),
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
        self.bulk = Arc::new(BulkImport::new(inbox, library));
        self.drop = Arc::new(LocalDrop::new(drop_folder, Arc::clone(&intake)));
        self.intake = intake;
        self.max_upload_bytes = max_upload_bytes;
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
        .route("/series", get(list_series))
        .route("/filters", get(get_filters))
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
        .route("/series/{id}", get(get_series).patch(patch_series))
        .route("/series/{id}/arcs", get(list_series_arcs).patch(patch_arcs))
        .route("/entries/{id}", get(get_entry).patch(patch_entry))
        .route("/drop", get(list_drop).post(take_from_drop))
        .route("/entries", post(receive_entry))
        // A staged file is not an entry: it is a proposal awaiting a confirmation, and
        // /intake/{id} says so. The old spelling shared a shape with /entries/{id}/file,
        // which only ever worked by the router matching the literal segment first.
        .route("/intake/{id}", axum::routing::delete(abandon_entry))
        .route("/intake/{id}/file", post(file_entry))
        .route("/import", get(list_imports).post(open_import))
        .route("/intake", get(list_intake))
        .route("/import/{id}", get(import_state).delete(abandon_import))
        .route("/import/{id}/file", axum::routing::put(receive_import_file))
        .route("/import/{id}/commit", post(commit_import))
        .route("/cleanup", post(cleanup))
        .route("/search", get(search))
        .route("/next", get(up_next))
        .route("/series/{id}/progress", get(series_progress))
        .route(
            "/entries/{id}/progress",
            get(entry_progress)
                .patch(record_progress)
                .delete(forget_progress),
        )
        .with_state(state)
}

// -------------------------------------------------------------------- guards

/// Any recognised key is welcome, but one is required.
pub struct Reader;

/// A key that carries the import right.
pub struct Importer;

impl FromRequestParts<AppState> for Reader {
    type Rejection = Refused;
    async fn from_request_parts(parts: &mut Parts, state: &AppState) -> Result<Self, Refused> {
        allowed(parts, state, Permission::Read).map(|()| Reader)
    }
}

impl FromRequestParts<AppState> for Importer {
    type Rejection = Refused;
    async fn from_request_parts(parts: &mut Parts, state: &AppState) -> Result<Self, Refused> {
        allowed(parts, state, Permission::Import).map(|()| Importer)
    }
}

fn allowed(parts: &Parts, state: &AppState, permission: Permission) -> Result<(), Refused> {
    if state.keys.open() {
        return Ok(());
    }
    let address = caller(parts, state.trust_proxy);

    if let Some(wait) = state.throttle.blocked_for(&address) {
        return Err(Refused::TooMany(wait.as_secs().max(1)));
    }

    let offered = parts.headers.get(HEADER).and_then(|v| v.to_str().ok());
    let Some(key) = state.keys.recognise(offered) else {
        state.throttle.record_failure(&address);
        return Err(Refused::Forbidden("unknown key".into()));
    };
    // A key that works clears the slate — the device was misconfigured, not hostile.
    state.throttle.record_success(&address);

    if key.permissions.contains(&permission) {
        return Ok(());
    }
    // A valid key asking for a right it has not got is not a failed attempt: nothing is
    // being guessed, so nothing should be counted against it.
    Err(Refused::Forbidden(format!(
        "this key does not carry the \"{}\" right",
        permission.name()
    )))
}

/// Who the throttle counts a request against.
///
/// The socket, unless a proxy is trusted — in which case the leftmost `X-Forwarded-For`
/// entry, which is the one the proxy wrote about its own client. Reading it untrusted would
/// let any caller claim any address and walk past the throttle, so it is opt-in and the
/// socket is what stands otherwise.
fn caller(parts: &Parts, trust_proxy: bool) -> String {
    if trust_proxy {
        if let Some(claimed) = parts
            .headers
            .get("x-forwarded-for")
            .and_then(|v| v.to_str().ok())
            .and_then(|v| v.split(',').next())
            .map(str::trim)
            .filter(|v| !v.is_empty())
        {
            return claimed.to_string();
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
        }
    }
}

// -------------------------------------------------------------------- routes

/// What the server is, and whether it is up. The only route that answers without a key.
async fn health(State(state): State<AppState>) -> Result<Json<HealthDto>, Failure> {
    let local_drop = state.drop.enabled();
    let library = blocking(move || {
        // Counted, not built: reading the length of a list of every series was a real
        // defect on the one route anyone can call.
        state.db.read(|cx| {
            Ok(cx
                .query_one("SELECT COUNT(*) FROM edition", [], |r| r.get::<_, i64>(0))?
                .unwrap_or(0))
        })
    })
    .await?;

    Ok(Json(HealthDto {
        status: "ok",
        api: API_VERSION,
        format: FORMAT_VERSION,
        library,
        local_drop,
    }))
}

/// The shelf, filtered, sorted and paged.
async fn list_series(
    _: Reader,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<SeriesPageDto>, Failure> {
    let filter = query.filter;
    let sort = SeriesSort::of(query.sort.as_deref());
    // A default that bounds the answer rather than one that hides part of it: `total`
    // always says how many there are, so a client can see it has only part and ask for the
    // rest. size=0 asks for everything on purpose.
    let size = query.size.unwrap_or(DEFAULT_PAGE).clamp(0, MAX_PAGE);
    let page = query.page.unwrap_or(0).max(0);

    let page_dto = blocking(move || {
        let repository = Repository::new(&state.db);
        Ok(SeriesPageDto {
            items: repository.series(&filter, sort, size, page * size)?,
            total: repository.count_series(&filter)?,
            page,
            size,
        })
    })
    .await?;
    Ok(Json(page_dto))
}

/// The values the filters can take, so the application can offer them rather than make you
/// spell them.
async fn get_filters(_: Reader, State(state): State<AppState>) -> Result<Json<FacetsDto>, Failure> {
    let facets = blocking(move || Repository::new(&state.db).facets()).await?;
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
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing(
        blocking(move || Repository::new(&state.db).one_series(&id)).await?,
        "unknown series",
    )
}

/// Volumes and standalone chapters, mixed, in reading order.
async fn list_series_entries(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<EntryDto>>, Failure> {
    Ok(Json(
        blocking(move || Repository::new(&state.db).entries(&id)).await?,
    ))
}

/// The whole edition sequence, whatever the materialisation.
async fn list_series_chapters(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<ChapterDto>>, Failure> {
    Ok(Json(
        blocking(move || Repository::new(&state.db).chapters_of_edition(&id)).await?,
    ))
}

async fn list_series_arcs(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<ArcDto>>, Failure> {
    Ok(Json(
        blocking(move || Repository::new(&state.db).arcs(&id)).await?,
    ))
}

async fn get_entry(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    or_missing(
        blocking(move || Repository::new(&state.db).entry(&id)).await?,
        "unknown entry",
    )
}

/// A volume's markers — what no tool can read today.
async fn list_entry_chapters(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<ChapterDto>>, Failure> {
    Ok(Json(
        blocking(move || Repository::new(&state.db).chapters_of_entry(&id)).await?,
    ))
}

/// The pages of an entry.
///
/// `?width` tells the server what the pages will be asked for at, so the first of them can
/// be prepared while the client is still drawing the list. Without it nothing is prepared:
/// a guessed width is work thrown away.
async fn list_entry_pages(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
) -> Result<Json<Vec<PageDto>>, Failure> {
    let width = query.width;
    Ok(Json(
        blocking(move || {
            state.pages.warm_opening(&id, width);
            Repository::new(&state.db).pages(&id)
        })
        .await?,
    ))
}

// -------------------------------------------------------------- maintenance

/// Where the scan is, and what the last one found.
async fn scan_status(_: Reader, State(state): State<AppState>) -> Json<ScanStatus> {
    Json(state.runner.status())
}

/// Starts a scan.
///
/// Answers at once and scans behind: a full library takes tens of seconds, which no client
/// should hold a connection open for. One at a time — a second request is told so rather
/// than queued, because the second would read the same disk.
async fn start_scan(_: Importer, State(state): State<AppState>) -> Response {
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
    _: Reader,
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
    let served = blocking(move || {
        let served = state.pages.page(&id, number, width);
        // Only when a width was asked for: serving the source costs nothing to begin with.
        // And never from a cover or from the warming itself — without that distinction the
        // preparation fed itself, and fetching a grid of covers quietly resized the opening
        // pages of every volume in the library.
        if let Some(width) = width {
            state.pages.warm_ahead(&id, number, width);
        }
        served
    })
    .await?;
    Ok(image_response(served, &headers))
}

/// The cover, small by default. A thumbnail is a page asked for narrow.
async fn entry_cover(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
) -> Result<Response, Failure> {
    let width = query.width;
    let served = blocking(move || state.pages.cover(&id, width)).await?;
    Ok(image_response(served, &headers))
}

/// The tile of a grid: one request, not one to find the first entry and another for its
/// cover.
async fn series_cover(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
    query: ListQuery,
    headers: axum::http::HeaderMap,
) -> Result<Response, Failure> {
    let width = query.width;
    let served = blocking(move || state.pages.series_cover(&id, width)).await?;
    Ok(image_response(served, &headers))
}

/// The original file: this is how a volume comes back down to be retouched.
async fn download_entry_file(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let path = blocking(move || {
        // It leaves stamped — work, edition, number — so it can find its own way home when
        // it comes back. A GET that writes: deliberate,
        // because the stamp has to be in the copy that leaves rather than the one that
        // stays.
        Records::new(&state.db).stamp_entry(&id)?;
        Repository::new(&state.db).entry_path(&id)
    })
    .await?;
    let Some(path) = path else {
        return Err(Failure::Missing("unknown entry"));
    };

    let name = std::path::Path::new(&path)
        .file_name()
        .map(|n| n.to_string_lossy().to_string())
        .unwrap_or_else(|| "volume.cbz".to_string());
    let bytes = tokio::fs::read(&path)
        .await
        .map_err(|e| Failure::Unhandled(e.into()))?;

    Ok((
        [
            (
                axum::http::header::CONTENT_TYPE,
                "application/octet-stream".to_string(),
            ),
            (
                axum::http::header::CONTENT_DISPOSITION,
                format!("attachment; filename=\"{name}\""),
            ),
        ],
        bytes,
    )
        .into_response())
}

/// The tag carries the file's modification time, so a cached page can be trusted for as
/// long as the client cares to keep it.
fn image_response(served: Option<ServedImage>, headers: &axum::http::HeaderMap) -> Response {
    let Some(image) = served else {
        return (StatusCode::NOT_FOUND, Json(ErrorDto::new("unknown page"))).into_response();
    };
    let offered = headers
        .get(axum::http::header::IF_NONE_MATCH)
        .and_then(|v| v.to_str().ok())
        .map(|v| v.trim_matches('"'));
    if offered == Some(image.tag.as_str()) {
        // The freshness headers go on the 304 too: a client that revalidates and gets a
        // bare 304 has learnt nothing about how long it may now keep what it has.
        return (
            StatusCode::NOT_MODIFIED,
            [
                (axum::http::header::ETAG.as_str(), image.tag),
                (
                    axum::http::header::CACHE_CONTROL.as_str(),
                    "private, max-age=31536000".to_string(),
                ),
            ],
        )
            .into_response();
    }
    (
        [
            (axum::http::header::ETAG.as_str(), image.tag),
            (
                axum::http::header::CACHE_CONTROL.as_str(),
                "private, max-age=31536000".to_string(),
            ),
            (axum::http::header::CONTENT_TYPE.as_str(), image.media_type),
        ],
        image.bytes,
    )
        .into_response()
}

// ------------------------------------------------------------------- search

/// Find something by name.
///
/// The shelf's own filters apply: a search runs *inside* what is showing, not beside it. A
/// lit chip is a statement about what you are looking at, and a search that ignored it would
/// hand back series the shelf behind it is hiding.
async fn search(
    _: Reader,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<Vec<SearchHitDto>>, Failure> {
    let hits = blocking(move || {
        Repository::new(&state.db).search(
            query.q.as_deref().unwrap_or_default(),
            query.limit.unwrap_or(40),
            &query.kind,
            &query.filter,
        )
    })
    .await?;
    Ok(Json(hits))
}

// ----------------------------------------------------------------- progress

/// Where the reader stands in one entry.
async fn entry_progress(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Response, Failure> {
    let found = blocking(move || Progress::new(&state.db).of(&id)).await?;
    Ok(match found {
        Some(progress) => Json(progress).into_response(),
        // Never opened. Not an error, and not an empty object either.
        None => StatusCode::NO_CONTENT.into_response(),
    })
}

/// Records a position.
///
/// Recording progress is part of reading, so a read-only key is allowed to do it —
/// otherwise the phone could not remember where you stopped.
async fn record_progress(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
    Json(patch): Json<ProgressPatch>,
) -> Result<Response, Failure> {
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_millis() as i64)
        .unwrap_or(0);
    or_missing(
        blocking(move || Progress::new(&state.db).record(&id, &patch, now)).await?,
        "unknown entry",
    )
}

async fn forget_progress(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<StatusCode, Failure> {
    blocking(move || Progress::new(&state.db).forget(&id)).await?;
    Ok(StatusCode::NO_CONTENT)
}

/// Where the reader stands in every entry of a series.
async fn series_progress(
    _: Reader,
    State(state): State<AppState>,
    Path(id): Path<String>,
) -> Result<Json<Vec<ProgressDto>>, Failure> {
    Ok(Json(
        blocking(move || Progress::new(&state.db).of_series(&id)).await?,
    ))
}

/// What to open next: what you are in the middle of, then what follows it.
async fn up_next(
    _: Reader,
    State(state): State<AppState>,
    query: ListQuery,
) -> Result<Json<Vec<UpNextDto>>, Failure> {
    let limit = query.limit.unwrap_or(20);
    Ok(Json(
        blocking(move || Progress::new(&state.db).up_next(limit)).await?,
    ))
}

// ------------------------------------------------- records, no file transfer

/// Edits what a series says about itself.
///
/// The edit goes into a sidecar on the disk, never into the index; the rescan that follows
/// reads it back. See [`super::records`] for why.
async fn patch_series(
    _: Importer,
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
        Repository::new(&state.db).one_series(&id)
    })
    .await?;
    or_missing(series, "unknown series")
}

/// Replaces a series' arcs outright: an arc list is a whole, and patching one entry of it
/// would need a name for each, which the sidecar deliberately does not have.
async fn patch_arcs(
    _: Importer,
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
        Repository::new(&state.db).arcs(&id).map(Some)
    })
    .await?;
    or_missing(arcs, "unknown series")
}

async fn patch_entry(
    _: Importer,
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
        Repository::new(&state.db).entry(&id)
    })
    .await?;
    or_missing(entry, "unknown entry")
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

/// Drop it, the server proposes a destination. Nothing has moved yet.
async fn receive_entry(
    _: Importer,
    State(state): State<AppState>,
    headers: axum::http::HeaderMap,
    body: Body,
) -> Result<Json<Proposal>, Failure> {
    let Some(name) = headers
        .get("X-Leaf-Name")
        .and_then(|v| v.to_str().ok())
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
    // A number, like every other count in this API. The Kotlin spelled it as text because
    // it built the response from a Map<String, String>, which was an accident of the type
    // rather than a decision — wart 2, decided at the port while there is no client to
    // break.
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
) -> Result<Response, Failure> {
    let result = blocking(move || {
        let result = state.bulk.commit(&id)?;
        // Aimed at what has just arrived rather than at the whole library. A full scan here
        // costs as much as the library is big — fifteen seconds at two hundred series — and
        // the request held the connection open for all of it, to index one folder.
        let target = state.bulk.target_of(&result.root);
        if can_be_aimed_at(&target) {
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
    _: Importer,
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
