#pragma once

// What an export writes and where: the trees of several series merged into one, inside the
// folder the reader chose, as `Exports` takes it to disk.
//
// **The paths apart from the transfer.** `Exports` is what QML calls and what talks to the
// server; this is the plan it follows — which folder a series sits in, which file goes where,
// what is already there and what the library's disk did not have. As one class they were
// forty methods, and none of the plan needs a signal or the network to be right.

#include "Api.h"

#include <QList>
#include <QString>
#include <QStringList>

class ExportPlan final
{
public:
    /// One file to fetch: the `index`-th of series `seriesId`'s tree, to `path` in the export.
    struct Job {
        QString seriesId;
        int index = 0;
        QString path;
        qint64 size = 0;
    };

    explicit ExportPlan(const QString &destination = {});

    /// The union of `trees`, the one of `ids` at each place; why it cannot be written — a path
    /// out of the destination, or nothing to write — or nothing when it can.
    QString merge(const QList<Api::Tree> &trees, const QStringList &ids);
    /// The sidecars and files of the export the destination already has.
    QStringList present() const;
    /// « Compléter »: the files already there are left out, and not counted in what has to fit.
    void leaveWhatIsThere();
    /// What the files weigh together.
    qint64 size() const;
    /// Makes every folder and writes every sidecar, keeping those that are there when asked;
    /// why it could not, or nothing.
    QString makeFolders(bool keepWhatIsThere) const;

    QString destination() const { return m_destination; }
    const QList<Job> &jobs() const { return m_jobs; }
    /// The sidecars the library's disk did not have, which are not made up.
    QStringList missing() const { return m_missing; }
    /// Where `path` of the export lands.
    QString target(const QString &path) const;
    /// Something inside the destination that exists once written, for « Ouvrir le dossier ».
    QString where() const;

private:
    bool inside(const QString &path) const;

    QString m_destination;
    QList<Api::ExportedFolder> m_folders;
    QList<Job> m_jobs;
    QStringList m_missing;
};
