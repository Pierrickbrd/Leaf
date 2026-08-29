#include "Settings.h"

#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>

#include <qt6keychain/keychain.h>

#include <memory>

namespace {
const QString Entry = QStringLiteral("server-key");

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

QString Settings::configurationFile()
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

QString Settings::missing() const
{
    if (!m_loaded) {
        return {};
    }
    if (m_address.isEmpty() && m_key.isEmpty()) {
        return tr("Leaf does not know where your library is.\n"
                  "Put an address and a key in %1, or in LEAF_ADDRESS and LEAF_KEY.")
            .arg(configurationFile());
    }
    if (m_address.isEmpty()) {
        return tr("No address for the server. Set it in %1, or in LEAF_ADDRESS.")
            .arg(configurationFile());
    }
    if (m_key.isEmpty()) {
        return tr("No key for the server. Set it in %1, in LEAF_KEY, or in the keyring.")
            .arg(configurationFile());
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
    auto *job = owned.get();
    QPointer<Settings> alive(this);
    connect(job, &QKeychain::Job::finished, job, [this, job, alive] {
        if (!alive) {
            return;
        }
        if (job->error() == QKeychain::NoError && !job->textData().isEmpty()) {
            m_key = job->textData();
            m_storage = Storage::Keyring;
        }
        // The file answers for whatever is still missing — always the address, which never
        // goes in the keyring, and the key when nobody put one there.
        fromFile();
        m_loaded = true;
        emit changed();
    });
    owned.release()->start();
}

void Settings::fromFile()
{
    const QString path = configurationFile();
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
            emit trouble(tr("%1 can be read by more than you, so it was not read at all. "
                            "chmod 600 it.")
                             .arg(path));
            return;
        }
    }

    QSettings file(path, QSettings::IniFormat);
    if (m_address.isEmpty()) {
        m_address = file.value(QStringLiteral("address")).toString().trimmed();
    }
    if (m_key.isEmpty()) {
        m_key = file.value(QStringLiteral("key")).toString().trimmed();
        if (!m_key.isEmpty()) {
            m_storage = Storage::ProtectedFile;
        }
    }
}
