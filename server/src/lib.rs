//! Leaf — a reading server for a comics library.
//!
//! A library and a thin binary rather than one `main.rs`, so the behaviour tests can reach
//! the same code the server runs. They are the oracle for the port; anything they cannot
//! see is not covered.

pub mod api;
pub mod archive;
pub mod config;
pub mod metadata;
pub mod net;
pub mod scan;
pub mod store;
