#pragma once

// Taking a piece of the library out as the folders Leaf reads back.
//
// A series, a volume, a whole work, a universe or a selection becomes an ordinary tree on the
// reader's disk: the folders it sits in with their sidecars, the archives, the covers beside
// them. Never an archive of archives — the point is a folder that a scan, an import or a hand
// can open.
//
// **The server describes, the client writes.** `/series/{id}/folders` lists what a series keeps
// and `/series/{id}/files/{index}` serves each of it; this merges the lists of several series
// (two editions of one universe share its folder), asks before touching a path that is already
// there, and streams every file into a `QSaveFile`. A file is whole or it is not there: a
// stopped or failed export leaves no half-written archive, which opens, is the wrong length,
// and says nothing about it.
//
// Which file goes where, and what is already there, is `ExportPlan`'s; this is the transfer.

#include "Api.h"
#include "ExportPlan.h"
#include "Server.h"

#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QSaveFile>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <memory>

class Exports : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// Reading the manifests or writing files: the bar is drawn.
    Q_PROPERTY(bool going READ going NOTIFY changed)
    /// Waiting for « Remplacer », « Compléter » or « Annuler ».
    Q_PROPERTY(bool asking READ asking NOTIFY changed)
    /// Either of the two: another export is not begun over one.
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString label READ label NOTIFY changed)
    Q_PROPERTY(qint64 sentBytes READ sentBytes NOTIFY changed)
    Q_PROPERTY(qint64 totalBytes READ totalBytes NOTIFY changed)
    Q_PROPERTY(double fraction READ fraction NOTIFY changed)
    Q_PROPERTY(QString percent READ percent NOTIFY changed)
    /// What stands in the way, worded for the question.
    Q_PROPERTY(QString question READ question NOTIFY changed)
    Q_PROPERTY(QVariantMap words READ words CONSTANT)

public:
    /// `freeSpace` says how many bytes a folder's disk has left; given so that a test can say.
    explicit Exports(Server *server,
                     std::function<qint64(const QString &)> freeSpace = {},
                     QObject *parent = nullptr);

    static Exports *create(QQmlEngine *engine, QJSEngine *);

    bool going() const { return m_phase == Phase::Reading || m_phase == Phase::Writing; }
    bool asking() const { return m_phase == Phase::Asking; }
    bool busy() const { return m_phase != Phase::Idle; }
    QString name() const { return m_name; }
    QString label() const;
    qint64 sentBytes() const { return m_sent; }
    qint64 totalBytes() const { return m_total; }
    double fraction() const;
    QString percent() const;
    QString question() const { return m_question; }
    QVariantMap words() const;

    /// Several series — one is a selection of one — with the folders they sit in.
    Q_INVOKABLE void exportSeries(const QStringList &seriesIds, const QUrl &into,
                                  const QString &name);
    /// Every edition of the work this series is one of.
    Q_INVOKABLE void exportWork(const QString &seriesId, const QUrl &into);
    /// Every series of the universe this one belongs to.
    Q_INVOKABLE void exportUniverse(const QString &seriesId, const QUrl &into);

    /// The answers to the question asked when a path was already there.
    Q_INVOKABLE void replace();
    Q_INVOKABLE void complete();
    /// Stops whatever is on its way, or the question, and leaves no partial file.
    Q_INVOKABLE void cancel();

signals:
    void changed();
    /// Something of the export is already in the folder: how many, and one of them named.
    void conflict(int count, const QString &example);
    /// Done. `where` is something inside the destination that exists, for « Ouvrir le dossier »;
    /// `missing` are the sidecars the library's disk did not have, which are not made up.
    void finished(const QString &where, const QStringList &missing);
    void failed(const QString &why);

private:
    enum class Phase { Idle, Reading, Asking, Writing };

    void begin(const QUrl &into, const QString &name);
    /// Every series' tree, then the plan merged from them, then the question or the writing.
    void ask(const QStringList &seriesIds);
    void resolve(const QString &seriesId, bool wholeUniverse);
    void write(bool keepWhatIsThere);
    /// The next file, or the end once there is none.
    void next();
    void fail(const QString &why);

    Server *m_server;
    std::function<qint64(const QString &)> m_freeSpace;

    Phase m_phase = Phase::Idle;
    /// Moves on at every start, stop and failure, so that an answer which arrives late is
    /// recognised as belonging to something that is over.
    int m_generation = 0;
    QString m_name;
    QString m_question;
    qint64 m_sent = 0;
    qint64 m_total = 0;

    QList<Api::Tree> m_trees;
    QStringList m_ids;
    int m_waiting = 0;

    ExportPlan m_plan;
    int m_at = 0;
    std::unique_ptr<QSaveFile> m_out;
    bool m_writeFailed = false;
};
