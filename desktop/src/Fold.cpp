#include "Fold.h"

#include <QList>
#include <QStringView>

namespace Fold {

QString searchKey(const QString &text)
{
    // `server/src/store/text.rs`, line for line: NFD and not NFKD, the same five blocks of
    // marks dropped, the same characters joining. One limit: Rust keeps a mark it calls
    // alphabetic (a Devanagari vowel sign) where `isLetterOrNumber` separates on it.
    const auto mark = [](char32_t ch) {
        return (ch >= 0x0300 && ch <= 0x036F) || (ch >= 0x1AB0 && ch <= 0x1AFF)
               || (ch >= 0x1DC0 && ch <= 0x1DFF) || (ch >= 0x20D0 && ch <= 0x20FF)
               || (ch >= 0xFE20 && ch <= 0xFE2F);
    };
    QString out;
    bool pending = false;
    const auto write = [&out, &pending](QStringView letters) {
        if (pending)
            out.append(u' ');
        pending = false;
        out.append(letters);
    };
    for (const char32_t ch : text.normalized(QString::NormalizationForm_D).toUcs4()) {
        if (mark(ch))
            continue;
        switch (ch) {
        case U'\'':
        case U'\u{2019}':
        case U'\u{2bc}':
        case U'`':
            continue;
        case U'\u{e6}':
        case U'\u{c6}':
            write(u"ae");
            continue;
        case U'\u{153}':
        case U'\u{152}':
            write(u"oe");
            continue;
        case U'\u{df}':
            write(u"ss");
            continue;
        case U'\u{f8}':
        case U'\u{d8}':
            write(u"o");
            continue;
        default:
            break;
        }
        if (QChar::isLetterOrNumber(ch)) {
            const char32_t lower = QChar::toLower(ch);
            write(QString::fromUcs4(&lower, 1));
        } else {
            pending = !out.isEmpty();
        }
    }
    return out;
}

} // namespace Fold
