// The palette, and the two things about it that go wrong silently.
//
// A colour edited by hand still compiles, still draws, and only stops being readable — so the
// figures measured for it are asserted here rather than trusted. And a token
// given a value in one theme and forgotten in the other is invisible until somebody switches
// themes, which nobody does while writing.

#include "Theme.h"

#include <QColor>
#include <QGuiApplication>
#include <QMetaProperty>
#include <QPalette>
#include <QTest>

#include <cmath>

using Qt::Literals::StringLiterals::operator""_s;

namespace {
/// Puts `QGuiApplication`'s palette back on scope exit, `QVERIFY` failure included. A restore
/// written as the slot's last statement only runs when every assertion above it passed — so it
/// would skip the restore on exactly the run where the process-global palette is most likely to
/// be wrong, leaving it to decide whichever test happens to run in this process next.
class RestoresThePalette
{
public:
    RestoresThePalette() : m_was(QGuiApplication::palette()) {}
    ~RestoresThePalette() { QGuiApplication::setPalette(m_was); }

private:
    QPalette m_was;
};
} // namespace

class KeepsItsContrast : public QObject
{
    Q_OBJECT

private:
    static double luminance(const QColor &colour)
    {
        const auto channel = [](int eight) {
            const double value = eight / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * channel(colour.red()) + 0.7152 * channel(colour.green())
             + 0.0722 * channel(colour.blue());
    }

    static double contrast(const QColor &one, const QColor &other)
    {
        const double a = luminance(one);
        const double b = luminance(other);
        return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
    }

    /// Every QColor property of Theme, by name.
    static QMap<QString, QColor> colours(const Theme &theme)
    {
        QMap<QString, QColor> found;
        const QMetaObject *meta = theme.metaObject();
        for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
            const QMetaProperty property = meta->property(i);
            if (property.metaType().id() == QMetaType::QColor)
                found.insert(QString::fromUtf8(property.name()),
                             property.read(&theme).value<QColor>());
        }
        return found;
    }

private slots:
    /// The eight measured figures, against the paper of their theme.
    void the_measured_contrasts_still_hold()
    {
        struct Pair {
            bool dark;
            const char *token;
            double documented;
            double least;
        };
        static const Pair pairs[] = {
            {false, "ink",      13.29, 7.0}, {false, "inkSoft",  5.38, 4.5},
            {false, "inkFaint",  4.22, 3.0}, {false, "emerald",  5.12, 4.5},
            {true,  "ink",      15.91, 7.0}, {true,  "inkSoft",  7.36, 7.0},
            {true,  "inkFaint",  4.01, 3.0}, {true,  "emerald",  7.69, 7.0},
        };

        for (const Pair &pair : pairs) {
            Theme theme;
            theme.setDark(pair.dark);
            const QColor foreground =
                theme.property(pair.token).value<QColor>();
            const double measured = contrast(foreground, theme.paper());
            const QString what = (pair.dark ? u"dark "_s : u"light "_s)
                               + QString::fromUtf8(pair.token);
            QVERIFY2(measured >= pair.least,
                     qPrintable(u"%1 falls to %2:1, under its %3:1 floor"_s
                                    .arg(what).arg(measured, 0, 'f', 2).arg(pair.least)));
            QVERIFY2(std::abs(measured - pair.documented) <= 0.01,
                     qPrintable(u"%1 is %2:1, not its recorded %3:1"_s
                                    .arg(what).arg(measured, 0, 'f', 2).arg(pair.documented)));
        }
    }

    /// A token given a value in one theme and forgotten in the other.
    void every_token_has_a_value_in_both_themes()
    {
        Theme light;
        light.setDark(false);
        Theme dark;
        dark.setDark(true);

        const QMap<QString, QColor> pale = colours(light);
        const QMap<QString, QColor> deep = colours(dark);

        // Not `pale.keys() == deep.keys()`: `colours()` walks one shared `QMetaObject`, so the
        // two key lists are identical by construction and that comparison can never fail. The
        // size catches a token dropped from the class or a `metaType().id()` filter that
        // stopped matching — neither of which the key comparison could ever see.
        QVERIFY(!pale.isEmpty());
        QCOMPARE(pale.size(), 13);
        for (auto it = pale.constBegin(); it != pale.constEnd(); ++it)
            QVERIFY2(it.value() != deep.value(it.key()),
                     qPrintable(u"%1 is the same colour in both themes"_s.arg(it.key())));
    }

    /// Qt 6.4 has no QStyleHints::colorScheme, so the window colour is what there is to read.
    void a_dark_desktop_selects_the_dark_theme()
    {
        const RestoresThePalette restoreOnExit;

        QPalette night;
        night.setColor(QPalette::Window, QColor(u"#101010"_s));
        QGuiApplication::setPalette(night);

        Theme theme;
        theme.followSystem();
        QVERIFY(theme.dark());

        QPalette day;
        day.setColor(QPalette::Window, QColor(u"#F0F0F0"_s));
        QGuiApplication::setPalette(day);

        theme.followSystem();
        QVERIFY(!theme.dark());
    }

    /// The focus ring stands off further in the dark, because a light gap looks wider.
    void the_focus_gap_widens_in_the_dark()
    {
        Theme theme;
        theme.setDark(false);
        QCOMPARE(theme.focusGap(), 2);
        theme.setDark(true);
        QCOMPARE(theme.focusGap(), 3);
    }
};

QTEST_MAIN(KeepsItsContrast)
#include "keeps_its_contrast.moc"
