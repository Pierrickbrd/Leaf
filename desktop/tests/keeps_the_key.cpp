// Where the configuration comes from, where it goes, and what is refused.
//
// The client writes its settings once it has linked itself to a server: the key to the
// keyring, the address and the pinned fingerprint to `leaf.conf`. What there is to check is
// the order it reads in, that a key anybody on the machine could read is neither read nor
// written, that the key lands in the file only when the keyring refused it, and that the
// environment is never overwritten.

#include "Settings.h"
#include "Words.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTest>
#include <QTimeZone>
#include <QUrl>

#include <qt6keychain/keychain.h>

class KeepsTheKey : public QObject
{
    Q_OBJECT

private:
    static QString folder()
    {
        return QFileInfo(Settings::configurationFile()).absolutePath();
    }

    /// A configuration file, with the permissions asked for — where the installer writes it
    /// unless another place is named.
    static void writeConfiguration(const QString &address, const QString &key,
                                   QFileDevice::Permissions permissions,
                                   const QString &path = Settings::configurationFile())
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile::remove(path);
        {
            QSettings file(path, QSettings::IniFormat);
            file.setValue(QStringLiteral("address"), address);
            file.setValue(QStringLiteral("key"), key);
        }
        QVERIFY(QFile::setPermissions(path, permissions));
    }

    /// What the keyring holds for this client under test, or empty.
    static QString inTheKeyring()
    {
        QKeychain::ReadPasswordJob read(QStringLiteral("Leaf-under-test"));
        read.setAutoDelete(false);
        read.setKey(QStringLiteral("server-key"));
        QEventLoop waiting;
        connect(&read, &QKeychain::Job::finished, &waiting, &QEventLoop::quit);
        read.start();
        waiting.exec();
        return read.error() == QKeychain::NoError ? read.textData() : QString();
    }

    /// `leaf.conf`, as text, or empty when there is none.
    static QString onDisk()
    {
        QFile file(Settings::configurationFile());
        if (!file.open(QIODevice::ReadOnly)) {
            return {};
        }
        return QString::fromUtf8(file.readAll());
    }

    /// Whether `leaf.conf` is readable and writable by its owner, and by nobody else.
    static bool ownersOnly()
    {
        const auto permissions = QFileInfo(Settings::configurationFile()).permissions();
        const auto others = QFileDevice::ReadGroup | QFileDevice::WriteGroup
                            | QFileDevice::ExeGroup | QFileDevice::ReadOther
                            | QFileDevice::WriteOther | QFileDevice::ExeOther;
        return (permissions & others) == 0 && permissions.testFlag(QFileDevice::ReadOwner)
               && permissions.testFlag(QFileDevice::WriteOwner);
    }

    /// Puts `key` in the keyring under test directly, as an earlier run would have.
    static bool plantInTheKeyring(const QString &key)
    {
        QKeychain::WritePasswordJob write(QStringLiteral("Leaf-under-test"));
        write.setAutoDelete(false);
        write.setKey(QStringLiteral("server-key"));
        write.setTextData(key);
        QEventLoop waiting;
        connect(&write, &QKeychain::Job::finished, &waiting, &QEventLoop::quit);
        write.start();
        waiting.exec();
        return write.error() == QKeychain::NoError;
    }

    /// Makes the configuration folder read-only, so that nothing can be saved into it.
    static void freezeTheFolder(bool frozen)
    {
        QFile::setPermissions(folder(), frozen ? QFileDevice::ReadOwner | QFileDevice::ExeOwner
                                               : QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                     | QFileDevice::ExeOwner);
    }

    /// Asks `settings` to remember a link, and waits until it says it has.
    static void link(Settings &settings, const QString &key = QStringLiteral("a-device-key"))
    {
        QSignalSpy changed(&settings, &Settings::changed);
        settings.remember(QUrl(QStringLiteral("https://leaf.example:8081")), Print, key,
                          QStringLiteral("Bureau"), Linked);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);
    }

    /// Written in capitals on purpose: what is kept is lowercase, whatever was handed in,
    /// because `Pinning` compares the whole lowercase string.
    static inline const QString Print =
        QStringLiteral("AB12CD34EF56AB12CD34EF56AB12CD34EF56AB12CD34EF56AB12CD34EF56AB12");
    /// Whole seconds: `leaf.conf` keeps the date in ISO 8601 to the second.
    static inline const QDateTime Linked =
        QDateTime(QDate(2026, 10, 22), QTime(14, 30, 5), QTimeZone::utc());

    /// A Settings, waited for.
    static void waitFor(Settings &settings)
    {
        QSignalSpy loaded(&settings, &Settings::changed);
        QVERIFY(loaded.wait(5000));
    }

private slots:
    /// install.sh, the README and `preferences.ini` all say `~/.config/Leaf/leaf.conf`. Qt's
    /// own « application config » folder is `Leaf/Leaf` once organisation and application are
    /// both « Leaf », so a file the installer wrote there was never read: `install.sh all`
    /// ended on the linking screen, found by running it in a container.
    void the_file_lives_beside_the_preferences_where_the_installer_writes_it()
    {
        QStandardPaths::setTestModeEnabled(true);
        QCOMPARE(Settings::configurationFile(),
                 QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                     + QStringLiteral("/Leaf/leaf.conf"));
    }

    void init()
    {
        QStandardPaths::setTestModeEnabled(true);
        QDir(folder()).removeRecursively();
        QDir(QFileInfo(Settings::formerConfigurationFile()).absolutePath()).removeRecursively();
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

    void cleanup()
    {
        freezeTheFolder(false);
        Settings::refuseTheKeyringForTests(false);
        qunsetenv("LEAF_ADDRESS");
        qunsetenv("LEAF_KEY");
    }

    void remembering_a_link_writes_leaf_conf_readable_by_its_owner_only()
    {
        Settings settings;
        waitFor(settings);
        link(settings, QStringLiteral("kept-in-the-keyring"));

        QVERIFY(ownersOnly());
        const QString written = onDisk();
        QVERIFY2(written.contains(QStringLiteral("address=https://leaf.example:8081")),
                 qPrintable(written));
        QVERIFY2(written.contains(QStringLiteral("fingerprint=") + Print.toLower()),
                 qPrintable(written));
        QCOMPARE(settings.fingerprint(), Print.toLower());
        QCOMPARE(settings.key(), QStringLiteral("kept-in-the-keyring"));
        QVERIFY(settings.configured());

        // A desktop has a keyring and a CI runner does not: each machine checks the branch it
        // has, and between the two both are run.
        if (settings.storage() == Settings::Storage::Keyring) {
            QVERIFY2(!written.contains(QStringLiteral("key=")), qPrintable(written));
            QCOMPARE(inTheKeyring(), QStringLiteral("kept-in-the-keyring"));
        } else {
            QCOMPARE(settings.storage(), Settings::Storage::ProtectedFile);
            QVERIFY(written.contains(QStringLiteral("key=kept-in-the-keyring")));
        }
    }

    void when_the_keyring_refuses_the_key_goes_to_leaf_conf()
    {
        Settings settings;
        waitFor(settings);
        Settings::refuseTheKeyringForTests(true);
        link(settings, QStringLiteral("kept-in-the-file"));

        QCOMPARE(settings.storage(), Settings::Storage::ProtectedFile);
        QCOMPARE(settings.storageLabel(), Words::keyStorage(Words::KeyFrom::ProtectedFile));
        QVERIFY(ownersOnly());
        QVERIFY(onDisk().contains(QStringLiteral("key=kept-in-the-file")));
        QVERIFY(inTheKeyring().isEmpty());
    }

    void forgetting_leaves_no_key_anywhere()
    {
        // Twice: once from wherever this machine puts the key, once from the file.
        for (const bool refused : {false, true}) {
            Settings settings;
            waitFor(settings);
            Settings::refuseTheKeyringForTests(refused);
            link(settings, QStringLiteral("to-be-forgotten"));

            QSignalSpy changed(&settings, &Settings::changed);
            settings.forget();
            QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);

            const QString written = onDisk();
            QVERIFY2(!written.contains(QStringLiteral("to-be-forgotten")), qPrintable(written));
            QVERIFY2(!written.contains(QStringLiteral("key=")), qPrintable(written));
            QVERIFY2(!written.contains(QStringLiteral("address=")), qPrintable(written));
            QVERIFY2(!written.contains(QStringLiteral("fingerprint=")), qPrintable(written));
            QVERIFY(inTheKeyring().isEmpty());
            QVERIFY(settings.key().isEmpty());
            QVERIFY(settings.address().isEmpty());
            QVERIFY(settings.fingerprint().isEmpty());
            QVERIFY(settings.deviceName().isEmpty());
            QVERIFY(!settings.linkedAt().isValid());
            QVERIFY(!settings.configured());

            Settings::refuseTheKeyringForTests(false);
            Settings again;
            waitFor(again);
            QVERIFY(again.key().isEmpty());
            QVERIFY(again.address().isEmpty());
        }
    }

    void a_key_the_keyring_kept_from_before_never_beats_the_one_written_since()
    {
        // Linked once with the keyring open, then to another server with it locked: the
        // keyring still held the first server's key, and reading it first sent that key to
        // the second server.
        Settings settings;
        waitFor(settings);
        link(settings, QStringLiteral("first-server-key"));

        Settings::refuseTheKeyringForTests(true);
        QSignalSpy changed(&settings, &Settings::changed);
        settings.remember(QUrl(QStringLiteral("https://second.example:8081")), Print,
                          QStringLiteral("second-server-key"), QStringLiteral("Bureau"),
                          Linked);
        QTRY_VERIFY_WITH_TIMEOUT(changed.count() > 0, 5000);
        QCOMPARE(settings.storage(), Settings::Storage::ProtectedFile);
        // Asked after the erasure, and QtKeychain runs its jobs in order.
        QVERIFY(inTheKeyring().isEmpty());

        Settings::refuseTheKeyringForTests(false);
        Settings restarted;
        waitFor(restarted);
        QCOMPARE(restarted.address(), QStringLiteral("https://second.example:8081"));
        QCOMPARE(restarted.key(), QStringLiteral("second-server-key"));
    }

    void a_key_in_the_file_wins_over_one_left_in_the_keyring()
    {
        // Belt and braces for the case above, should the erasure itself have been refused.
        // On a machine with no keyring nothing is planted, and the file wins all the same.
        plantInTheKeyring(QStringLiteral("left-over"));
        writeConfiguration(QStringLiteral("https://leaf.example:8081"),
                           QStringLiteral("written-since"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        Settings settings;
        waitFor(settings);
        QCOMPARE(settings.key(), QStringLiteral("written-since"));
        QCOMPARE(settings.storage(), Settings::Storage::ProtectedFile);
    }

    void a_link_the_file_refuses_changes_nothing()
    {
        for (const bool refused : {false, true}) {
            QDir(folder()).removeRecursively();
            Settings settings;
            waitFor(settings);
            Settings::refuseTheKeyringForTests(refused);
            link(settings, QStringLiteral("the-old-key"));
            const auto storage = settings.storage();
            const QString before = onDisk();

            freezeTheFolder(true);
            QSignalSpy changed(&settings, &Settings::changed);
            QSignalSpy said(&settings, &Settings::trouble);
            settings.remember(QUrl(QStringLiteral("https://elsewhere:8081")), QString(),
                              QStringLiteral("the-new-key"), QStringLiteral("Salon"), Linked);
            QTRY_VERIFY_WITH_TIMEOUT(said.count() > 0, 5000);
            QTest::qWait(100);

            QCOMPARE(said.first().first().toString(),
                     Words::couldNotWrite(Settings::configurationFile()));
            QCOMPARE(changed.count(), 0);
            QCOMPARE(settings.address(), QStringLiteral("https://leaf.example:8081"));
            QCOMPARE(settings.key(), QStringLiteral("the-old-key"));
            QCOMPARE(settings.fingerprint(), Print.toLower());
            QCOMPARE(settings.storage(), storage);
            QCOMPARE(onDisk(), before);
            // The keyring holds what it held before: the old key, or nothing.
            if (storage == Settings::Storage::Keyring) {
                QCOMPARE(inTheKeyring(), QStringLiteral("the-old-key"));
            } else {
                QVERIFY(inTheKeyring().isEmpty());
            }

            freezeTheFolder(false);
            Settings::refuseTheKeyringForTests(false);
            Settings restarted;
            waitFor(restarted);
            QCOMPARE(restarted.address(), QStringLiteral("https://leaf.example:8081"));
            QCOMPARE(restarted.key(), QStringLiteral("the-old-key"));
        }
    }

    void an_unlinking_the_file_refuses_changes_nothing()
    {
        Settings settings;
        waitFor(settings);
        link(settings, QStringLiteral("still-linked"));
        // Owner-only and read-only, so the file is still read but cannot be rewritten.
        QVERIFY(QFile::setPermissions(Settings::configurationFile(), QFileDevice::ReadOwner));
        freezeTheFolder(true);

        QSignalSpy changed(&settings, &Settings::changed);
        QSignalSpy said(&settings, &Settings::trouble);
        settings.forget();
        QTest::qWait(100);

        QCOMPARE(said.count(), 1);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(settings.key(), QStringLiteral("still-linked"));
        QCOMPARE(settings.address(), QStringLiteral("https://leaf.example:8081"));
        QVERIFY(settings.configured());
    }

    void the_environment_still_wins_and_is_never_overwritten()
    {
        writeConfiguration(QStringLiteral("https://on-disk:8081"), QStringLiteral("from-disk"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        const QString before = onDisk();
        qputenv("LEAF_ADDRESS", "https://from-env:8081");
        qputenv("LEAF_KEY", "from-env");
        Settings settings;
        waitFor(settings);

        QSignalSpy changed(&settings, &Settings::changed);
        QSignalSpy said(&settings, &Settings::trouble);
        settings.remember(QUrl(QStringLiteral("https://elsewhere:8081")), Print,
                          QStringLiteral("never-kept"), QStringLiteral("Bureau"), Linked);
        settings.moveTo(QUrl(QStringLiteral("https://elsewhere:8081")), Print);
        settings.forget();
        QTest::qWait(200);

        QCOMPARE(said.count(), 3);
        for (const auto &one : said) {
            QCOMPARE(one.first().toString(), Words::settingsFromEnvironment());
        }
        QCOMPARE(changed.count(), 0);
        QCOMPARE(onDisk(), before);
        QVERIFY(inTheKeyring().isEmpty());
        QCOMPARE(settings.address(), QStringLiteral("https://from-env:8081"));
        QCOMPARE(settings.key(), QStringLiteral("from-env"));
        QVERIFY(settings.fingerprint().isEmpty());
    }

    void a_written_configuration_reads_back_the_same_after_a_restart()
    {
        {
            Settings settings;
            waitFor(settings);
            link(settings, QStringLiteral("survives-a-restart"));
        }
        {
            Settings restarted;
            waitFor(restarted);
            QCOMPARE(restarted.address(), QStringLiteral("https://leaf.example:8081"));
            QCOMPARE(restarted.key(), QStringLiteral("survives-a-restart"));
            QCOMPARE(restarted.fingerprint(), Print.toLower());
            QCOMPARE(restarted.deviceName(), QStringLiteral("Bureau"));
            QCOMPARE(restarted.linkedAt(), Linked);

            // Renamed and moved, then read again: the pin is dropped when the new address
            // has an authority's certificate, and the key stays where it was.
            QSignalSpy changed(&restarted, &Settings::changed);
            restarted.rename(QStringLiteral("Salon"));
            restarted.moveTo(QUrl(QStringLiteral("https://leaf.example.org")), QString());
            QCOMPARE(changed.count(), 2);
        }
        Settings again;
        waitFor(again);
        QCOMPARE(again.address(), QStringLiteral("https://leaf.example.org"));
        QCOMPARE(again.key(), QStringLiteral("survives-a-restart"));
        QVERIFY(again.fingerprint().isEmpty());
        QCOMPARE(again.deviceName(), QStringLiteral("Salon"));
        QCOMPARE(again.linkedAt(), Linked);
    }

    void partial_configuration_names_the_missing_value()
    {
        qputenv("LEAF_ADDRESS", "https://leaf.local");
        Settings settings;
        waitFor(settings);
        QCOMPARE(settings.storageLabel(), Words::keyStorage(Words::KeyFrom::Environment));
        QCOMPARE(settings.keyTitle(), Words::theKey());
        // Six for the principal, who manages everything. Counted rather than named, because
        // what this test is about is the key — but counted all the same, so that a section
        // added without a word to put on its pill fails here rather than on screen.
        QCOMPARE(settings.sections(QStringLiteral("principal")).size(), 6);
        for (const QVariant &one : settings.sections(QStringLiteral("principal")))
            QVERIFY(!one.toMap().value(QStringLiteral("label")).toString().isEmpty());
        QCOMPARE(settings.missing(), Words::noKey(Settings::configurationFile()));
        settings.setAddress({});
        settings.setKey(QStringLiteral("test-value"));
        QCOMPARE(settings.missing(), Words::noAddress(Settings::configurationFile()));
        settings.setAddress(QStringLiteral("https://leaf.local"));
        QVERIFY(settings.missing().isEmpty());
        QSignalSpy changed(&settings, &Settings::changed);
        settings.setAddress(settings.address());
        settings.setKey(settings.key());
        QCOMPARE(changed.count(), 0);
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
        QCOMPARE(settings.storage(), Settings::Storage::Environment);
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
        QCOMPARE(settings.storageLabel(), Words::keyStorage(Words::KeyFrom::ProtectedFile));
        QVERIFY(settings.configured());
    }

    /// A configuration left in the old place by a client that wrote there is still read,
    /// rather than lost on the update.
    void the_former_place_is_still_read_when_nothing_is_where_the_installer_writes()
    {
        QVERIFY(Settings::formerConfigurationFile() != Settings::configurationFile());
        writeConfiguration(QStringLiteral("https://former:8081"), QStringLiteral("former-key"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner,
                           Settings::formerConfigurationFile());
        Settings settings;
        waitFor(settings);

        QCOMPARE(settings.address(), QStringLiteral("https://former:8081"));
        QCOMPARE(settings.key(), QStringLiteral("former-key"));
    }

    void the_installers_file_wins_over_the_former_place()
    {
        writeConfiguration(QStringLiteral("https://former:8081"), QStringLiteral("former-key"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner,
                           Settings::formerConfigurationFile());
        writeConfiguration(QStringLiteral("https://installed:8081"),
                           QStringLiteral("installed-key"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner);
        Settings settings;
        waitFor(settings);

        QCOMPARE(settings.address(), QStringLiteral("https://installed:8081"));
        QCOMPARE(settings.key(), QStringLiteral("installed-key"));
    }

    /// The fallback is refused on the same terms: a key anybody can read has already left,
    /// whichever folder it sits in.
    void a_former_file_anybody_can_read_is_not_read_either()
    {
        writeConfiguration(QStringLiteral("https://former:8081"), QStringLiteral("former-key"),
                           QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ReadOther,
                           Settings::formerConfigurationFile());
        Settings settings;
        QSignalSpy said(&settings, &Settings::trouble);
        waitFor(settings);

        QVERIFY(settings.key().isEmpty());
        QCOMPARE(said.count(), 1);
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
        QCOMPARE(settings.storageLabel(), Words::keyStorage(Words::KeyFrom::Unknown));
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
