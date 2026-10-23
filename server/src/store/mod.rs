//! The read and write side of the index.

pub mod db;
pub mod devices;
pub mod files;
pub mod repository;
pub mod schema;
pub mod text;

pub use db::{Cx, Db};
pub use devices::Devices;
pub use repository::Repository;
