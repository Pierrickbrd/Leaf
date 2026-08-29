// Where the configuration comes from, and what is refused.
//
// The application reads and never writes: where its server lives is a deployment question,
// the same way the server's own configuration is. So what there is to check is the order it
// looks in, and that a key anybody on the machine could read is not read at all.

#include "Settings.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>

#include <qt6keychain/keychain.h>

class KeepsTheKey : public QObject
{
    Q_OBJECT

private:
    static QString folder()
    {
        return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    }

    /// A configuration file, with the permissions asked for.
    static void writeConfiguration(const QString &address, const QString &key,
                                   QFileDevice::Permissions permissions)
    {
        QDir().mkpath(folder());
        const QString path = Settings::configurationFile();
        QFile::remove(path);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(QStringLiteral("address"), address);
            file.setValue(QStringLiteral("key"), key);
        }
        QVERIFY(QFile::setPermissions(path, permissions));
    }

    /// A Settings, waited for.
    static void waitFor(Settings &settings)
    {
        QSignalSpy loaded(&settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
    }

private slots:
    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(folder()).removeRecursively();
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");

        // The keyring outlives the process, so a run inherits whatever the run before it
        // left there.
        QKeychain::DeletePasswordJob forget(QStringLiteral("Leaf-under-test"));
        forget.setAutoDelete(false);
        forget.setKey(QStringLiteral("server-key"));
        QEventLoop waiting;
        connect(&forget, &QKeychain::Job::finished, &waiting, &QEventLoop::quit);
        forget.start();
        waiting.exec();
    }

    void the_environment_is_read_first()
    {
        // What a launcher or a systemd unit sets, for one run.
        writeConfiguration(QStringLiteral("https://on-disk:8081"), QStringLiteral("from-disk"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        qputenv("LEAF_ADDRESS", "https://from-env:8081");
        qputenv("LEAF_KEY", "from-env");

        Settings settings;
        waitFor(settings);

        QCOMPARE(settings.address(), QStringLiteral("https://from-env:8081"));
        QCOMPARE(settings.key(), QStringLiteral("from-env"));
        QCOMPARE(settings.storage(), Settings::Environment);
        QVERIFY(settings.configured());
        QVERIFY(settings.missing().isEmpty());
    }

    void a_file_only_you_can_read_is_read()
    {
        writeConfiguration(QStringLiteral("https://leaf.local:8081"),
                           QStringLiteral("8f3a92c1d4e5b6a7"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        Settings settings;
        waitFor(settings);

        QCOMPARE(settings.address(), QStringLiteral("https://leaf.local:8081"));
        QCOMPARE(settings.key(), QStringLiteral("8f3a92c1d4e5b6a7"));
        QVERIFY(settings.configured());
    }

    void a_file_anybody_can_read_is_not_read_at_all()
    {
        // The server will not start open on the network without a key. Same rule, other end:
        // a key the rest of the machine can read is a key that has already left.
        writeConfiguration(QStringLiteral("https://leaf.local:8081"),
                           QStringLiteral("8f3a92c1d4e5b6a7"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ReadGroup | QFileDevice::ReadOther);
        Settings settings;
        QSignalSpy said(&settings, &Settings::trouble);
        waitFor(settings);

        QVERIFY(settings.key().isEmpty());
        QVERIFY(!settings.configured());
        QCOMPARE(said.count(), 1);
        QVERIFY2(said.first().first().toString().contains(QStringLiteral("chmod 600")),
                 qPrintable(said.first().first().toString()));
    }

    void with_nothing_anywhere_it_says_what_is_missing_and_where_it_goes()
    {
        Settings settings;
        waitFor(settings);

        QVERIFY(!settings.configured());
        const QString said = settings.missing();
        // Saying what is absent is not the same as offering to manage it — but an empty
        // window that does not say why is a dead end.
        QVERIFY2(said.contains(QStringLiteral("leaf.conf")), qPrintable(said));
        QVERIFY2(said.contains(QStringLiteral("LEAF_ADDRESS")), qPrintable(said));
    }

    void nothing_is_said_before_the_keyring_has_answered()
    {
        // Two milliseconds of "no key" would send a configured person to a screen telling
        // them to configure something. `loaded` exists for that, and nothing else.
        Settings settings;
        QVERIFY(!settings.loaded());
        QVERIFY(settings.missing().isEmpty());
        waitFor(settings);
        QVERIFY(settings.loaded());
    }

    /// Quitting while the keyring is still answering.
    ///
    /// The single millisecond below is the whole test. Destroying at once survives —
    /// libsecret has not started, so there is nothing in flight to come back — and it was
    /// the first thing tried, which made a broken build pass. One millisecond in, the D-Bus
    /// call is out and the crash is certain. Measured, not chosen:
    ///
    ///     0 ms before destroying   survives
    ///     1 ms                     SIGSEGV
    void closing_while_the_keyring_is_still_answering_is_survivable()
    {
        for (int i = 0; i < 5; ++i) {
            auto *settings = new Settings;
            QTest::qWait(1);
            delete settings;
        }
        QTest::qWait(300);
        QVERIFY2(true, "still here");
    }

    void how_long_the_keyring_takes_to_answer()
    {
        QList<qint64> taken;
        for (int i = 0; i < 12; ++i) {
            QElapsedTimer clock;
            clock.start();
            Settings settings;
            waitFor(settings);
            taken.append(clock.elapsed());
        }
        std::sort(taken.begin(), taken.end());
        qInfo().noquote() << QStringLiteral("  keyring answered in %1 ms (median), %2 worst")
                                 .arg(taken.at(taken.size() / 2))
                                 .arg(taken.last());
        QVERIFY(!taken.isEmpty());
    }
};

QTEST_MAIN(KeepsTheKey)
#include "keeps_the_key.moc"
