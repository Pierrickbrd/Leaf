//! How the server holds a connection open.
//!
//! **A connection that says nothing costs the server a socket and a task, and a few hundred
//! of them cost it the ability to accept another.** Opened and never finished — one header
//! line a minute, a body one byte at a time — they are the whole of a slow-loris attack, and
//! need no bandwidth. Two bounds close them: a request's header must arrive within
//! [`Limits::header_timeout`] (which hyper also applies to the wait for the next request on a
//! kept-alive connection, so an idle one goes too), and a request body must keep moving —
//! no more than [`Limits::body_idle`] between two pieces of it. A body is not given a total
//! time: a two-gigabyte volume over a slow link is a legitimate request that takes an hour.
//!
//! **No cap on the number of connections.** One would stop the table filling, and hand the
//! attacker the very denial it exists to prevent: they fill the cap instead. Behind a proxy or
//! a tunnel every client shares one address, so a cap per address is a cap on everybody.

use std::future::Future;
use std::net::SocketAddr;
use std::time::Duration;

use anyhow::{Context, Result};
use axum::Router;
use hyper_util::rt::{TokioExecutor, TokioTimer};
use hyper_util::server::conn::auto::Builder;
use tokio::io::{AsyncRead, AsyncWrite, ReadBuf};
use tower_http::timeout::RequestBodyTimeoutLayer;

/// The times a connection is given to say something.
#[derive(Debug, Clone, Copy)]
pub struct Limits {
    /// From the first byte of a request, or from the end of the last one, to the end of its
    /// header.
    pub header_timeout: Duration,
    /// Between two pieces of a request body.
    pub body_idle: Duration,
}

impl Default for Limits {
    fn default() -> Self {
        Limits {
            header_timeout: Duration::from_secs(10),
            body_idle: Duration::from_secs(30),
        }
    }
}

/// The app with the body bound on, for whichever server it is handed to.
pub fn bounded(app: Router, limits: Limits) -> Router {
    app.layer(RequestBodyTimeoutLayer::new(limits.body_idle))
}

/// Serves `app` on `address`, over https when `tls` is given, until `stop` ends — and
/// then gives what is in flight ten seconds to finish.
pub async fn serve<F>(
    address: SocketAddr,
    tls: Option<axum_server::tls_rustls::RustlsConfig>,
    app: Router,
    limits: Limits,
    stop: F,
) -> Result<()>
where
    F: std::future::Future<Output = ()> + Send + 'static,
{
    let app = bounded(app, limits).into_make_service_with_connect_info::<SocketAddr>();
    let handle = axum_server::Handle::new();
    let asked_to_stop = handle.clone();
    tokio::spawn(async move {
        stop.await;
        asked_to_stop.graceful_shutdown(Some(Duration::from_secs(10)));
    });
    // Bound here, so that an address that cannot be is named as such and not lost in the
    // serving.
    let listener =
        std::net::TcpListener::bind(address).with_context(|| format!("binding {address}"))?;
    listener.set_nonblocking(true)?;
    match tls {
        Some(config) => {
            only_http1(&config);
            tracing::info!("Listening on https://{address}");
            let mut server = axum_server::from_tcp_rustls(listener, config)?
                .handle(handle)
                .map(|inner| FirstWord {
                    inner,
                    wait: limits.header_timeout,
                });
            tune(server.http_builder(), limits);
            server.serve(app).await.context("serving")
        }
        None => {
            tracing::info!("Listening on http://{address}");
            let mut server = axum_server::from_tcp(listener)?
                .handle(handle)
                .map(|inner| FirstWord {
                    inner,
                    wait: limits.header_timeout,
                });
            tune(server.http_builder(), limits);
            server.serve(app).await.context("serving")
        }
    }
}

/// Every client of this server speaks HTTP/1 (a guard in `tools/` holds the client to it), and
/// HTTP/2 has none of the header bounds set below: a stream opened and never finished would
/// stay. So HTTP/2 is not offered at the handshake, and a client that begins with its preface
/// without asking is cut by [`Waiting`].
fn only_http1(config: &axum_server::tls_rustls::RustlsConfig) {
    let mut inner = (*config.get_inner()).clone();
    inner.alpn_protocols = vec![b"http/1.1".to_vec()];
    config.reload_from_config(std::sync::Arc::new(inner));
}

fn tune(builder: &mut Builder<TokioExecutor>, limits: Limits) {
    builder
        .http1()
        .timer(TokioTimer::new())
        .header_read_timeout(limits.header_timeout);
}

/// Gives a connection `wait` to send its first byte, once it is established — after the
/// handshake over https, since a client can finish that and then say nothing. hyper's header
/// timeout starts with the first byte of a request, so a socket opened and never written to
/// would otherwise be held for ever: the plainest form of the attack.
#[derive(Clone)]
struct FirstWord<A> {
    inner: A,
    wait: Duration,
}

impl<A, I, S> axum_server::accept::Accept<I, S> for FirstWord<A>
where
    A: axum_server::accept::Accept<I, S>,
    A::Future: Send + 'static,
    A::Stream: AsyncRead + AsyncWrite + Unpin + Send + 'static,
    A::Service: Send + 'static,
{
    type Stream = Waiting<A::Stream>;
    type Service = A::Service;
    type Future = std::pin::Pin<
        Box<dyn std::future::Future<Output = std::io::Result<(Self::Stream, A::Service)>> + Send>,
    >;

    fn accept(&self, stream: I, service: S) -> Self::Future {
        let accepted = self.inner.accept(stream, service);
        let wait = self.wait;
        Box::pin(async move {
            let (stream, service) = accepted.await?;
            Ok((Waiting::new(stream, wait), service))
        })
    }
}

/// A stream whose first read fails with `TimedOut` if nothing has arrived by the deadline, and
/// whose reads fail if it opens with the HTTP/2 preface. hyper-util's `http1_only` is ignored
/// by the connection kind axum-server serves (the one that allows upgrades), which reads the
/// preface and starts HTTP/2 regardless — measured: a bare preface was answered with SETTINGS.
struct Waiting<T> {
    inner: T,
    deadline: Option<std::pin::Pin<Box<tokio::time::Sleep>>>,
    /// The first four bytes, until they are known: `PRI ` opens the HTTP/2 preface.
    head: Vec<u8>,
}

impl<T> Waiting<T> {
    fn new(inner: T, wait: Duration) -> Self {
        Waiting {
            inner,
            deadline: Some(Box::pin(tokio::time::sleep(wait))),
            head: Vec::with_capacity(4),
        }
    }
}

impl<T: AsyncRead + Unpin> AsyncRead for Waiting<T> {
    fn poll_read(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
        buf: &mut ReadBuf<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        use std::task::Poll;
        let before = buf.filled().len();
        match std::pin::Pin::new(&mut self.inner).poll_read(cx, buf) {
            Poll::Ready(done) => {
                if buf.filled().len() > before {
                    let room = 4 - self.head.len();
                    let fresh = &buf.filled()[before..];
                    let take = room.min(fresh.len());
                    self.head.extend_from_slice(&fresh[..take]);
                    if self.head == b"PRI " {
                        return Poll::Ready(Err(std::io::Error::new(
                            std::io::ErrorKind::InvalidData,
                            "HTTP/2 is not offered",
                        )));
                    }
                    // The deadline stands while the bytes so far could still open the HTTP/2
                    // preface: hyper-util waits for 24 of them before its own header timer
                    // starts, so a lone `P` and then silence would otherwise be held for ever.
                    if !b"PRI ".starts_with(&self.head) {
                        self.deadline = None;
                    }
                }
                Poll::Ready(done)
            }
            Poll::Pending => {
                let late = match self.deadline.as_mut() {
                    Some(sleep) => sleep.as_mut().poll(cx).is_ready(),
                    None => false,
                };
                if late {
                    Poll::Ready(Err(std::io::Error::new(
                        std::io::ErrorKind::TimedOut,
                        "a connection that said nothing",
                    )))
                } else {
                    Poll::Pending
                }
            }
        }
    }
}

impl<T: AsyncWrite + Unpin> AsyncWrite for Waiting<T> {
    fn poll_write(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
        buf: &[u8],
    ) -> std::task::Poll<std::io::Result<usize>> {
        std::pin::Pin::new(&mut self.inner).poll_write(cx, buf)
    }
    fn poll_flush(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        std::pin::Pin::new(&mut self.inner).poll_flush(cx)
    }
    fn poll_shutdown(
        mut self: std::pin::Pin<&mut Self>,
        cx: &mut std::task::Context<'_>,
    ) -> std::task::Poll<std::io::Result<()>> {
        std::pin::Pin::new(&mut self.inner).poll_shutdown(cx)
    }
}
