//! What a scan found, and what it could not make sense of.
//!
//! The counts describe **the library**, not the work done on it: an unchanged rescan still
//! reports fifty thousand chapters, because that is how many there are. The lists describe
//! what a person might want to fix.
use serde::Serialize;

#[derive(Debug, Default, Clone)]
pub struct ScanReport {
    pub duplicate_numbers: Vec<String>,
    pub chapters_without_start_page: Vec<String>,
    pub entries_without_metadata: Vec<String>,
    pub duplicate_page_names: Vec<String>,
    pub derived_arcs: Vec<String>,
    pub missing_required: Vec<String>,
    /// Files that were read and then disregarded, because of where they sit.
    ///
    /// The counterpart of `missing_required`, and the one that was absent: that list says
    /// what a folder failed to declare, and nothing said what a folder declared in vain. A
    /// `universe.json` one level too deep, an `edition.json` on a folder that turns out to
    /// hold an implicit edition — both are read, both are partly ignored, and both used to
    /// be ignored in silence, which is the shape of an afternoon spent wondering why an
    /// edit changes nothing.
    pub disregarded: Vec<String>,
    /// Fields that are there, and disagree — with each other, or with the file holding them.
    ///
    /// The third of three lists, and each points at a different fix. `missing_required` says
    /// what a folder failed to declare; `disregarded` what it declared in vain; this one what
    /// it declared twice and differently. None of them stops a scan: a library describes
    /// itself as well as it describes itself, and the answer is to say so once.
    pub contradictions: Vec<String>,
    pub identity_mismatch: Vec<String>,
    pub errors: Vec<String>,

    pub universes: u32,
    pub works: u32,
    pub editions: u32,
    pub entries: u32,
    pub chapters: u32,
    pub pages: u32,
    /// How many entries were opened and read rather than skipped as unchanged.
    pub reanalysed: u32,
}

/// What a scan found, in numbers and named lists rather than in a paragraph.
///
/// The paragraph still exists — `summary()` writes it, and a terminal is exactly where a
/// paragraph belongs. A screen is not: the client words its own French, every string of it
/// living in one file that a typography test can sweep, and a sentence arriving from the
/// server would be the one string that file could not reach.
///
/// So the counts and the *kind* of each list cross the wire, and the client says them in
/// French. What does not cross translated is each item's own line — « Death Note/Tome 1.cbz
/// — no number » is about one file, written by whoever met it, and turning every one of
/// those into a code with parameters is a contract the size of the scanner itself.
#[derive(Debug, Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ScanCounts {
    pub universes: u32,
    pub works: u32,
    pub editions: u32,
    pub entries: u32,
    pub chapters: u32,
    pub pages: u32,
    pub reanalysed: u32,
}

#[derive(Debug, Clone, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Finding {
    /// A word the client has a French sentence for. Unknown ones are shown by their items
    /// alone rather than dropped: the scan found something either way.
    pub kind: &'static str,
    /// How many there are, which is not how many are listed.
    pub total: usize,
    /// The first sixteen, as whoever met them wrote them. The same ceiling the paragraph
    /// uses: a library with four hundred untitled volumes should not send four hundred
    /// lines to say so.
    pub items: Vec<String>,
}

#[derive(Debug, Clone, Default, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct Findings {
    pub counts: ScanCounts,
    #[serde(skip_serializing_if = "Vec::is_empty")]
    pub findings: Vec<Finding>,
    #[serde(skip_serializing_if = "is_zero")]
    pub chapters_without_start_page: usize,
    /// Set when the scan itself failed. The failure reaches the client rather than only the
    /// log: a scan that quietly did nothing is worse than one that says why.
    #[serde(skip_serializing_if = "Option::is_none")]
    pub failure: Option<String>,
}

fn is_zero(value: &usize) -> bool {
    *value == 0
}

/// The most a list sends. Sixteen is what the paragraph shows, and a screen has no more
/// reason than a terminal to hold four hundred lines about one mistake repeated.
const MOST: usize = 16;

impl ScanReport {
    /// The same report, for a screen instead of a terminal.
    pub fn findings(&self) -> Findings {
        let mut found = Findings {
            counts: ScanCounts {
                universes: self.universes,
                works: self.works,
                editions: self.editions,
                entries: self.entries,
                chapters: self.chapters,
                pages: self.pages,
                reanalysed: self.reanalysed,
            },
            chapters_without_start_page: self.chapters_without_start_page.len(),
            ..Findings::default()
        };
        let mut add = |kind: &'static str, items: &[String]| {
            if items.is_empty() {
                return;
            }
            found.findings.push(Finding {
                kind,
                total: items.len(),
                items: items.iter().take(MOST).cloned().collect(),
            });
        };
        // The same order the paragraph uses, worst first: what stopped a shelf being read
        // before what a file failed to say about itself.
        add("ERRORS", &self.errors);
        add("MISSING_METADATA", &self.missing_required);
        add("DISREGARDED", &self.disregarded);
        add("CONTRADICTIONS", &self.contradictions);
        add("IDENTITY", &self.identity_mismatch);
        add("WITHOUT_METADATA", &self.entries_without_metadata);
        add("DUPLICATE_NUMBERS", &self.duplicate_numbers);
        add("DUPLICATE_PAGES", &self.duplicate_page_names);
        add("DERIVED_ARCS", &self.derived_arcs);
        found
    }

    pub fn summary(&self) -> String {
        let mut out = format!(
            "{} universe(s), {} work(s), {} edition(s), {} entry(ies), {} chapter(s), {} page(s)\n\
             {} entry(ies) reanalysed",
            self.universes,
            self.works,
            self.editions,
            self.entries,
            self.chapters,
            self.pages,
            self.reanalysed
        );
        let mut section = |title: &str, items: &[String]| {
            if items.is_empty() {
                return;
            }
            out.push_str(&format!("\n\n{title} ({}):\n", items.len()));
            for item in items.iter().take(16) {
                out.push_str(&format!("\t· {item}\n"));
            }
            if items.len() > 16 {
                out.push_str(&format!("\t… and {} more\n", items.len() - 16));
            }
        };
        section("Errors", &self.errors);
        section("Missing metadata", &self.missing_required);
        section("Read, and disregarded", &self.disregarded);
        section("Saying two things at once", &self.contradictions);
        section(
            "Identity does not match the folder",
            &self.identity_mismatch,
        );
        section("Entries describing nothing", &self.entries_without_metadata);
        section("Chapter numbers claimed twice", &self.duplicate_numbers);
        section("Page names claimed twice", &self.duplicate_page_names);
        section(
            "Arcs derived from <StoryArc>, therefore per volume",
            &self.derived_arcs,
        );
        if !self.chapters_without_start_page.is_empty() {
            out.push_str(&format!(
                "\n\nChapters without a start page: {}",
                self.chapters_without_start_page.len()
            ));
        }
        out
    }
}
