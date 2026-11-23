//! A connection that says nothing is not allowed to keep a socket for ever.

use std::net::SocketAddr;
use std::time::Duration;

use axum::routing::{get, post};
use axum::Router;
use leaf_server::serving::{serve, Limits};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::net::TcpStream;

const SHORT: Limits = Limits {
    header_timeout: Duration::from_millis(300),
    body_idle: Duration::from_millis(300),
};

async fn running() -> SocketAddr {
    let free = std::net::TcpListener::bind("127.0.0.1:0").unwrap();
    let address = free.local_addr().unwrap();
    drop(free);
    let app = Router::new().route("/", get(|| async { "here" })).route(
        "/body",
        post(|body: axum::body::Bytes| async move { body.len().to_string() }),
    );
    tokio::spawn(serve(address, None, app, SHORT, std::future::pending()));
    // Listening before the first connection is made.
    for _ in 0..50 {
        if TcpStream::connect(address).await.is_ok() {
            break;
        }
        tokio::time::sleep(Duration::from_millis(20)).await;
    }
    address
}

/// What the server does with a connection once it has been quiet: closes it, or answers and
/// closes it. Either way the read ends, which is the point; it is never still waiting.
async fn ends_within(stream: &mut TcpStream, wait: Duration) -> bool {
    let mut buffer = [0u8; 512];
    loop {
        match tokio::time::timeout(wait, stream.read(&mut buffer)).await {
            Err(_) => return false,
            Ok(Ok(0)) | Ok(Err(_)) => return true,
            Ok(Ok(_)) => continue,
        }
    }
}

#[tokio::test]
async fn an_ordinary_request_is_answered() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();

    stream
        .write_all(b"GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n")
        .await
        .unwrap();

    let mut said = String::new();
    stream.read_to_string(&mut said).await.unwrap();
    assert!(said.contains("here"), "{said}");
}

#[tokio::test]
async fn a_header_that_never_finishes_is_cut() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();
    stream
        .write_all(b"GET / HTTP/1.1\r\nHost: x\r\n")
        .await
        .unwrap();

    assert!(ends_within(&mut stream, Duration::from_secs(3)).await);
}

#[tokio::test]
async fn a_connection_that_sends_nothing_is_cut() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();

    assert!(ends_within(&mut stream, Duration::from_secs(3)).await);
}

#[tokio::test]
async fn a_body_that_stops_moving_is_cut() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();
    stream
        .write_all(b"POST /body HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\nx")
        .await
        .unwrap();

    assert!(ends_within(&mut stream, Duration::from_secs(3)).await);
}

#[tokio::test]
async fn a_slow_body_that_keeps_moving_is_not() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();
    stream
        .write_all(
            b"POST /body HTTP/1.1\r\nHost: x\r\nConnection: close\r\nContent-Length: 6\r\n\r\n",
        )
        .await
        .unwrap();
    for piece in [b"a", b"b", b"c", b"d", b"e", b"f"] {
        tokio::time::sleep(Duration::from_millis(150)).await;
        stream.write_all(piece).await.unwrap();
    }

    let mut said = String::new();
    stream.read_to_string(&mut said).await.unwrap();
    assert!(said.ends_with('6'), "{said}");
}

/// The preface HTTP/2 begins with, sent over a plain connection by a client that did not ask.
/// With HTTP/2 offered it would be accepted, and a stream opened after it has no header bound.
#[tokio::test]
async fn an_http2_preface_is_not_taken_for_a_request() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();

    stream
        .write_all(b"PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n")
        .await
        .unwrap();

    let mut buffer = [0u8; 512];
    let answered = tokio::time::timeout(Duration::from_secs(3), stream.read(&mut buffer)).await;
    assert!(
        matches!(answered, Ok(Ok(0)) | Ok(Err(_))),
        "an HTTP/2 preface was answered: {answered:?}"
    );
}

/// One byte that could open the HTTP/2 preface, then nothing: hyper-util keeps reading to tell
/// the protocols apart, and its own header timer has not started.
#[tokio::test]
async fn a_lone_byte_of_what_could_be_a_preface_is_cut_all_the_same() {
    let address = running().await;
    let mut stream = TcpStream::connect(address).await.unwrap();

    stream.write_all(b"P").await.unwrap();

    assert!(
        ends_within(&mut stream, Duration::from_secs(3)).await,
        "a connection that began a preface and stopped was held"
    );
}
