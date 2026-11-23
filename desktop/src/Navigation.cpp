#include "Navigation.h"

#include "Words.h"

#include <algorithm>
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

bool Navigation::holdsSeries() const
{
    return std::ranges::any_of(m_stack, [](const Step &one) {
        return one.where == Destination::Series;
    });
}

QString Navigation::label() const
{
    return Words::destination(destination());
}

QString Navigation::backLabel() const
{
    // The page underneath, not the one showing. A back button named after where you are
    // would be a button that tells you nothing you did not know.
    if (m_stack.size() < 2)
        return Words::backTo({});
    return Words::backTo(m_stack.at(m_stack.size() - 2).where);
}

bool Navigation::open(Destination where, const QVariantMap &with)
{
    const std::optional<QString> needs = required(where);
    if (!needs.has_value())
        return false;
    if (!needs->isEmpty() && with.value(*needs).toString().isEmpty())
        return false;

    // Where one already is, with what one already has: nothing to open. Pushed, the shelf stood
    // in the stack as a second copy of itself, and the Escape that came back from it came back
    // to what it had left, and seemed to do nothing. Only for the screens that name nothing: a
    // series page can show another edition than the one its parameters say, and opening the
    // series it was opened with is then a real request.
    if (needs->isEmpty() && where == destination() && with == parameters())
        return true;

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
