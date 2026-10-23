#pragma once

// The pieces a list of keys is drawn from, as QML reads them.
//
// The reader's shortcuts card made these, and the settings screen's keys are the same shapes
// in a smaller size: written twice, the two would drift, and a « Maj » on one card would be a
// different key from the « Maj » on the other. `KeyRow.qml` draws a list of them.

#include "Words.h"

#include <QString>
#include <QVariantMap>

namespace KeyShapes {

/// A key with a word on it. Wide when the word is longer than a letter, as the mockup pads
/// « Ctrl » and not « F ».
inline QVariantMap key(Words::KeyName name)
{
    const QString text = Words::keyName(name);
    return {{QStringLiteral("kind"), QStringLiteral("key")},
            {QStringLiteral("text"), text},
            {QStringLiteral("wide"), text.size() > 1}};
}

/// A key with a Tabler glyph on it.
inline QVariantMap key(const QString &glyph, bool wide = false)
{
    return {{QStringLiteral("kind"), QStringLiteral("key")},
            {QStringLiteral("glyph"), glyph},
            {QStringLiteral("wide"), wide}};
}

/// What stands between two keys: « + », « / », « ou ».
inline QVariantMap sign(const QString &text)
{
    return {{QStringLiteral("kind"), QStringLiteral("sign")}, {QStringLiteral("text"), text}};
}

inline QVariantMap gesture(const QString &glyph)
{
    return {{QStringLiteral("kind"), QStringLiteral("gesture")},
            {QStringLiteral("glyph"), glyph}};
}

inline QVariantMap hint(const QString &text)
{
    return {{QStringLiteral("kind"), QStringLiteral("hint")}, {QStringLiteral("text"), text}};
}

} // namespace KeyShapes
