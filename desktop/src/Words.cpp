#include "Words.h"

#include <QStringList>

#include <cmath>

using Qt::Literals::StringLiterals::operator""_s;

namespace {

/// A number as French writes it: no decimal point, and no trailing zero on a whole one.
///
/// Volume numbers are halves as often as not — a 3.5 is a side story — and « Tome 3.5 » is
/// English. « Tome 3,5 » is French, and « Tome 12,0 » is neither.
QString number(double value)
{
    if (std::abs(value - std::round(value)) < 0.0001)
        return QString::number(static_cast<qint64>(std::llround(value)));
    return QString::number(value, 'g', 4).replace(u'.', u',');
}

} // namespace

namespace Words {

const QChar Nbsp = QChar(0x00A0);

QString volumes(int count, std::optional<Api::Medium> medium)
{
    // Zero and one both take the singular in French; two and above do not.
    const bool many = count >= 2;
    if (medium == Api::Medium::Bd)
        return u"%1 %2"_s.arg(count).arg(many ? u"albums"_s : u"album"_s);
    return u"%1 %2"_s.arg(count).arg(many ? u"tomes"_s : u"tome"_s);
}

QString readStatus(Api::ReadStatus status)
{
    using enum Api::ReadStatus;

    switch (status) {
    case Unread:
        return u"Non lues"_s;
    case InProgress:
        return u"En cours"_s;
    case Read:
        return u"Terminées"_s;
    }
    return {};
}

QString medium(Api::Medium value)
{
    using enum Api::Medium;

    switch (value) {
    case Manga:
        return u"Manga"_s;
    // An acronym keeps its case. This is the reason none of this is a capitalise-the-first
    // -letter helper applied to the contract's spelling.
    case Bd:
        return u"BD"_s;
    case Comics:
        return u"Comics"_s;
    case Manhwa:
        return u"Manhwa"_s;
    case Manhua:
        return u"Manhua"_s;
    case Webtoon:
        return u"Webtoon"_s;
    case Artbook:
        return u"Artbook"_s;
    case Other:
        return u"Autre"_s;
    }
    return {};
}

QString labelled(const QString &label, const QString &value)
{
    return label + Nbsp + u": "_s + value;
}

QString where(Api::UpNext::Kind kind, std::optional<double> number_, std::optional<int> page,
              int pageCount, const std::optional<QString> &chapter)
{
    QStringList parts;

    if (number_.has_value())
        parts << (kind == Api::UpNext::Kind::Chapter ? u"Chapitre "_s : u"Tome "_s)
                     + number(*number_);

    if (page.has_value() && pageCount > 0)
        parts << u"Page %1/%2"_s.arg(*page).arg(pageCount);

    // A chapter entry already said which chapter it is; repeating it would read as two.
    if (chapter && !chapter->isEmpty() && kind != Api::UpNext::Kind::Chapter)
        parts << *chapter;

    return parts.join(u" · "_s);
}

QString nothingHere(const QStringList &pills, int withoutThem)
{
    // The pills arrive already worded, so this sentence names them exactly as the bar does.
    // Section 05's rule still applies to the sentence itself: one capital, at the start.
    return u"Aucun résultat dans %1 · %2 sans les filtres"_s.arg(pills.join(u", "_s))
        .arg(withoutThem);
}

} // namespace Words
