// A second launch handing its link to the first: over a real local socket, because what has
// to be proved is that the process which was clicked can leave knowing the link arrived.
//
// Every case names its own socket. The default one is this user's, and a Leaf open on the
// machine running the tests would be handed the test's links.

#include "Doorbell.h"

#include <QFile>
#include <QLocalServer>
#include <QLocalSocket>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include <atomic>
#include <thread>

#include <cerrno>
#include <cstring>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

class AnswersTheDoorbell : public QObject
{
    Q_OBJECT

private:
    /// An absolute path in a folder of its own, as the real one is: a bare name would land
    /// in the shared temporary folder, which is the thing the real one must not do.
    QString nameFor()
    {
        if (!m_folder.isValid()) {
            return {};
        }
        return m_folder.filePath(QUuid::createUuid().toString(QUuid::Id128).left(8));
    }

    QTemporaryDir m_folder;

private slots:
    void a_second_launch_hands_its_link_to_the_first_and_leaves()
    {
        const QString name = nameFor();
        Doorbell first(name);
        QVERIFY(first.listen());
        QSignalSpy rang(&first, &Doorbell::rang);

        const QString link = u"leaf://example.test:8443/?code=ABCD-EFGH"_s;
        // `ring` blocks until the answer, and the first one answers from this very event
        // loop: it is rung from another thread, as it is from another process, so that the
        // loop is free while the call waits.
        std::atomic<bool> answered = false;
        std::thread second([&] { answered = Doorbell::ring(link, name); });
        QTRY_VERIFY(rang.size() == 1);
        QTRY_VERIFY(answered);
        second.join();
        QCOMPARE(rang.first().first().toString(), link);
    }

    void with_nobody_listening_ringing_fails_and_the_launch_goes_on()
    {
        QVERIFY(!Doorbell::ring(u"leaf://example.test/"_s, nameFor()));
    }

    void a_stale_socket_left_by_a_crash_is_taken_over()
    {
        const QString name = nameFor();

        // What a crash leaves: a socket file and nobody behind it. QLocalServer removes its
        // file when it closes, so the file is made by hand and abandoned.
        const QString path = name;
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        QVERIFY(fd >= 0);
        sockaddr_un address{};
        address.sun_family = AF_UNIX;
        const QByteArray bytes = QFile::encodeName(path);
        QVERIFY(size_t(bytes.size()) < sizeof(address.sun_path));
        std::memcpy(address.sun_path, bytes.constData(), size_t(bytes.size()));
        QCOMPARE(::bind(fd, reinterpret_cast<sockaddr *>(&address), sizeof(address)), 0);
        ::close(fd);
        QVERIFY(QFile::exists(path));

        // Nobody answers behind it, so the launch is the first...
        QVERIFY(!Doorbell::ring(u"leaf://example.test/"_s, name));
        // ...and listens over what the crash left.
        Doorbell first(name);
        QVERIFY(first.listen());
        QSignalSpy rang(&first, &Doorbell::rang);
        std::atomic<bool> answered = false;
        std::thread second([&] { answered = Doorbell::ring(u"leaf://x.test/"_s, name); });
        QTRY_VERIFY(answered);
        second.join();
        QCOMPARE(rang.size(), 1);
    }

    void a_second_listener_on_a_live_name_fails_and_the_first_stays_reachable()
    {
        const QString name = nameFor();
        Doorbell first(name);
        QVERIFY(first.listen());
        QSignalSpy rang(&first, &Doorbell::rang);

        // A plain launch listens without having rung: it must not unlink a live socket.
        Doorbell second(name);
        QVERIFY(!second.listen());

        std::atomic<bool> answered = false;
        std::thread third([&] { answered = Doorbell::ring(u"leaf://x.test/"_s, name); });
        QTRY_VERIFY(answered);
        third.join();
        QCOMPARE(rang.size(), 1);
    }

    void the_socket_file_is_for_its_owner_alone()
    {
        const QString name = nameFor();
        Doorbell first(name);
        QVERIFY(first.listen());
        struct stat info{};
        QCOMPARE(::stat(QFile::encodeName(name).constData(), &info), 0);
        QCOMPARE(info.st_mode & (S_IRWXG | S_IRWXO), mode_t(0));
    }

    void the_default_socket_lives_in_the_runtime_folder_and_not_in_tmp()
    {
        QTemporaryDir runtime;
        QVERIFY(runtime.isValid());
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
        const QString path = Doorbell::defaultName();
        qunsetenv("XDG_RUNTIME_DIR");
        QVERIFY2(path.startsWith(runtime.path() + u'/'), qPrintable(path));
    }

    void a_peer_of_another_user_is_not_taken_for_this_one()
    {
        // A second user cannot be made in a test, so the check is given the uid it expects:
        // the same socket is this user's and is not the next one's.
        int pair[2];
        QCOMPARE(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
        QVERIFY(Doorbell::peerIs(pair[0], ::getuid()));
        QVERIFY(!Doorbell::peerIs(pair[0], ::getuid() + 1));
        QVERIFY(!Doorbell::peerIs(-1, ::getuid()));
        ::close(pair[0]);
        ::close(pair[1]);
    }

    void a_peer_that_sends_no_newline_is_dropped()
    {
        const QString name = nameFor();
        Doorbell first(name);
        QVERIFY(first.listen());
        QSignalSpy rang(&first, &Doorbell::rang);
        QLocalSocket silent;
        silent.connectToServer(name);
        QVERIFY(silent.waitForConnected(1000));
        silent.write("leaf://never-finished");
        silent.flush();
        QSignalSpy gone(&silent, &QLocalSocket::disconnected);
        QVERIFY(gone.wait(6000));
        QCOMPARE(rang.size(), 0);
    }

    void a_connection_is_answered_once_whatever_it_sends()
    {
        const QString name = nameFor();
        Doorbell first(name);
        QVERIFY(first.listen());
        QSignalSpy rang(&first, &Doorbell::rang);
        QLocalSocket eager;
        eager.connectToServer(name);
        QVERIFY(eager.waitForConnected(1000));
        eager.write("leaf://a.test/\nleaf://b.test/\n");
        eager.flush();
        QTRY_COMPARE(rang.size(), 1);
        QTest::qWait(200);
        QCOMPARE(rang.size(), 1);
        QCOMPARE(rang.first().first().toString(), u"leaf://a.test/"_s);
    }
};

QTEST_GUILESS_MAIN(AnswersTheDoorbell)
#include "answers_the_doorbell.moc"
