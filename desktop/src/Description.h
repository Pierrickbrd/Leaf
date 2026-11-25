#pragma once

// What is said about the series a page shows, past what names it and where it sits: who made
// it, what it weighs, its genres, its summary and the four columns of the description.
//
// **It reads the series `Series` holds, and keeps no copy of it.** The two were one class,
// and `cpp:S1448` counted forty-three methods on it: the getters a page object grows one by
// one had pushed the page past the ceiling, and these are the ones that only *word* the
// answer — none asks the server anything. A copy here would be a second answer to keep in
// step with the first, and the one that goes stale is always the one nobody looked at; the
// `friend` declaration in `Series.h` is the whole of what the two share.
//
// It is reached from QML as `Series.description`, and it says it changed whenever the page
// does, so a binding on it moves exactly when it moved before the split.

#include "Api.h"

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <optional>

class Series;

class Description final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Read through Series.description: it words the series the page holds.")

    /// The two lines of the header: who made it, and what it weighs.
    Q_PROPERTY(QString makers READ makers NOTIFY changed)
    /// The same line written for a rich text, « et N autres » a link to the people's window.
    Q_PROPERTY(QString makersLinked READ makersLinked NOTIFY changed)
    /// Everyone the volumes are credited with, for the window that lists them all:
    /// `[{section, name, count}]`, sections « Auteur », « Scénario » and « Dessin » in that
    /// order, each person once and in the page's order, `count` in the medium's word.
    Q_PROPERTY(QVariantList people READ people NOTIFY changed)
    Q_PROPERTY(QString peopleTitle READ peopleTitle NOTIFY changed)
    Q_PROPERTY(QString peopleCount READ peopleCount NOTIFY changed)
    Q_PROPERTY(QString weights READ weights NOTIFY changed)
    Q_PROPERTY(QStringList genres READ genres NOTIFY changed)
    Q_PROPERTY(QString summary READ summary NOTIFY changed)

    /// The four columns of the description, under the names the design gives them:
    /// `credits` and `nature` — who made it and who publishes it on the left, what it is on
    /// the right — then `holding`, what *this library* holds of the edition, and `received`,
    /// when it arrived. Each is a list of `{label, value}` rather than a getter apiece: a
    /// fifteenth fact then costs nothing, and a line whose value is missing is simply not in
    /// the list.
    ///
    /// One map and not four properties, the shape `Erasure::said` and `Shelf::narrowing`
    /// already take. Four getters of the same kind, read by one `.qml`, are a group whether
    /// or not it is written down.
    Q_PROPERTY(QVariantMap facts READ facts NOTIFY changed)

public:
    /// One person the series is credited with, as the page ranks them: what `work.json`
    /// declares first, then the most volumes, then whoever came first. The series listed them
    /// in the order its volumes were read, so the writer of one late volume could stand before
    /// the one of sixty.
    struct Person {
        QString name;
        int volumes = 0;
        bool declared = false;
        /// When the name was first met, reading the volumes in order.
        int firstRead = 0;
        bool wrote = false;
        bool drew = false;
    };

    /// `of` owns this object and outlives it by construction.
    explicit Description(Series *of);

    QString makers() const;
    QString makersLinked() const;
    QVariantList people() const;
    QString peopleTitle() const;
    QString peopleCount() const;
    QList<Person> peopleOrdered() const;
    QString weights() const;
    QStringList genres() const;
    QString summary() const;
    QVariantMap facts() const;

    /// The volumes of the edition, as the list has them: counted here rather than asked for
    /// again. Ignored for any series but the one pointed at.
    void takeVolumes(const QString &seriesId, const QList<Api::Entry> &volumes);

signals:
    void changed();

private:
    /// The answer the page holds, read where it is kept.
    const std::optional<Api::Series> &series() const;
    /// The names of `peopleOrdered`, or of one role of them.
    QStringList namesOf(bool wrote, bool drew) const;

    Series *m_of;
    QList<Api::Entry> m_volumes;
    QString m_volumesOf;
};
