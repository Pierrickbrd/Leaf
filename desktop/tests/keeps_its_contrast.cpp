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
#include <QSet>
#include <QSignalSpy>
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
            {false, "alert",     6.90, 4.5},
            {true,  "ink",      15.91, 7.0}, {true,  "inkSoft",  7.36, 7.0},
            {true,  "inkFaint",  4.01, 3.0}, {true,  "emerald",  7.69, 7.0},
            {true,  "alert",     5.71, 4.5},
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

    /// The initial of a profile is written on its tint, so it is measured there and not against
    /// the paper: thirty-two pairs, sixteen colours in two themes. The ink is the surface tone
    /// of the theme it is in, and the tints were chosen so that it reads on every one.
    void the_initial_of_a_profile_reads_on_each_of_its_sixteen_tints()
    {
        for (const bool dark : {false, true}) {
            Theme theme;
            theme.setDark(dark);
            QCOMPARE(theme.profileColors().size(), 16);
            QCOMPARE(theme.profileInk(), QColor::fromRgb(dark ? 0x0C100Eu : 0xF0ECE3u));
            const QVariantMap tints = theme.profileTints();
            for (const QString &id : theme.profileColors()) {
                const double measured =
                    contrast(theme.profileInk(), tints.value(id).value<QColor>());
                QVERIFY2(measured >= 4.5,
                         qPrintable(u"%1 %2 falls to %3:1, under 4,5:1"_s
                                        .arg(dark ? u"dark"_s : u"light"_s, id)
                                        .arg(measured, 0, 'f', 2)));
            }
        }
    }

    /// Two colours that drew the same would make a swatch picker lie, and a word the client
    /// was not taught must still draw something.
    void sixteen_tints_are_distinct_and_an_unknown_one_is_slate()
    {
        for (const bool dark : {false, true}) {
            Theme theme;
            theme.setDark(dark);
            const QVariantMap tints = theme.profileTints();
            QSet<QRgb> seen;
            for (const QString &id : theme.profileColors())
                seen.insert(tints.value(id).value<QColor>().rgb());
            QCOMPARE(seen.size(), 16);
            const QColor slate = tints.value(u"ardoise"_s).value<QColor>();
            QCOMPARE(theme.profileTintIn(tints, u"chartreuse"_s), slate);
            QCOMPARE(theme.profileTintIn(tints, QString()), slate);
            QCOMPARE(theme.profileTintIn(tints, u"terre"_s),
                     tints.value(u"terre"_s).value<QColor>());
        }
        Theme theme;
        QCOMPARE(theme.profileColors().first(), u"emeraude"_s);
        QCOMPARE(theme.profileColors().last(), u"terre"_s);
    }

    /// A swatch that was bound to the map must turn with the theme by itself: the map and the
    /// ink notify, so nothing but the binding has to know.
    void the_tints_and_the_ink_follow_the_theme()
    {
        Theme theme;
        theme.setDark(false);
        const QVariantMap pale = theme.profileTints();
        const QColor paleInk = theme.profileInk();
        QSignalSpy turned(&theme, &Theme::changed);
        theme.setDark(true);
        QCOMPARE(turned.count(), 1);
        QVERIFY(theme.profileTints() != pale);
        QVERIFY(theme.profileInk() != paleInk);
        QCOMPARE(theme.metaObject()->property(
                     theme.metaObject()->indexOfProperty("profileTints")).notifySignal().name(),
                 QByteArray("changed"));
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
        QCOMPARE(pale.size(), 22);
        for (auto it = pale.constBegin(); it != pale.constEnd(); ++it)
            QVERIFY2(it.value() != deep.value(it.key()),
                     qPrintable(u"%1 is the same colour in both themes"_s.arg(it.key())));
    }

    /// The one token in the palette that is not read against the page: the resume band's
    /// button is an emerald pill, and its word sits on the emerald. Measured against the paper
    /// it would pass at any tone and still be unreadable where it is actually drawn.
    void the_word_on_the_emerald_is_measured_against_the_emerald()
    {
        struct Pair {
            bool dark;
            double documented;
        };
        static const Pair pairs[] = {{false, 5.56}, {true, 7.69}};

        for (const Pair &pair : pairs) {
            Theme theme;
            theme.setDark(pair.dark);
            const double measured = contrast(theme.onEmerald(), theme.emerald());
            const QString what = pair.dark ? u"dark onEmerald"_s : u"light onEmerald"_s;
            QVERIFY2(measured >= 4.5,
                     qPrintable(u"%1 falls to %2:1, under its 4.5:1 floor"_s
                                    .arg(what).arg(measured, 0, 'f', 2)));
            QVERIFY2(std::abs(measured - pair.documented) <= 0.01,
                     qPrintable(u"%1 is %2:1, not its recorded %3:1"_s
                                    .arg(what).arg(measured, 0, 'f', 2).arg(pair.documented)));
        }
    }

    /// The lip under a key is what makes it a key: a lip lighter than its face reads as a
    /// key lit from below, which is no key at all.
    void the_lip_of_a_key_is_darker_than_its_face_in_both_themes()
    {
        for (const bool dark : {false, true}) {
            Theme theme;
            theme.setDark(dark);
            QVERIFY2(theme.emeraldLip().lightnessF() < theme.emerald().lightnessF(),
                     dark ? "dark lip" : "light lip");
        }
    }

    /// The ISBN's barcode is dark bars on a lighter ground in both themes, or a scanner reads
    /// it inverted: in the light theme the emerald key drew light bars on dark (22/10/2026).
    /// The dark theme stays the key that was approved.
    void the_barcode_is_dark_on_light_in_both_themes()
    {
        struct Pair {
            bool dark;
            double documented;
        };
        static const Pair pairs[] = {{false, 8.54}, {true, 7.69}};
        for (const Pair &pair : pairs) {
            Theme theme;
            theme.setDark(pair.dark);
            const QString what = pair.dark ? u"dark barcode"_s : u"light barcode"_s;
            QVERIFY2(theme.barcodeInk().lightnessF() < theme.barcodeFace().lightnessF(),
                     qPrintable(what));
            QVERIFY2(theme.barcodeLip().lightnessF() < theme.barcodeFace().lightnessF(),
                     qPrintable(what));
            const double measured = contrast(theme.barcodeInk(), theme.barcodeFace());
            QVERIFY2(std::abs(measured - pair.documented) <= 0.01,
                     qPrintable(u"%1 is %2:1, not its recorded %3:1"_s
                                    .arg(what).arg(measured, 0, 'f', 2).arg(pair.documented)));
        }
        Theme dark;
        dark.setDark(true);
        QCOMPARE(dark.barcodeFace(), dark.emerald());
        QCOMPARE(dark.barcodeInk(), dark.onEmerald());
        QCOMPARE(dark.barcodeLip(), dark.emeraldLip());
    }

    /// The alert colour is not read on the paper alone: it is written on a card, in the
    /// hollow a hovered row draws, and on its own wash. The hollow is the one that decided
    /// the dark value — a deeper garnet fell to 4,44 there, under the floor, while every
    /// other surface stayed comfortable and would have let it through.
    void the_alert_is_measured_on_every_surface_it_is_written_on()
    {
        struct Pair {
            bool dark;
            const char *surface;
            double documented;
        };
        static const Pair pairs[] = {
            {false, "paper", 6.90}, {false, "surface", 7.48},
            {false, "onBar", 6.88}, {false, "alertWash", 6.76},
            {true,  "paper", 5.71}, {true,  "surface", 5.30},
            {true,  "onBar", 4.88}, {true,  "alertWash", 5.23},
        };

        for (const Pair &pair : pairs) {
            Theme theme;
            theme.setDark(pair.dark);
            const QColor under = theme.property(pair.surface).value<QColor>();
            const double measured = contrast(theme.alert(), under);
            const QString what = (pair.dark ? u"dark alert on "_s : u"light alert on "_s)
                               + QString::fromUtf8(pair.surface);
            QVERIFY2(measured >= 4.5,
                     qPrintable(u"%1 falls to %2:1, under its 4.5:1 floor"_s
                                    .arg(what).arg(measured, 0, 'f', 2)));
            QVERIFY2(std::abs(measured - pair.documented) <= 0.01,
                     qPrintable(u"%1 is %2:1, not its recorded %3:1"_s
                                    .arg(what).arg(measured, 0, 'f', 2).arg(pair.documented)));
        }
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

    /// The three radii nothing else in this suite calls — a rounded rectangle either matches
    /// the measured figure or it does not, and nobody would notice one drawn with the wrong
    /// one until it sat next to a real card.
    void the_three_radii_are_the_ones_measured()
    {
        Theme theme;
        QCOMPARE(theme.cardRadius(), 18);
        QCOMPARE(theme.coverRadius(), 12);
        QCOMPARE(theme.buttonRadius(), 12);
    }

    /// `displayFamily`/`textFamily` exist so QML reaches the two families through `Theme`
    /// rather than a second singleton — see the header comment. What is worth asserting is
    /// that they still agree with `Fonts`, not a family name copied a second time in this file.
    void the_two_families_still_agree_with_fonts()
    {
        Theme theme;
        QCOMPARE(theme.displayFamily(), Fonts::display());
        QCOMPARE(theme.textFamily(), Fonts::text());
    }
};

QTEST_MAIN(KeepsItsContrast)
#include "keeps_its_contrast.moc"
