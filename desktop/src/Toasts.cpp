#include "Toasts.h"

#include "Words.h"

#include <QDebug>
#include <QGuiApplication>

#include <algorithm>

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
    return parent.isValid() ? 0 : int(std::min<qsizetype>(m_shown.size(), m_atOnce));
}

void Toasts::setCramped(bool cramped)
{
    const int drawn = cramped ? AtOnceWhenShort : AtOnce;
    if (drawn == m_atOnce)
        return;
    beginResetModel();
    m_atOnce = drawn;
    endResetModel();
    emit changed();
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
    return int(std::max<qsizetype>(0, m_shown.size() - m_atOnce));
}

QVariantList Toasts::history() const
{
    QVariantList all;
    for (const Memory &one : m_history) {
        all << QVariantMap{{u"failed"_s, one.tone == Tone::Failed},
                           {u"headline"_s, one.headline},
                           {u"detail"_s, one.detail},
                           {u"when"_s, Words::moment(one.at.toMSecsSinceEpoch())}};
    }
    return all;
}

QString Toasts::historyTitle() const
{
    return Words::notificationHistory();
}

QString Toasts::historyEmpty() const
{
    return Words::nothingToldYet();
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

    // Remembered whether or not it is drawn: a family a reader silenced is still something
    // that happened, and the list is where to look for it.
    m_history.prepend({tone, headline, detail, QDateTime::currentDateTime()});
    while (m_history.size() > Remembered)
        m_history.removeLast();
    emit historyChanged();

    // The desktop first, because it does not depend on there being room for a bubble: a
    // reader who keeps Leaf on another virtual desktop wants the system's notification and
    // nothing else, and that is a sensible thing to want.
    if ((m_preferences == nullptr || m_preferences->reachesTheDesktop(family))
        && m_awake && !m_awake()) {
        emit escalated(headline, detail);
    }

    if (m_preferences != nullptr && !m_preferences->bubbles(family))
        return;
    raise(tone, headline, detail, offer, subject);
}

void Toasts::raise(Tone tone, const QString &headline, const QString &detail, Offer offer,
                   const QString &subject)
{
    // One lifetime for every tone (the header says why).
    const QDateTime until = QDateTime::currentDateTime().addMSecs(m_lifetime);
    const bool drawn = m_shown.size() < m_atOnce;
    if (drawn)
        beginInsertRows(QModelIndex(), int(m_shown.size()), int(m_shown.size()));
    m_shown.append({tone, headline, detail, offer, subject, until, false, 0});
    if (drawn)
        endInsertRows();

    if (!m_clock.isActive())
        m_clock.start();
    emit changed();
}

void Toasts::forget()
{
    if (!m_shown.isEmpty()) {
        beginResetModel();
        m_shown.clear();
        endResetModel();
        m_clock.stop();
        emit changed();
    }
    if (m_history.isEmpty())
        return;
    m_history.clear();
    emit historyChanged();
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
    const bool drawn = row < m_atOnce;
    if (drawn)
        beginRemoveRows(QModelIndex(), int(row), int(row));
    m_shown.removeAt(row);
    if (drawn)
        endRemoveRows();

    // One that was waiting behind takes the place, which is a row appearing and not a row
    // changing: the view is told so rather than left to notice.
    if (drawn && m_shown.size() >= m_atOnce) {
        beginInsertRows(QModelIndex(), m_atOnce - 1, m_atOnce - 1);
        endInsertRows();
    }
    if (m_shown.isEmpty())
        m_clock.stop();
    emit changed();
}

void Toasts::hold(int row, bool held)
{
    if (row < 0 || row >= rowCount())
        return;
    Bubble &one = m_shown[row];
    if (one.held == held)
        return;
    one.held = held;
    const QDateTime now = QDateTime::currentDateTime();
    if (held)
        one.left = std::max<qint64>(0, now.msecsTo(one.until));
    else
        one.until = now.addMSecs(one.left);
}

void Toasts::sweep()
{
    const QDateTime now = QDateTime::currentDateTime();
    for (qsizetype row = m_shown.size() - 1; row >= 0; --row) {
        const Bubble &one = m_shown.at(row);
        if (!one.held && one.until <= now)
            take(row);
    }
}
