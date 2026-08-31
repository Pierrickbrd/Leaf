#include "Fonts.h"

#include <QFontDatabase>
#include <QStringList>

using Qt::Literals::StringLiterals::operator""_s;

namespace {
const QStringList &files()
{
    static const QStringList paths = {
        u":/fonts/BarlowCondensed-SemiBold.ttf"_s,
        u":/fonts/BarlowCondensed-Bold.ttf"_s,
        u":/fonts/Inter_18pt-Regular.ttf"_s,
        u":/fonts/Inter_18pt-Medium.ttf"_s,
    };
    return paths;
}
} // namespace

namespace Fonts {

bool load()
{
    bool all = true;
    for (const QString &path : files())
        all = QFontDatabase::addApplicationFont(path) >= 0 && all;
    return all;
}

const QString &display()
{
    static const QString family = u"Barlow Condensed"_s;
    return family;
}

const QString &text()
{
    static const QString family = u"Inter 18pt"_s;
    return family;
}

} // namespace Fonts
