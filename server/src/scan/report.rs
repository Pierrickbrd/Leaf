//! What a scan found, and what it could not make sense of.
//!
//! The counts describe **the library**, not the work done on it: an unchanged rescan still
//! reports fifty thousand chapters, because that is how many there are. The lists describe
//! what a person might want to fix.

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

impl ScanReport {
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
