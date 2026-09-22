#pragma once

// What one card of the import queue, and one line of its tree, say about themselves.
//
// The same split `Captions.h` made on `Search` and `ImportCaptions.h` made on `Imports`, one
// floor further down. `ImportCaptions` holds what the *dialog* says — a title, six buttons,
// the two file pickers — and every one of those is a constant property read once when the
// screen is built. These are not that: they are answers about one thing being imported, asked
// per row and per node, and the class carried both halves under one name until Sonar's
// `cpp:S1448` counted forty-one methods on it.
//
// The line is where they belong together. `ImportRow.qml` asks what stage a card is at and
// how far it has got, `ImportNode.qml` asks what one node of the tree is and what becomes of
// it, `LevelMark.qml` asks which word and which icon a level wears — three files, one
// subject, and not one of them needs anything the dialog holds. Which is why this one takes
// no `Imports` and announces nothing: every answer here is a function of its arguments, and
// a binding on it is refreshed by the model role that changed, not by a signal from here.
//
// Nothing here decides anything. `Words` holds the French and the typography; `Imports` holds
// the queue; this binds the two.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class CardCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    explicit CardCaptions(QObject *parent = nullptr);

    /// One word per stage. Invokable rather than a property because a row asks it about
    /// itself.
    Q_INVOKABLE QString stageLabel(int stage) const;
    /// The stage and how far it has got, joined here and never in a `.qml` file.
    Q_INVOKABLE QString stageAnd(int stage, qint64 sent, qint64 whole) const;
    /// Everything one line of the tree says about itself, under one separator.
    Q_INVOKABLE QString nodeLine(int level, const QString &state, const QString &holds) const;
    /// One concern of an archive, with the mark that makes it one of a list.
    Q_INVOKABLE QString concern(const QString &said) const;
    Q_INVOKABLE QString tryingAgainIn(int seconds) const;

    /// The word for a level of the tree, and the name of the icon that marks it — both from
    /// `Words`, so `LevelMark` composes the path and nothing French leaves it.
    Q_INVOKABLE QString levelLabel(int level) const;
    Q_INVOKABLE QString levelIcon(int level) const;

    /// « créera la série « Elfes » ». Said here rather than in the QML for the reason
    /// `Words.h` gives: the elision in front of a vowel is not a rule QML could apply.
    Q_INVOKABLE QString willCreateLabel(const QString &kind, const QString &name) const;

    /// « « Elfes » est déjà dans « Mangas » — le ranger ici l'y déplacera ». `from` is the
    /// whole path the server sent; only the folder it sits in is shown, because a reader
    /// recognises « Mangas » and reads past `/srv/leaf/library/Mangas`.
    Q_INVOKABLE QString alreadyElsewhereLabel(const QString &name, const QString &from) const;
};
