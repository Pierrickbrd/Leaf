#pragma once

// A server of forty lines, for the tests that need one.
//
// Shared rather than copied: it started inside `talks_to_the_server.cpp`, and the shelf needs
// the same thing — something that answers a known body over a real socket, so what is being
// tested is the client's own behaviour and not a mock of the client's own behaviour.

#include <QByteArray>
#include <QTcpServer>
#include <QTcpSocket>

/// Answers with whatever it was told to, and remembers what it was asked.
///
/// `heard` accumulates across connections, so a test reading it between two requests clears
/// it first; `answer` is read at the moment of writing, so a test can change what comes back
/// between one request and the next.
class Pretend : public QTcpServer
{
    Q_OBJECT

public:
    QByteArray answer;
    QByteArray heard;

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
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] {
            heard += socket->readAll();
            if (!heard.contains("\r\n\r\n")) {
                return;
            }
            socket->write(answer);
            socket->flush();
            socket->disconnectFromHost();
        });
    }
};
