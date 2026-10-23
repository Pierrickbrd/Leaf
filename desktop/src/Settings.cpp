#include "Settings.h"

#include "Words.h"

#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>

#include <qt6keychain/keychain.h>

#include <memory>

namespace {
const QString Entry = QStringLiteral("server-key");

/// The names in `leaf.conf`. The first two were read before the client wrote anything; the
/// other three arrived with linking.
const QString AddressName = QStringLiteral("address");
const QString KeyName = QStringLiteral("key");
const QString FingerprintName = QStringLiteral("fingerprint");
const QString DeviceName = QStringLiteral("name");
const QString LinkedName = QStringLiteral("linked");

/// See `refuseTheKeyringForTests`.
bool keyringRefused = false;

/// Lowercase and trimmed, whether handed in or read back: `Pinning` compares the whole
/// string, and a pin typed in capitals is the same pin.
QString normalised(const QString &fingerprint)
{
    return fingerprint.trimmed().toLower();
}

/// Writes `value`, or removes the name when there is nothing to write — so a pin dropped
/// for an authority's certificate leaves no `fingerprint=` behind to be read as one.
void put(QSettings &file, const QString &name, const QString &value)
{
    if (value.isEmpty()) {
        file.remove(name);
    } else {
        file.setValue(name, value);
    }
}

/// What the key is filed under in the keyring.
///
/// A different name under test, because a test that reaches into the real keyring reads
/// whatever a previous run left there — which is how the first version of this passed while
/// reading a key none of its own code had put anywhere.
QString service()
{
    return QStandardPaths::isTestModeEnabled() ? QStringLiteral("Leaf-under-test")
                                               : QStringLiteral("Leaf");
}
} // namespace

Settings::Settings(QObject *parent) : QObject(parent)
{
    load();
}

/// Under the generic config folder, not `AppConfigLocation`: with organisation and application
/// both « Leaf » that is `Leaf/Leaf`, and install.sh wrote `Leaf/leaf.conf` — so `install.sh
/// all` left a client that never found its key.
QString Settings::configurationFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/Leaf/leaf.conf");
}

QString Settings::formerConfigurationFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
           + QStringLiteral("/leaf.conf");
}

void Settings::setAddress(const QString &address)
{
    if (m_address == address) {
        return;
    }
    m_address = address;
    emit changed();
}

void Settings::setKey(const QString &key)
{
    if (m_key == key) {
        return;
    }
    m_key = key;
    emit changed();
}

void Settings::setFingerprint(const QString &fingerprint)
{
    const QString pin = normalised(fingerprint);
    if (m_fingerprint == pin) {
        return;
    }
    m_fingerprint = pin;
    emit changed();
}

void Settings::refuseTheKeyringForTests(bool refuse)
{
    // Inert outside test mode, so that nothing a real run does can turn the keyring off.
    keyringRefused = refuse && QStandardPaths::isTestModeEnabled();
}

void Settings::putBackInTheKeyring(const QString &key)
{
    // Best effort, and nobody waits for it: if the keyring will not take this either, the
    // file is still the one place `load` believes first — see `fromFile`.
    std::unique_ptr<QKeychain::Job> owned;
    if (key.isEmpty()) {
        auto erase = std::make_unique<QKeychain::DeletePasswordJob>(service());
        erase->setKey(Entry);
        owned = std::move(erase);
    } else {
        auto restore = std::make_unique<QKeychain::WritePasswordJob>(service());
        restore->setKey(Entry);
        restore->setTextData(key);
        owned = std::move(restore);
    }
    owned->setAutoDelete(true);
    owned.release()->start();
}

bool Settings::refusedByEnvironment()
{
    if (m_storage != Storage::Environment) {
        return false;
    }
    emit trouble(Words::settingsFromEnvironment());
    return true;
}

bool Settings::write(const std::function<void(QSettings &)> &edit)
{
    const QString path = configurationFile();
    const auto ownerOnly = QFileDevice::ReadOwner | QFileDevice::WriteOwner;
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Owner-only *before* anything goes in, not after: a key written into a file and then
    // chmodded has been readable by the rest of the machine for as long as that took.
    // `QSettings` saves through a temporary file renamed over this one, and keeps the
    // permissions it finds here.
    if (QFile::exists(path)) {
        if (!QFile::setPermissions(path, ownerOnly)) {
            emit trouble(Words::couldNotWrite(path));
            return false;
        }
    } else {
        QFile created(path);
        if (!created.open(QIODevice::WriteOnly, ownerOnly)) {
            emit trouble(Words::couldNotWrite(path));
            return false;
        }
    }

    QSettings file(path, QSettings::IniFormat);
    QVariantMap before;
    for (const QString &name : file.allKeys()) {
        before.insert(name, file.value(name));
    }
    edit(file);
    file.sync();
    if (file.status() != QSettings::NoError || !QFile::setPermissions(path, ownerOnly)) {
        // Undone in memory as well. Every `QSettings` on one file in one process shares a
        // cache, and a save that failed stays in it: the next one opened here read back the
        // address that never reached the disk, and the next save that worked would have
        // written it there after all.
        file.clear();
        for (auto one = before.cbegin(); one != before.cend(); ++one) {
            file.setValue(one.key(), one.value());
        }
        emit trouble(Words::couldNotWrite(path));
        return false;
    }
    return true;
}

void Settings::remember(const QUrl &address, const QString &fingerprint, const QString &key,
                        const QString &deviceName, const QDateTime &linkedAt)
{
    if (refusedByEnvironment()) {
        return;
    }
    const quint64 generation = ++m_generation;

    if (keyringRefused && QStandardPaths::isTestModeEnabled()) {
        // Queued, so that the test sees the same order as a real refusal: after this returns.
        QMetaObject::invokeMethod(
            this,
            [=, this] {
                if (generation == m_generation) {
                    keyStored(false, address, fingerprint, key, deviceName, linkedAt);
                }
            },
            Qt::QueuedConnection);
        return;
    }

    // Owned the same way as the read in `load`, for the same reason: QtKeychain keeps its
    // own list of queued jobs, and a job both auto-deleting and parented is freed twice.
    auto owned = std::make_unique<QKeychain::WritePasswordJob>(service());
    owned->setAutoDelete(true);
    owned->setKey(Entry);
    owned->setTextData(key);
    const auto *job = owned.get();
    QPointer<Settings> alive(this);
    connect(job, &QKeychain::Job::finished, job, [=, this] {
        // Overtaken by a `forget` or a later `remember`: whatever this one meant to write is
        // no longer what the person asked for.
        if (!alive || generation != m_generation) {
            return;
        }
        keyStored(job->error() == QKeychain::NoError, address, fingerprint, key, deviceName,
                  linkedAt);
    });
    owned.release()->start();
}

void Settings::keyStored(bool accepted, const QUrl &address, const QString &fingerprint,
                         const QString &key, const QString &deviceName,
                         const QDateTime &linkedAt)
{
    const QString pin = normalised(fingerprint);
    // What the keyring held before this link, if the key lived there: put back should the
    // file refuse the address that goes with the new one.
    const QString before = m_storage == Storage::Keyring ? m_key : QString();
    const bool written = write([&](QSettings &file) {
        file.setValue(AddressName, address.toString());
        put(file, FingerprintName, pin);
        put(file, DeviceName, deviceName);
        put(file, LinkedName, linkedAt.isValid() ? linkedAt.toUTC().toString(Qt::ISODate)
                                                 : QString());
        // In the file only when the keyring would not take it — and taken out of the file
        // when it did, so a key from an earlier fallback is not left lying beside it.
        put(file, KeyName, accepted ? QString() : key);
    });

    if (!written) {
        // Nothing adopted, nothing announced — `trouble` has said why. A new key left in the
        // keyring beside the old address would be sent, at the next start, to the old
        // server; so the keyring goes back to what it held.
        if (accepted) {
            putBackInTheKeyring(before);
        }
        return;
    }
    if (!accepted) {
        // The key is in the file now. Whatever an earlier link left in the keyring is the old
        // key, and must not be read back in its place.
        putBackInTheKeyring({});
    }

    m_address = address.toString();
    m_fingerprint = pin;
    m_key = key;
    m_deviceName = deviceName;
    m_linkedAt = linkedAt;
    m_storage = accepted ? Storage::Keyring : Storage::ProtectedFile;
    emit changed();
}

void Settings::rename(const QString &deviceName)
{
    if (!write([&](QSettings &file) { put(file, DeviceName, deviceName); })) {
        return;
    }
    m_deviceName = deviceName;
    emit changed();
}

void Settings::moveTo(const QUrl &address, const QString &fingerprint)
{
    if (refusedByEnvironment()) {
        return;
    }
    const QString pin = normalised(fingerprint);
    if (!write([&](QSettings &file) {
            file.setValue(AddressName, address.toString());
            put(file, FingerprintName, pin);
        })) {
        return;
    }
    m_address = address.toString();
    // Changed here and read by `Pinning::renew` before the next request, which drops the
    // connections kept open to the certificate this replaces.
    m_fingerprint = pin;
    emit changed();
}

void Settings::forget()
{
    if (refusedByEnvironment()) {
        return;
    }
    // The file first, and nothing else changes if it refuses: a screen saying « délié »
    // over a leaf.conf that still holds the key would be lying. `trouble` says why.
    if (QFile::exists(configurationFile())
        && !write([](QSettings &file) {
               for (const QString &name :
                    {AddressName, KeyName, FingerprintName, DeviceName, LinkedName}) {
                   file.remove(name);
               }
           })) {
        return;
    }
    const quint64 generation = ++m_generation;
    const bool wasInTheKeyring = m_storage == Storage::Keyring;

    // The memory at once; the keyring is a service and answers later.
    m_address.clear();
    m_key.clear();
    m_fingerprint.clear();
    m_deviceName.clear();
    m_linkedAt = {};
    m_storage = Storage::Unknown;

    // Asked even when the key came from the file: a keyring entry left by an earlier run,
    // or put there by hand, is a key just the same.
    auto owned = std::make_unique<QKeychain::DeletePasswordJob>(service());
    owned->setAutoDelete(true);
    owned->setKey(Entry);
    const auto *job = owned.get();
    QPointer<Settings> alive(this);
    connect(job, &QKeychain::Job::finished, job, [=, this] {
        if (!alive || generation != m_generation) {
            return;
        }
        const auto error = job->error();
        if (wasInTheKeyring && error != QKeychain::NoError
            && error != QKeychain::EntryNotFound) {
            emit trouble(Words::keyringKeptTheKey());
        }
        emit changed();
    });
    owned.release()->start();
}

QString Settings::keyTitle() const
{
    return Words::theKey();
}

QVariantList Settings::sections(const QString &kind) const
{
    const auto tab = [](const QString &name, const QString &label) {
        return QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("label"), label}};
    };
    // A kind this client was not taught reads as restricted, like everywhere else: the
    // narrowest reading is the safe one for a profile that is not understood. The words are
    // compared here rather than through `Api::kind`, which would drag the whole contract into
    // every program that only wants to know where the key is.
    const bool principal = kind == QStringLiteral("principal");
    const bool reader = principal || kind == QStringLiteral("reader");
    QVariantList out{tab(QStringLiteral("general"), Words::generalSettings())};
    if (reader)
        out << tab(QStringLiteral("profiles"), Words::profilesTab());
    if (principal)
        out << tab(QStringLiteral("devices"), Words::devicesTab())
            << tab(QStringLiteral("library"), Words::librarySettings());
    out << tab(QStringLiteral("notifications"), Words::notificationSettings())
        << tab(QStringLiteral("about"), Words::aboutTab());
    return out;
}

QString Settings::storageLabel() const
{
    switch (m_storage) {
    case Storage::Environment:
        return Words::keyStorage(Words::KeyFrom::Environment);
    case Storage::Keyring:
        return Words::keyStorage(Words::KeyFrom::Keyring);
    case Storage::ProtectedFile:
        return Words::keyStorage(Words::KeyFrom::ProtectedFile);
    case Storage::Unknown:
        break;
    }
    return Words::keyStorage(Words::KeyFrom::Unknown);
}

QString Settings::missing() const
{
    if (!m_loaded) {
        return {};
    }
    if (m_address.isEmpty() && m_key.isEmpty()) {
        return Words::nothingConfigured(configurationFile());
    }
    if (m_address.isEmpty()) {
        return Words::noAddress(configurationFile());
    }
    if (m_key.isEmpty()) {
        return Words::noKey(configurationFile());
    }
    return {};
}

bool Settings::fromEnvironment()
{
    const QString address = qEnvironmentVariable("LEAF_ADDRESS");
    const QString key = qEnvironmentVariable("LEAF_KEY");
    if (address.isEmpty() && key.isEmpty()) {
        return false;
    }
    m_address = address;
    m_key = key;
    m_storage = Storage::Environment;
    return true;
}

void Settings::load()
{
    // The environment first, and then nothing else: what a launcher sets for one run should
    // not be half-overridden by what is on the disk.
    if (fromEnvironment()) {
        m_loaded = true;
        // Queued, so that whoever connects to it right after constructing this still hears.
        QMetaObject::invokeMethod(this, &Settings::changed, Qt::QueuedConnection);
        return;
    }

    // Not parented to `this`, and watched with a guard. A job that is both auto-deleting and
    // owned by something else is deleted twice over: QtKeychain keeps queued jobs in a list
    // of its own, so tearing this down while a read is in flight left that list holding a
    // pointer to freed memory — a crash measured at one millisecond into the read.
    //
    // Owned here only until it starts, and by QtKeychain from then on. `release` sits on the
    // exact line where that changes hands: before it, leaving this function early takes the
    // job with it; after it, `setAutoDelete` is what frees it.
    auto owned = std::make_unique<QKeychain::ReadPasswordJob>(service());
    owned->setAutoDelete(true);
    owned->setKey(Entry);
    // A view, not a handle: everything that changes the job goes through `owned`, and this
    // is only ever read from — by `connect`, and by the callback asking how it went.
    const auto *job = owned.get();
    QPointer<Settings> alive(this);
    const quint64 generation = m_generation;
    connect(job, &QKeychain::Job::finished, job, [this, job, alive, generation] {
        if (!alive) {
            return;
        }
        // Linked or unlinked before the keyring had even answered: what is held now is newer
        // than anything this read could bring back.
        if (generation != m_generation) {
            m_loaded = true;
            emit changed();
            return;
        }
        if (job->error() == QKeychain::NoError && !job->textData().isEmpty()) {
            m_key = job->textData();
            m_storage = Storage::Keyring;
        }
        // The file answers for the address, which never goes in the keyring — and for the key
        // too when it holds one, over whatever the keyring said: see `fromFile`.
        fromFile();
        m_loaded = true;
        emit changed();
    });
    owned.release()->start();
}

void Settings::fromFile()
{
    // The installer's place first; the former one only when nothing is there, so a file the
    // update left behind is not lost, and never mixed with the one that replaced it.
    QString path = configurationFile();
    if (!QFile::exists(path)) {
        path = formerConfigurationFile();
    }
    if (!QFile::exists(path)) {
        return;
    }

    // Refused rather than read. The server will not start open on the network without a key;
    // this is the same rule from the other end, and a key that anybody on the machine can
    // read is a key that has already left.
    const auto permissions = QFileInfo(path).permissions();
    for (auto reachable : {QFileDevice::ReadGroup, QFileDevice::WriteGroup,
                           QFileDevice::ReadOther, QFileDevice::WriteOther}) {
        if (permissions.testFlag(reachable)) {
            emit trouble(Words::readableByOthers(path));
            return;
        }
    }

    QSettings file(path, QSettings::IniFormat);
    if (m_address.isEmpty()) {
        m_address = file.value(AddressName).toString().trimmed();
    }
    // A key in the file wins over one in the keyring. The file only gets one when the
    // keyring refused it, so the keyring's can only be older: linked once with the keyring
    // open, then again with it locked, the keyring still held the first server's key, and
    // reading it first sent that key to the second server.
    const QString filed = file.value(KeyName).toString().trimmed();
    if (!filed.isEmpty()) {
        m_key = filed;
        m_storage = Storage::ProtectedFile;
    }
    m_fingerprint = normalised(file.value(FingerprintName).toString());
    m_deviceName = file.value(DeviceName).toString().trimmed();
    m_linkedAt = QDateTime::fromString(file.value(LinkedName).toString(), Qt::ISODate);
}

