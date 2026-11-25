#include "ExportPlan.h"

#include "Words.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>
#include <QSet>

using namespace Qt::StringLiterals;

namespace {

/// The file each level carries, for the sentence that names the one the disk did not have.
QString sidecarOf(const QString &level)
{
    if (level == u"UNIVERSE"_s)
        return u"universe.json"_s;
    if (level == u"WORK"_s)
        return u"work.json"_s;
    return u"edition.json"_s;
}

} // namespace

ExportPlan::ExportPlan(const QString &destination)
    : m_destination(destination)
{
}

bool ExportPlan::inside(const QString &path) const
{
    // The server names where a file goes, and a name with `..` in it would go out of the
    // folder the reader chose. Refused whoever sent it.
    const QString whole = QDir::cleanPath(m_destination + u'/' + path);
    return !path.isEmpty() && whole.startsWith(m_destination + u'/');
}

QString ExportPlan::target(const QString &path) const
{
    return QDir::cleanPath(m_destination + u'/' + path);
}

QString ExportPlan::merge(const QList<Api::Tree> &trees, const QStringList &ids)
{
    // The union, each path once. The folders keep the order of the first series to name them,
    // outermost first; a folder two series name is one folder, and the copy that carries its
    // sidecar wins over the one that does not.
    QHash<QString, int> seenFolder;
    QSet<QString> seenFile;
    for (int at = 0; at < trees.size(); ++at) {
        const Api::Tree &tree = trees.at(at);
        for (const Api::ExportedFolder &one : tree.folders) {
            const auto known = seenFolder.constFind(one.path);
            if (known == seenFolder.cend()) {
                seenFolder.insert(one.path, int(m_folders.size()));
                m_folders.append(one);
            } else if (!m_folders.at(*known).content && one.content) {
                m_folders[*known] = one;
            }
        }
        for (int index = 0; index < tree.files.size(); ++index) {
            const Api::ExportedFile &one = tree.files.at(index);
            if (seenFile.contains(one.path))
                continue;
            seenFile.insert(one.path);
            m_jobs.append({ids.at(at), index, one.path, one.size});
        }
    }

    for (const Api::ExportedFolder &one : std::as_const(m_folders)) {
        if (!inside(one.path))
            return Words::exportUnsafePath(one.path);
        if (!one.content)
            m_missing << one.path + u'/' + sidecarOf(one.level);
    }
    for (const Job &one : std::as_const(m_jobs)) {
        if (!inside(one.path))
            return Words::exportUnsafePath(one.path);
    }
    if (m_jobs.isEmpty())
        return Words::nothingToExport();
    return {};
}

QStringList ExportPlan::present() const
{
    QStringList there;
    for (const Api::ExportedFolder &one : m_folders) {
        const QString sidecar = one.path + u'/' + sidecarOf(one.level);
        if (one.content && QFileInfo::exists(target(sidecar)))
            there << sidecar;
    }
    for (const Job &one : m_jobs) {
        if (QFileInfo::exists(target(one.path)))
            there << one.path;
    }
    return there;
}

void ExportPlan::leaveWhatIsThere()
{
    m_jobs.removeIf([this](const Job &one) { return QFileInfo::exists(target(one.path)); });
}

qint64 ExportPlan::size() const
{
    qint64 total = 0;
    for (const Job &one : m_jobs)
        total += one.size;
    return total;
}

QString ExportPlan::makeFolders(bool keepWhatIsThere) const
{
    for (const Api::ExportedFolder &one : m_folders) {
        if (!QDir().mkpath(target(one.path)))
            return Words::couldNotMakeFolder(target(one.path));
        if (!one.content)
            continue;
        const QString path = target(one.path + u'/' + sidecarOf(one.level));
        if (keepWhatIsThere && QFileInfo::exists(path))
            continue;
        // As received: the text is the disk's own, accents and line ends included, and a
        // sidecar tidied on the way out would no longer be the one that was read.
        QSaveFile out(path);
        const QByteArray bytes = one.content->toUtf8();
        if (!out.open(QIODevice::WriteOnly) || out.write(bytes) != bytes.size()
            || !out.commit()) {
            return Words::couldNotWrite(path);
        }
    }
    return {};
}

QString ExportPlan::where() const
{
    const QString first = !m_folders.isEmpty() ? m_folders.first().path : m_jobs.first().path;
    return target(first.section(u'/', 0, 0));
}
