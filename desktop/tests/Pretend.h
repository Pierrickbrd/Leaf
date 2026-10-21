#pragma once

// A server of forty lines, for the tests that need one.
//
// Shared rather than copied: it started inside `talks_to_the_server.cpp`, and the shelf needs
// the same thing — something that answers a known body over a real socket, so what is being
// tested is the client's own behaviour and not a mock of the client's own behaviour.

#include <QByteArray>
#include <QList>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>

#include <algorithm>
#include <functional>
#include <utility>

/// Answers with whatever it was told to, and remembers what it was asked.
///
/// `heard` accumulates across connections, so a test reading it between two requests clears
/// it first; `answer` is read at the moment of writing, so a test can change what comes back
/// between one request and the next. `answerFor` is the route-aware form for tests where two
/// resources on that same server deliberately have different representations.
class Pretend : public QTcpServer
{
    Q_OBJECT

public:
    QByteArray answer;
    QByteArray heard;
    std::function<QByteArray(const QByteArray &)> answerFor;
    /// Requests kept unanswered until `release()`, for a test that needs an answer to land
    /// *after* something else happened — an order the network would otherwise choose.
    std::function<bool(const QByteArray &)> holdBack;

    /// Answers everything `holdBack` kept, now, in the order it was asked — or the opposite
    /// order with `backwards`, for a test that needs the older answer to land last.
    void release(bool backwards = false)
    {
        auto kept = std::exchange(m_held, {});
        if (backwards)
            std::reverse(kept.begin(), kept.end());
        for (const auto &[socket, one] : kept) {
            if (socket && socket->state() == QAbstractSocket::ConnectedState)
                reply(socket, one, true);
        }
    }

    /// Built rather than typed, because a hand-counted Content-Length is a way to fail a
    /// test for a reason that has nothing to do with what it is testing.
    void answers(int status, const QByteArray &body, const QByteArray &extra = {})
    {
        answer = "HTTP/1.1 " + QByteArray::number(status) + " .\r\n"
                 "Content-Type: application/json\r\n" + extra
                 + "Content-Length: " + QByteArray::number(body.size()) + "\r\n\r\n" + body;
    }

    void incomingConnection(qintptr handle) override
    {
        auto *socket = new QTcpSocket(this);
        socket->setSocketDescriptor(handle);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this,
                [this, socket, request = QByteArray{}]() mutable {
            const QByteArray arrived = socket->readAll();
            request += arrived;
            heard += arrived;
            // Every request in the buffer, and not the first alone. A client that keeps a
            // connection alive sends the next one down it without waiting, and three of them
            // arrived in a single read the first time a screen asked for three things at
            // once: answering one and going quiet read, on the client side, as a server that
            // stopped — and as « the server said something this client cannot read », because
            // the one answer written went to the wrong question.
            while (answerOne(socket, request)) {
            }
        });
    }

private:
    /// Answers the request at the front of the buffer and takes it off, or says there is not
    /// a whole one there yet.
    bool answerOne(QTcpSocket *socket, QByteArray &request)
    {
            const qsizetype headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return false;
            }
            // A body several megabytes wide — the folder send path's own chunk — arrives
            // in more than the one `readyRead` headers alone would ever need, and the
            // first of those already contains the blank line the check above looks for.
            // Answering there closed the connection out from under a client still writing
            // the rest of it, which read on the client side as the transfer having failed
            // rather than as this deliberately tiny server jumping the gun. `Content-Length`
            // is what the request itself says its body is, so waiting for that many bytes
            // past the header is the one honest way to know it has all arrived.
            qint64 contentLength = 0;
            for (const QByteArray &line : request.left(headerEnd).split('\n')) {
                const QByteArray trimmed = line.trimmed();
                if (!trimmed.toLower().startsWith("content-length:"))
                    continue;
                contentLength = trimmed.mid(trimmed.indexOf(':') + 1).trimmed().toLongLong();
                break;
            }
            const qsizetype whole = headerEnd + 4 + contentLength;
            if (request.size() < whole) {
                return false;
            }
            // A cover leaving GridView's one-row buffer cancels its request. The peer can
            // disappear after sending the headers and before this deliberately tiny server
            // gets to reply; that is success for the client, not a socket warning in a test.
            if (socket->state() != QAbstractSocket::ConnectedState)
                return false;
            const QByteArray one = request.left(whole);
            request.remove(0, whole);
            if (holdBack && holdBack(one)) {
                m_held.append({socket, one});
                return !request.isEmpty();
            }
            return reply(socket, one, request.isEmpty()) && !request.isEmpty();
    }

    /// Writes the answer to one request, and hangs up after it when it is the `last`.
    /// False when there is nothing to say, which holds the connection open.
    bool reply(QTcpSocket *socket, const QByteArray &one, bool last)
    {
            QByteArray reply = answerFor ? answerFor(one) : answer;
            // Nothing to say is a server that never answers, so the connection is held open
            // rather than closed. Closed without a byte, it read as a server that hung up, the
            // request failed, and the card a test meant to keep « in flight » went to `Failed`
            // before the assertion could look at it.
            if (reply.isEmpty())
                return false;
            // Said, when this answer is the last before hanging up. A server closing a
            // connection it did not announce closing leaves the client a socket it believes
            // reusable: the next request goes down it and meets the close. Qt sends a GET
            // again on its own; a POST it does not, so an import asked for just after the
            // shelf's three questions failed before reaching here — one run in five alone,
            // most runs in the whole suite.
            const qsizetype statusEnd = reply.indexOf("\r\n");
            if (last && reply.startsWith("HTTP/") && statusEnd > 0
                && !reply.toLower().contains("\r\nconnection:"))
                reply.insert(statusEnd + 2, "Connection: close\r\n");
            socket->write(reply);
            socket->flush();
            // Closed only once there is nothing left to answer: a client that pipelined two
            // requests down one connection is still waiting for the second.
            if (last)
                socket->disconnectFromHost();
            return true;
    }

    QList<std::pair<QPointer<QTcpSocket>, QByteArray>> m_held;
};
