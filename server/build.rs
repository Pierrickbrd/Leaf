use std::fs;

fn main() {
    println!("cargo:rerun-if-changed=../VERSION");

    let version = fs::read_to_string("../VERSION")
        .expect("reading the repository VERSION file")
        .trim()
        .to_owned();
    let package = std::env::var("CARGO_PKG_VERSION").expect("Cargo package version");
    assert_eq!(
        version, package,
        "VERSION and server/Cargo.toml disagree; release both components together"
    );
    println!("cargo:rustc-env=LEAF_VERSION={version}");
}
