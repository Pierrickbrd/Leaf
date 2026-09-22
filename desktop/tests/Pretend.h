#pragma once

// A server of forty lines, for the tests that need one.
//
// Shared rather than copied: it started inside `talks_to_the_server.cpp`, and the shelf needs
// the same thing — something that answers a known body over a real socket, so what is being
// tested is the client's own behaviour and not a mock of the client's own behaviour.

#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>

#include <functional>

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
                [this, socket, request = QByteArray{}, answered = false]() mutable {
            if (answered)
                return;
            const QByteArray arrived = socket->readAll();
            request += arrived;
            heard += arrived;
            const qsizetype headerEnd = request.indexOf("\r\n\r\n");
            if (headerEnd < 0) {
                return;
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
            if (request.size() < headerEnd + 4 + contentLength) {
                return;
            }
            answered = true;
            // A cover leaving GridView's one-row buffer cancels its request. The peer can
            // disappear after sending the headers and before this deliberately tiny server
            // gets to reply; that is success for the client, not a socket warning in a test.
            if (socket->state() != QAbstractSocket::ConnectedState)
                return;
            socket->write(answerFor ? answerFor(request) : answer);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};
