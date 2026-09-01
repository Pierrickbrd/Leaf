#include "Navigation.h"

#include "Words.h"

#include <optional>

using Qt::Literals::StringLiterals::operator""_s;

namespace {
/// What a destination cannot be opened without: `std::nullopt` for a destination outside the
/// enumeration, an empty string for one that needs nothing, the field name otherwise.
std::optional<QString> required(Navigation::Destination where)
{
    using enum Navigation::Destination;

    switch (where) {
    case Series:
        return u"series"_s;
    case Reader:
        return u"entry"_s;
    case Shelf:
    case Health:
    case Settings:
        return QString();
    }
    // Not a default case: the switch above is exhaustive, so adding a destination breaks the
    // compile here rather than silently landing in a fallback. This line is what an int cast
    // from QML reaches.
    return std::nullopt;
}
} // namespace

Navigation::Navigation(QObject *parent) : QObject(parent)
{
    m_stack.append({Destination::Shelf, {}});
}

QString Navigation::label() const
{
    return Words::destination(destination());
}

bool Navigation::open(Destination where, const QVariantMap &with)
{
    const std::optional<QString> needs = required(where);
    if (!needs.has_value())
        return false;
    if (!needs->isEmpty() && with.value(*needs).toString().isEmpty())
        return false;

    m_stack.append({where, with});
    emit changed();
    return true;
}

void Navigation::back()
{
    if (!canGoBack())
        return;
    m_stack.removeLast();
    emit changed();
}
