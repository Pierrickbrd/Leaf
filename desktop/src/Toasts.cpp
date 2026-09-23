#include "Toasts.h"

#include "Words.h"

#include <QDebug>
#include <QFileInfo>
#include <QGuiApplication>

using namespace Qt::StringLiterals;

Toasts *Toasts::create(QQmlEngine *engine, QJSEngine *)
{
    auto *preferences =
        engine->singletonInstance<Preferences *>(qmlTypeId("Leaf", 1, 0, "Preferences"));
    if (!preferences) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Preferences singleton — everything will "
                              "warn, which is the noisy way for this to fail");
    }
    return new Toasts(preferences);
}

Toasts::Toasts(Preferences *preferences, std::function<bool()> awake, int lifetime,
               QObject *parent)
    : QAbstractListModel(parent)
    , m_preferences(preferences)
    , m_awake(std::move(awake))
    , m_lifetime(lifetime)
{
    if (!m_awake) {
        // What the application itself says about its own window. Read here rather than asked
        // of a window, because a bubble is raised by a model and models have no windows.
        m_awake = [] { return QGuiApplication::applicationState() == Qt::ApplicationActive; };
    }
    m_clock.setInterval(250);
    connect(&m_clock, &QTimer::timeout, this, &Toasts::sweep);
}

int Toasts::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(std::min<qsizetype>(m_shown.size(), AtOnce));
}

QVariant Toasts::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= rowCount())
        return {};
    const Bubble &one = m_shown.at(index.row());
    using enum Role;
    switch (Role(role)) {
    case Tone_:
        return QVariant::fromValue(one.tone);
    case Headline:
        return one.headline;
    case Detail:
        return one.detail;
    case Label:
        return Words::bubbleOffer(one.offer);
    case Offer_:
        return QVariant::fromValue(one.offer);
    }
    return {};
}

QHash<int, QByteArray> Toasts::roleNames() const
{
    using enum Role;
    QHash<int, QByteArray> named;
    named.insert(qToUnderlying(Tone_), "tone");
    named.insert(qToUnderlying(Headline), "headline");
    named.insert(qToUnderlying(Detail), "detail");
    named.insert(qToUnderlying(Label), "label");
    named.insert(qToUnderlying(Offer_), "offer");
    return named;
}

int Toasts::more() const
{
    return int(std::max<qsizetype>(0, m_shown.size() - AtOnce));
}

QString Toasts::moreLabel() const
{
    return Words::moreBubbles(more());
}

int Toasts::corner() const
{
    return m_preferences == nullptr ? std::to_underlying(Preferences::Corner::BottomRight)
                                    : std::to_underlying(m_preferences->corner());
}

void Toasts::say(Preferences::Warns family, Tone tone, const QString &headline,
                 const QString &detail, Offer offer, const QString &subject)
{
    if (headline.isEmpty())
        return;

    // The desktop first, because it does not depend on there being room for a bubble: a
    // reader who keeps Leaf on another virtual desktop wants the system's notification and
    // nothing else, and that is a sensible thing to want.
    if ((m_preferences == nullptr || m_preferences->reachesTheDesktop(family))
        && m_awake && !m_awake()) {
        emit escalated(headline, detail);
    }

    if (m_preferences != nullptr && !m_preferences->bubbles(family))
        return;

    // A failure stays until it is clicked. A failure that fades on its own is a failure
    // nobody read.
    const QDateTime until = tone == Tone::Failed
                                ? QDateTime()
                                : QDateTime::currentDateTime().addMSecs(m_lifetime);
    const bool drawn = m_shown.size() < AtOnce;
    if (drawn)
        beginInsertRows(QModelIndex(), int(m_shown.size()), int(m_shown.size()));
    m_shown.append({tone, headline, detail, offer, subject, until});
    if (drawn)
        endInsertRows();

    if (!m_clock.isActive())
        m_clock.start();
    emit changed();
}

void Toasts::importSettled(bool went, const QString &subject, const QString &said)
{
    using enum Preferences::Warns;
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

void Toasts::scanFinished(const QString &counts)
{
    say(Preferences::Warns::Scans, Tone::Done, Words::scanEnded(), counts);
}

void Toasts::scanFailed(const QString &why)
{
    say(Preferences::Warns::Failures, Tone::Failed, Words::scanStopped(), why);
}

void Toasts::copySaved(const QString &path)
{
    say(Preferences::Warns::Downloads, Tone::Done, Words::copyKept(),
        QFileInfo(path).fileName(), Offer::OpenFolder, path);
}

void Toasts::commandRefused(const QString &why)
{
    say(Preferences::Warns::Failures, Tone::Failed, Words::commandRefused(), why);
}

void Toasts::dismiss(int row)
{
    take(row);
}

void Toasts::act(int row)
{
    if (row < 0 || row >= rowCount())
        return;
    const Offer what = m_shown.at(row).offer;
    const QString subject = m_shown.at(row).subject;
    take(row);
    if (what != Offer::Nothing)
        emit acted(what, subject);
}

void Toasts::take(qsizetype row)
{
    if (row < 0 || row >= m_shown.size())
        return;

    // Drawn or not decides whether a view is told: the ones past the third are a count, and
    // a count changing is not a row being removed.
    const bool drawn = row < AtOnce;
    if (drawn)
        beginRemoveRows(QModelIndex(), int(row), int(row));
    m_shown.removeAt(row);
    if (drawn)
        endRemoveRows();

    // One that was waiting behind takes the place, which is a row appearing and not a row
    // changing: the view is told so rather than left to notice.
    if (drawn && m_shown.size() >= AtOnce) {
        beginInsertRows(QModelIndex(), AtOnce - 1, AtOnce - 1);
        endInsertRows();
    }
    if (m_shown.isEmpty())
        m_clock.stop();
    emit changed();
}

void Toasts::sweep()
{
    const QDateTime now = QDateTime::currentDateTime();
    for (qsizetype row = m_shown.size() - 1; row >= 0; --row) {
        const QDateTime &until = m_shown.at(row).until;
        if (until.isValid() && until <= now)
            take(row);
    }
}
