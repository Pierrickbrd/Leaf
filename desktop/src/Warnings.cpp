#include "Warnings.h"

#include "Words.h"

#include <QDebug>
#include <QFileInfo>

Warnings *Warnings::create(QQmlEngine *engine, QJSEngine *)
{
    auto *bubbles = engine->singletonInstance<Toasts *>(qmlTypeId("Leaf", 1, 0, "Toasts"));
    if (!bubbles) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Toasts singleton — nothing will warn");
    }
    return new Warnings(bubbles);
}

Warnings::Warnings(Toasts *bubbles, QObject *parent)
    : QObject(parent)
    , m_bubbles(bubbles)
{
}

void Warnings::say(Preferences::Warns family, Toasts::Tone tone, const QString &headline,
                   const QString &detail, Toasts::Offer offer, const QString &subject)
{
    if (m_bubbles)
        m_bubbles->say(family, tone, headline, detail, offer, subject);
}

void Warnings::raise(Toasts::Tone tone, const QString &headline, const QString &detail)
{
    if (m_bubbles)
        m_bubbles->raise(tone, headline, detail, Toasts::Offer::Nothing, {});
}

void Warnings::importSettled(bool went, const QString &subject, const QString &said)
{
    using enum Preferences::Warns;
    using Tone = Toasts::Tone;
    using Offer = Toasts::Offer;
    if (went) {
        // The subject on the first line and the work's own words on the second: « 29 tomes
        // envoyés · 1 encore à venir » is `whatLanded` word for word, and it counts in
        // « tomes » whatever the medium — an import may cross several series and does not
        // know theirs yet.
        say(Imports, Tone::Done, subject, said, Offer::See, subject);
        return;
    }
    say(Failures, Tone::Failed, Words::couldNotSend(subject), said, Offer::Retry, subject);
}

void Warnings::scanFinished(const QString &counts)
{
    say(Preferences::Warns::Scans, Toasts::Tone::Done, Words::scanEnded(), counts);
}

void Warnings::scanFailed(const QString &why)
{
    say(Preferences::Warns::Failures, Toasts::Tone::Failed, Words::scanStopped(), why);
}

void Warnings::copySaved(const QString &path)
{
    say(Preferences::Warns::Downloads, Toasts::Tone::Done, Words::copyKept(),
        QFileInfo(path).fileName(), Toasts::Offer::OpenFolder, path);
}

void Warnings::exported(const QString &name, const QString &path, const QStringList &missing)
{
    say(Preferences::Warns::Downloads, Toasts::Tone::Done, Words::exportDone(),
        Words::exportDoneDetail(name, missing), Toasts::Offer::OpenFolder, path);
}

void Warnings::exportFailed(const QString &why)
{
    say(Preferences::Warns::Failures, Toasts::Tone::Failed, Words::exportStopped(), why);
}

void Warnings::commandRefused(const QString &why)
{
    say(Preferences::Warns::Failures, Toasts::Tone::Failed, Words::commandRefused(), why);
}

void Warnings::isbnCopied(const QString &isbn)
{
    raise(Toasts::Tone::Done, Words::isbnCopied(), isbn);
}

void Warnings::readsImages(const QList<QByteArray> &readable)
{
    // Once: one missing package is one sentence, not one per cover that fails to draw.
    if (m_toldWebp || readable.contains("webp"))
        return;
    m_toldWebp = true;
    raise(Toasts::Tone::Failed, Words::webpMissing(), Words::webpMissingRemedy());
}
