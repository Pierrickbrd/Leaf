#include "Doorbell.h"

#include <QLocalSocket>

#include <QStandardPaths>
#include <QTimer>

#include <sys/socket.h>
#include <unistd.h>

#include <memory>

namespace {

/// A link is a few hundred characters. Past this a peer is not sending one, and the process
/// that listens has no reason to keep reading.
constexpr qsizetype Longest = 4096;

/// Long enough for a window that is busy painting, short enough that a click on a dead
/// socket does not look like a click that did nothing.
constexpr int PatienceMs = 1500;

/// A peer that connects and says nothing, or never finishes its line, is not kept.
constexpr int WaitingMs = 2000;

const QByteArray Heard = "ok\n";

} // namespace

Doorbell::Doorbell(const QString &name, QObject *parent) : QObject(parent), m_name(name)
{
    // Only this user's processes may ring: the socket file is where a stranger on the
    // machine would otherwise push a link into somebody else's window.
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, &Doorbell::accept);
}

QString Doorbell::defaultName()
{
    QString folder = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if (folder.isEmpty()) {
        folder = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        return QStringLiteral("%1/leaf-desktop-%2.sock").arg(folder).arg(::getuid());
    }
    return folder + QStringLiteral("/leaf-desktop.sock");
}

bool Doorbell::peerIs(qintptr descriptor, unsigned user)
{
    if (descriptor < 0) {
        return false;
    }
    ucred who{};
    socklen_t size = sizeof(who);
    if (::getsockopt(int(descriptor), SOL_SOCKET, SO_PEERCRED, &who, &size) != 0) {
        return false;
    }
    return who.uid == user;
}

bool Doorbell::ring(const QString &link, const QString &name)
{
    QLocalSocket socket;
    socket.connectToServer(name);
    if (!socket.waitForConnected(PatienceMs)) {
        return false;
    }
    // Before a byte is written: whoever made this socket first would otherwise read the
    // link, one-time code included, and answer « ok » to it.
    if (!peerIs(socket.socketDescriptor(), ::getuid())) {
        return false;
    }
    socket.write(link.toUtf8() + '\n');
    if (!socket.waitForBytesWritten(PatienceMs)) {
        return false;
    }
    // Not leaving before it is acknowledged: a process that exits with the link still in
    // its own buffer has handed over nothing, and the click would have done nothing.
    QByteArray answer;
    while (answer.size() < Heard.size() && socket.waitForReadyRead(PatienceMs)) {
        answer += socket.readAll();
    }
    return answer == Heard;
}

bool Doorbell::listen()
{
    // Probed first, and not after a failed listen: Qt's own `listen` unlinks a socket file
    // that is already there and succeeds, live or not. Somebody answering is a Leaf that is
    // up — a launch with no link never rang, and taking its name would leave that window
    // unreachable. A name nobody answers is what a crash leaves.
    QLocalSocket probe;
    probe.connectToServer(m_name);
    if (probe.waitForConnected(PatienceMs)) {
        return false;
    }
    if (m_server.listen(m_name)) {
        return true;
    }
    QLocalServer::removeServer(m_name);
    return m_server.listen(m_name);
}

void Doorbell::accept()
{
    while (QLocalSocket *socket = m_server.nextPendingConnection()) {
        socket->setParent(this);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (!peerIs(socket->socketDescriptor(), ::getuid())) {
            socket->abort();
            continue;
        }
        QTimer::singleShot(WaitingMs, socket, [socket] { socket->abort(); });
        auto answered = std::make_shared<bool>(false);
        connect(socket, &QLocalSocket::readyRead, socket, [this, socket, answered] {
            if (*answered) {
                return;
            }
            // Everything up to the first newline is the link; a peer that never sends one,
            // or sends too much, is let go without being heard.
            if (socket->bytesAvailable() > Longest) {
                socket->disconnectFromServer();
                return;
            }
            if (!socket->canReadLine()) {
                return;
            }
            *answered = true;
            const QString link = QString::fromUtf8(socket->readLine()).trimmed();
            socket->write(Heard);
            socket->disconnectFromServer();
            emit rang(link);
        });
    }
}
