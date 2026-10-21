#include "ReaderCaptions.h"

#include "Words.h"

#include <QVariantMap>

#include <optional>
#include <utility>

using Qt::Literals::StringLiterals::operator""_s;

ReaderCaptions::ReaderCaptions(QObject *parent)
    : QObject(parent)
{
}

QString ReaderCaptions::volumePanel() const
{
    return Words::readerVolumePanel();
}

QString ReaderCaptions::settings() const
{
    return Words::readerSettings();
}

QString ReaderCaptions::fullScreen() const
{
    return Words::readerFullScreen(true);
}

QString ReaderCaptions::leaveFullScreen() const
{
    return Words::readerFullScreen(false);
}

QString ReaderCaptions::nextPage() const
{
    return Words::readerPage(true);
}

QString ReaderCaptions::previousPage() const
{
    return Words::readerPage(false);
}

QString ReaderCaptions::nextChapter() const
{
    return Words::readerChapterStep(true);
}

QString ReaderCaptions::previousChapter() const
{
    return Words::readerChapterStep(false);
}

QString ReaderCaptions::cardTitle() const
{
    return Words::readerCardTitle();
}

QString ReaderCaptions::closeCard() const
{
    return Words::readerCloseCard();
}

QString ReaderCaptions::backToSeries() const
{
    return Words::readerBackToSeries();
}

QString ReaderCaptions::stayHere() const
{
    return Words::readerStayHere();
}

QString ReaderCaptions::thisSeries() const
{
    return Words::readerThisSeries();
}

QString ReaderCaptions::thisDesk() const
{
    return Words::readerThisDesk();
}

QString ReaderCaptions::deskNote() const
{
    return Words::readerDeskNote();
}

QString ReaderCaptions::pagesGroup() const
{
    return Words::readerDeskGroup(Words::ReaderDeskGroup::Pages);
}

QString ReaderCaptions::comfortGroup() const
{
    return Words::readerDeskGroup(Words::ReaderDeskGroup::Comfort);
}

QString ReaderCaptions::layoutName() const
{
    return Words::readerSetting(Words::ReaderSetting::Layout);
}

QString ReaderCaptions::fitName() const
{
    return Words::readerSetting(Words::ReaderSetting::Fit);
}

QString ReaderCaptions::backdropName() const
{
    return Words::readerSetting(Words::ReaderSetting::Backdrop);
}

QString ReaderCaptions::motionName() const
{
    return Words::readerSetting(Words::ReaderSetting::Motion);
}

QString ReaderCaptions::factSeries() const
{
    return Words::readerFact(Words::ReaderFact::Series);
}

QString ReaderCaptions::factPages() const
{
    return Words::readerFact(Words::ReaderFact::Pages);
}

QString ReaderCaptions::factChapters() const
{
    return Words::readerFact(Words::ReaderFact::Chapters);
}

QString ReaderCaptions::factPublished() const
{
    return Words::readerFact(Words::ReaderFact::Published);
}

QString ReaderCaptions::factIsbn() const
{
    return Words::readerFact(Words::ReaderFact::Isbn);
}

QString ReaderCaptions::factRead() const
{
    return Words::readerFact(Words::ReaderFact::Read);
}

QString ReaderCaptions::copyIsbn() const
{
    return Words::readerCopyIsbn();
}

QString ReaderCaptions::volumeHead(const QString &entry, const QString &title) const
{
    return Words::readerVolumeHead(entry, title);
}

QString ReaderCaptions::published(const QString &iso) const
{
    return Words::readerPublished(iso);
}

QString ReaderCaptions::readAt(int page, int count, bool finished) const
{
    return Words::readerReadAt(page, count, finished);
}

QString ReaderCaptions::chapterPage(int startPage) const
{
    return Words::readerChapterPage(startPage + 1);
}

QVariantList ReaderCaptions::directions() const
{
    using Way = Api::ReadingDirection;
    QVariantList all{QVariantMap{{u"value"_s, -1},
                                 {u"label"_s, Words::readerDirection(std::nullopt)}}};
    for (const Way way : {Way::LeftToRight, Way::RightToLeft, Way::Vertical})
        all.append(QVariantMap{{u"value"_s, std::to_underlying(way)},
                               {u"label"_s, Words::readerDirection(way)}});
    return all;
}

QVariantList ReaderCaptions::layouts() const
{
    using Shape = Preferences::PageLayout;
    const auto one = [](Shape which, const QString &name) {
        return QVariantMap{{u"value"_s, std::to_underlying(which)},
                           {u"label"_s, Words::readerLayout(which)},
                           {u"name"_s, name}};
    };
    return {one(Shape::Single, u"single"_s), one(Shape::Double, u"double"_s),
            one(Shape::Auto, u"auto"_s)};
}

namespace {
QVariantMap aFit(Preferences::Fit which, const QString &name)
{
    return {{u"value"_s, std::to_underlying(which)},
            {u"label"_s, Words::readerFit(which)},
            {u"name"_s, name}};
}
} // namespace

QVariantList ReaderCaptions::fits() const
{
    using Fit = Preferences::Fit;
    return {aFit(Fit::Height, u"height"_s), aFit(Fit::Width, u"width"_s),
            aFit(Fit::Original, u"original"_s)};
}

QVariantList ReaderCaptions::stripFits() const
{
    using Fit = Preferences::Fit;
    return {aFit(Fit::Width, u"width"_s), aFit(Fit::Original, u"original"_s)};
}

QVariantList ReaderCaptions::backdrops() const
{
    using Backdrop = Preferences::Backdrop;
    const auto one = [](Backdrop which, const QString &name) {
        return QVariantMap{{u"value"_s, std::to_underlying(which)},
                           {u"label"_s, Words::readerBackdrop(which)},
                           {u"name"_s, name}};
    };
    return {one(Backdrop::Black, u"black"_s), one(Backdrop::Grey, u"grey"_s),
            one(Backdrop::White, u"white"_s)};
}

QVariantList ReaderCaptions::motions() const
{
    using Motion = Preferences::Motion;
    const auto one = [](Motion which, const QString &name) {
        return QVariantMap{{u"value"_s, std::to_underlying(which)},
                           {u"label"_s, Words::readerMotion(which)},
                           {u"name"_s, name}};
    };
    return {one(Motion::Slide, u"slide"_s), one(Motion::Fade, u"fade"_s),
            one(Motion::Instant, u"instant"_s)};
}

QString ReaderCaptions::declares(int declaredDirection) const
{
    const bool known = declaredDirection >= 0
                       && declaredDirection <= std::to_underlying(Api::ReadingDirection::Vertical);
    return Words::readerDeclares(
        known ? std::optional(Api::ReadingDirection(declaredDirection)) : std::nullopt);
}

QString ReaderCaptions::where(const QString &entry, const QString &chapter,
                              const QString &title) const
{
    return Words::readerWhere(entry, chapter, title);
}

QString ReaderCaptions::scrubbing(const QString &chapter, const QString &title, int page) const
{
    return Words::readerScrubbing(chapter, title, page + 1);
}

QString ReaderCaptions::besideLabel(const QString &label, const QString &title) const
{
    return Words::readerBesideLabel(label, title);
}

QString ReaderCaptions::volumeEnd(const QString &label, const QString &title) const
{
    return Words::readerVolumeEnd(label, title);
}

QString ReaderCaptions::outOf(int count) const
{
    return Words::readerOutOf(count);
}

QString ReaderCaptions::zoom(qreal level) const
{
    return Words::readerZoom(level);
}

QString ReaderCaptions::shortcuts() const
{
    return Words::readerShortcuts();
}

namespace {

/// A key with a word on it. Wide when the word is longer than a letter, as the mockup pads
/// « Ctrl » and not « F ».
QVariantMap key(Words::KeyName name)
{
    const QString text = Words::keyName(name);
    return {{u"kind"_s, u"key"_s}, {u"text"_s, text}, {u"wide"_s, text.size() > 1}};
}

/// A key with a Tabler glyph on it.
QVariantMap key(const QString &glyph, bool wide = false)
{
    return {{u"kind"_s, u"key"_s}, {u"glyph"_s, glyph}, {u"wide"_s, wide}};
}

QVariantMap sign(const QString &text)
{
    return {{u"kind"_s, u"sign"_s}, {u"text"_s, text}};
}

QVariantMap gesture(const QString &glyph)
{
    return {{u"kind"_s, u"gesture"_s}, {u"glyph"_s, glyph}};
}

QVariantMap hint(const QString &text)
{
    return {{u"kind"_s, u"hint"_s}, {u"text"_s, text}};
}

QVariantMap row(Words::ShortcutDoes does, const QVariantList &keys)
{
    return {{u"does"_s, Words::shortcutDoes(does)}, {u"keys"_s, keys}};
}

QVariantMap group(const QString &name, Words::ShortcutGroup which, const QVariantList &rows)
{
    return {{u"name"_s, name}, {u"title"_s, Words::shortcutGroup(which)}, {u"rows"_s, rows}};
}

} // namespace

QVariantList ReaderCaptions::shortcutGroups(bool rightToLeft) const
{
    using Words::KeyName;
    using enum Words::ShortcutDoes;
    const QString left = u"tabler-arrow-left"_s;
    const QString right = u"tabler-arrow-right"_s;
    const QVariantMap plus = sign(Words::shortcutAnd());
    const QVariantMap either = sign(Words::shortcutOr());

    return {
        group(u"navigate"_s, Words::ShortcutGroup::Navigate,
              {row(rightToLeft ? TurnByArrowRightToLeft : TurnByArrow, {key(left), key(right)}),
               row(TurnBySpace, {key(KeyName::Space), sign(Words::shortcutSlash()),
                                 key(KeyName::Shift), plus, key(KeyName::Space)}),
               row(rightToLeft ? ToTheEndsRightToLeft : ToTheEnds,
                   {key(KeyName::Ctrl), plus, key(left), key(right)}),
               row(BackAPage, {key(u"tabler-backspace"_s, true)})}),
        group(u"display"_s, Words::ShortcutGroup::Display,
              {row(FullScreen, {key(KeyName::F), either, key(KeyName::F11)}),
               row(VolumeInfo, {key(KeyName::I)}),
               row(TheseShortcuts, {key(KeyName::Question), either, key(KeyName::F1)})}),
        group(u"zoom"_s, Words::ShortcutGroup::Zoom,
              {row(ZoomByKey, {key(KeyName::Ctrl), plus, key(u"tabler-plus"_s),
                               key(u"tabler-minus"_s)}),
               row(BackToWhole, {key(KeyName::Ctrl), plus, key(KeyName::Zero)}),
               row(ZoomByWheel, {key(KeyName::Ctrl), plus, gesture(u"tabler-mouse"_s)}),
               row(ZoomWhereAimed, {gesture(u"tabler-mouse"_s), hint(Words::doubleClick())}),
               row(Pinch, {gesture(u"tabler-hand-two-fingers"_s)})}),
        group(u"leave"_s, Words::ShortcutGroup::Leave,
              {row(CloseWhatIsOpen, {key(KeyName::Escape)}),
               row(BackToSeries, {key(KeyName::Alt), plus, key(left)}),
               row(CloseReader, {key(KeyName::Ctrl), plus, key(KeyName::W)})}),
    };
}
