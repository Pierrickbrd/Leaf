#pragma once

// What the reader's bars say.
//
// The split `CardCaptions.h` made beside `Imports`, made beside `Reader`: `Reader` holds the
// open book and the words that are facts about it — « Tome 12 », « Tome 12 terminé » — and
// this holds the words of the screen drawn over it, its buttons and the bubble over its
// progress bar. `Reader` was already six hundred lines of opening, turning and recording; a
// dozen labels for buttons it has never heard of did not belong in it.
//
// Nothing here decides anything and nothing here changes: every answer is a function of its
// arguments, so a binding on one is refreshed by the property of `Reader` it was handed.
// `Words` holds the French and the typography.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

class ReaderCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString volumePanel READ volumePanel CONSTANT)
    Q_PROPERTY(QString settings READ settings CONSTANT)
    Q_PROPERTY(QString fullScreen READ fullScreen CONSTANT)
    Q_PROPERTY(QString leaveFullScreen READ leaveFullScreen CONSTANT)
    Q_PROPERTY(QString nextPage READ nextPage CONSTANT)
    Q_PROPERTY(QString previousPage READ previousPage CONSTANT)
    Q_PROPERTY(QString nextChapter READ nextChapter CONSTANT)
    Q_PROPERTY(QString previousChapter READ previousChapter CONSTANT)
    Q_PROPERTY(QString cardTitle READ cardTitle CONSTANT)
    Q_PROPERTY(QString closeCard READ closeCard CONSTANT)
    Q_PROPERTY(QString backToSeries READ backToSeries CONSTANT)
    Q_PROPERTY(QString stayHere READ stayHere CONSTANT)
    Q_PROPERTY(QString thisSeries READ thisSeries CONSTANT)
    Q_PROPERTY(QString thisDesk READ thisDesk CONSTANT)
    Q_PROPERTY(QString deskNote READ deskNote CONSTANT)
    /// `[{ value, label }]` for `SettingsChoice`: « Par défaut » is −1, the others are
    /// `Reader.LeftToRight` and its two siblings. No `icon`: these are words alone.
    /// The names of the lines in the volume's panel.
    Q_PROPERTY(QString factSeries READ factSeries CONSTANT)
    Q_PROPERTY(QString factPages READ factPages CONSTANT)
    Q_PROPERTY(QString factChapters READ factChapters CONSTANT)
    Q_PROPERTY(QString factPublished READ factPublished CONSTANT)
    Q_PROPERTY(QString factIsbn READ factIsbn CONSTANT)
    Q_PROPERTY(QString factRead READ factRead CONSTANT)
    /// « Cliquer pour copier », under the ISBN's barcode.
    Q_PROPERTY(QString copyIsbn READ copyIsbn CONSTANT)
    Q_PROPERTY(QVariantList directions READ directions CONSTANT)
    /// The two halves of « Sur cet appareil », and the name of each row under them.
    Q_PROPERTY(QString pagesGroup READ pagesGroup CONSTANT)
    Q_PROPERTY(QString comfortGroup READ comfortGroup CONSTANT)
    Q_PROPERTY(QString layoutName READ layoutName CONSTANT)
    Q_PROPERTY(QString fitName READ fitName CONSTANT)
    Q_PROPERTY(QString backdropName READ backdropName CONSTANT)
    Q_PROPERTY(QString motionName READ motionName CONSTANT)
    /// `[{ value, label, name }]`, `Preferences.Single` and its two siblings. No `icon`: the
    /// mockup approved on 22/10/2026 names each row beside its pills and draws the pills in
    /// words alone, as the directions above them are. `name` is what the pill is called among
    /// the items, in English, so that a test finds it without the picto that used to name it.
    Q_PROPERTY(QVariantList layouts READ layouts CONSTANT)
    /// All three fits, for a book. A strip has no height to fit, and takes `stripFits`.
    Q_PROPERTY(QVariantList fits READ fits CONSTANT)
    Q_PROPERTY(QVariantList stripFits READ stripFits CONSTANT)
    /// `[{ value, label, name }]`, `Preferences.Black` and its two siblings, then
    /// `Preferences.Slide` and its two.
    Q_PROPERTY(QVariantList backdrops READ backdrops CONSTANT)
    Q_PROPERTY(QVariantList motions READ motions CONSTANT)
    /// « Raccourcis », the card's title and its button's bubble.
    Q_PROPERTY(QString shortcuts READ shortcuts CONSTANT)

public:
    explicit ReaderCaptions(QObject *parent = nullptr);

    QString volumePanel() const;
    QString settings() const;
    QString fullScreen() const;
    QString leaveFullScreen() const;
    QString nextPage() const;
    QString previousPage() const;
    QString nextChapter() const;
    QString previousChapter() const;
    QString cardTitle() const;
    QString closeCard() const;
    QString backToSeries() const;
    QString stayHere() const;
    QString thisSeries() const;
    QString thisDesk() const;
    QString deskNote() const;
    QString pagesGroup() const;
    QString comfortGroup() const;
    QString layoutName() const;
    QString fitName() const;
    QString backdropName() const;
    QString motionName() const;
    QString factSeries() const;
    QString factPages() const;
    QString factChapters() const;
    QString factPublished() const;
    QString factIsbn() const;
    QString factRead() const;
    QString copyIsbn() const;
    QVariantList directions() const;
    QVariantList layouts() const;
    QVariantList fits() const;
    QVariantList stripFits() const;
    QVariantList backdrops() const;
    QVariantList motions() const;
    QString shortcuts() const;
    /// The card of the reader's keys, as data: `[{ name, title, rows }]` in reading order,
    /// each row `{ does, keys }`, each of `keys` one thing drawn left to right —
    /// `{ kind: "key", text, wide }`, `{ kind: "key", glyph, wide }`, `{ kind: "sign", text }`
    /// for « + », « / » and « ou », `{ kind: "gesture", glyph }`, `{ kind: "hint", text }`.
    /// Built here so that every word of it is `Words`' and the QML only lays it out.
    ///
    /// For the book open, and not once for every book: in a book read right to left ← and
    /// Ctrl ← go on, and the lines for the arrows say so.
    Q_INVOKABLE QVariantList shortcutGroups(bool rightToLeft) const;

    /// The line under the directions, for what `Reader.declaredDirection` holds (−1 for
    /// nothing). Styled text: the declared way is in `<b>`.
    Q_INVOKABLE QString declares(int declaredDirection) const;

    /// « Tome 12 · Chap.099 : Coup de sifflet ».
    Q_INVOKABLE QString where(const QString &entry, const QString &chapter,
                              const QString &title) const;
    /// « Chap.099 : Coup de sifflet · page 47 », `page` counted from 0 like every page index
    /// in the reader, and said from 1.
    Q_INVOKABLE QString scrubbing(const QString &chapter, const QString &title, int page) const;
    /// « : Coup de sifflet », after a chapter's label drawn apart; nothing when the title
    /// is that label again, and the title alone when there is no label.
    Q_INVOKABLE QString besideLabel(const QString &label, const QString &title) const;
    /// « Chap.107 · fin du tome ».
    Q_INVOKABLE QString volumeEnd(const QString &label, const QString &title) const;
    /// « Tome 12 · Le temps des examens ».
    Q_INVOKABLE QString volumeHead(const QString &entry, const QString &title) const;
    /// « 4 mars 2015 », from the server's « 2015-03-04 ».
    Q_INVOKABLE QString published(const QString &iso) const;
    /// « page 47 / 190 »; `page` from 1.
    Q_INVOKABLE QString readAt(int page, int count, bool finished) const;
    /// « p. 3 » for a chapter beginning at page 2 counted from 0.
    Q_INVOKABLE QString chapterPage(int startPage) const;
    /// « / 190 ».
    Q_INVOKABLE QString outOf(int count) const;
    /// « 200 % » for 2.
    Q_INVOKABLE QString zoom(qreal level) const;
};
