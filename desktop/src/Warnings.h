#pragma once

// The things that warn, each worded where the words live.
//
// `Toasts` decides how a bubble is laid, how long it stays and whether the desktop hears of
// it; this decides what each event *says* and which family the settings screen answers for
// it. They were one class, and `cpp:S1448` counted forty-one methods on it: the nine wordings
// had nothing to do with the queue, and the queue had nothing to do with French. A `.qml`
// connecting a signal to one of these is a `.qml` that carries no French, which is the whole
// arrangement of this client.

#include "Toasts.h"

#include <QByteArray>
#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>

class Warnings final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    /// `bubbles` is where every wording goes. Never null in the application; a test builds
    /// its own `Toasts` and hands it over.
    explicit Warnings(Toasts *bubbles, QObject *parent = nullptr);

    static Warnings *create(QQmlEngine *engine, QJSEngine *);

    Q_INVOKABLE void importSettled(bool went, const QString &subject, const QString &said);
    Q_INVOKABLE void scanFinished(const QString &counts);
    Q_INVOKABLE void scanFailed(const QString &why);
    Q_INVOKABLE void copySaved(const QString &path);
    Q_INVOKABLE void commandRefused(const QString &why);
    /// « Export terminé », the folder it went to and the sidecars the disk did not have. The
    /// bubble offers to open the folder; `path` is something inside it, as for a saved copy.
    Q_INVOKABLE void exported(const QString &name, const QString &path,
                              const QStringList &missing);
    Q_INVOKABLE void exportFailed(const QString &why);
    /// « ISBN copié », the number under it. Outside every family the settings screen answers
    /// for, and never sent to the desktop: it answers a click made in this window a moment
    /// ago, and a reader who silenced the reports of imports and scans still wants to know
    /// that their click did something — a copy shows nothing on the screen.
    Q_INVOKABLE void isbnCopied(const QString &isbn);
    /// Says, once, that Qt cannot read WebP — the format of every cover and every resized
    /// page — and which package fixes it. `readable` is what `QImageReader` lists, given
    /// rather than read so that a test can take the plugin away. Outside every family: it is
    /// not news about the library but the reason none of it can be seen.
    void readsImages(const QList<QByteArray> &readable);

private:
    /// `Toasts::say` and `Toasts::raise`, or nothing when there is no `Toasts` to say it to:
    /// a singleton that failed to resolve is reported once, in `create`, not at every event.
    void say(Preferences::Warns family, Toasts::Tone tone, const QString &headline,
             const QString &detail, Toasts::Offer offer = Toasts::Offer::Nothing,
             const QString &subject = {});
    void raise(Toasts::Tone tone, const QString &headline, const QString &detail);

    Toasts *m_bubbles;
    bool m_toldWebp = false;
};
