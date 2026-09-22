#include "Preferences.h"

#include "Words.h"

#include <QVariantMap>
#include <memory>
#include <utility>

using namespace Qt::StringLiterals;

namespace {

/// Written as a word and never as a number: a file holding `2` is a file nobody can read,
/// and an enumeration that gains a value in the middle would silently move everyone's
/// choice.
QString spell(Preferences::Appearance which)
{
    using enum Preferences::Appearance;

    switch (which) {
    case Light:
        return u"light"_s;
    case Dark:
        return u"dark"_s;
    case System:
        break;
    }
    return u"system"_s;
}

Preferences::Appearance read(const QString &word)
{
    using enum Preferences::Appearance;

    if (word == u"light"_s)
        return Light;
    if (word == u"dark"_s)
        return Dark;
    // A word this version does not know is the desktop's choice, which is the answer that
    // is right whatever the word meant.
    return System;
}

} // namespace

Preferences *Preferences::create(QQmlEngine *, QJSEngine *)
{
    // Owned until the handoff, then the engine owns it: `QML_SINGLETON` takes the pointer
    // and deletes it with the engine. Written this way so that a throw between here and
    // the return could not leak it.
    return std::make_unique<Preferences>().release();
}

Preferences::Preferences(QObject *parent)
    : QObject(parent)
{
    m_appearance = read(m_file.value(u"appearance"_s).toString());
    m_lastPlace = QUrl(m_file.value(u"lastPlace"_s).toString());
}

QString Preferences::appearanceTitle() const
{
    return Words::appearance();
}

QVariantList Preferences::appearances() const
{
    return {
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::System)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::System)},
                    {u"icon"_s, u"contrast"_s}},
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::Light)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::Light)},
                    {u"icon"_s, u"light_mode"_s}},
        QVariantMap{{u"value"_s, std::to_underlying(Appearance::Dark)},
                    {u"label"_s, Words::appearanceChoice(Words::Looks::Dark)},
                    {u"icon"_s, u"dark_mode"_s}},
    };
}

void Preferences::chooseAppearance(Appearance wanted)
{
    if (wanted == m_appearance)
        return;

    m_appearance = wanted;
    // Written at once and not on the way out: an application that is killed, or that
    // crashes, has still been told what somebody wanted.
    m_file.setValue(u"appearance"_s, spell(wanted));
    m_file.sync();
    emit changed();
    emit appearanceChosen(wanted);
}

void Preferences::rememberPlace(const QUrl &place)
{
    if (!place.isValid() || place.isEmpty() || place == m_lastPlace)
        return;
    m_lastPlace = place;
    m_file.setValue(u"lastPlace"_s, place.toString());
    m_file.sync();
    emit changed();
}
