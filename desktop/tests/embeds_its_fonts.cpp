// That the fonts are the ones asked for, and not what the machine had lying around.
//
// This does not check that the files load — it checks the family Qt actually resolves to.
// `fc-match sans-serif` answers Noto Sans on the machines this runs on, and neither Inter nor
// Barlow Condensed is installed on them. So a renamed or missing .ttf does not crash and does
// not warn: it silently draws the whole application in another face.

#include "Fonts.h"

#include <QFont>
#include <QFontInfo>
#include <QTest>

class EmbedsItsFonts : public QObject
{
    Q_OBJECT

private slots:
    void the_four_faces_are_registered()
    {
        QVERIFY(Fonts::load());
    }

    void the_display_family_is_the_one_asked_for_and_not_a_fallback()
    {
        QVERIFY(Fonts::load());
        QCOMPARE(QFontInfo(QFont(Fonts::display())).family(), Fonts::display());
    }

    void the_text_family_is_the_one_asked_for_and_not_a_fallback()
    {
        QVERIFY(Fonts::load());
        QCOMPARE(QFontInfo(QFont(Fonts::text())).family(), Fonts::text());
    }

    /// Loading twice is what a test binary and a restarted engine both do.
    void loading_twice_is_harmless()
    {
        QVERIFY(Fonts::load());
        QVERIFY(Fonts::load());
        QCOMPARE(QFontInfo(QFont(Fonts::text())).family(), Fonts::text());
    }
};

QTEST_MAIN(EmbedsItsFonts)
#include "embeds_its_fonts.moc"
