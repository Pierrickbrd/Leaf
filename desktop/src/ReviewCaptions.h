#pragma once

// What the second step of the import dialog says around a card: the list and the tree, the
// sheet's fields, the readings and their clues, and the splice of a reimported volume.
//
// Split off `ImportCaptions` when that class reached a hundred and three methods — Sonar's
// `cpp:S1448` — by holding every step of the dialog at once. `ImportCaptions.h` says why one
// class per step. The words of the sheet's sections that open a window of their own —
// credits, arcs, chapters, orders — are `SheetCaptions`, because they are one map apiece and
// a dozen invokables that would have put this class back over the line.
//
// Nothing here is bound to the review's state: what a word quotes arrives as an argument.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class ReviewCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString missingLabel READ missingLabel CONSTANT)
    /// « obligatoire », after the green asterisk at the right of the sheet's bar.
    Q_PROPERTY(QString requiredLegend READ requiredLegend CONSTANT)
    Q_PROPERTY(QString editLabel READ editLabel CONSTANT)
    /// « Chercher une série, un auteur… » — over the list of the frame.
    Q_PROPERTY(QString searchLabel READ searchLabel CONSTANT)
    /// « Revoir la suivante », under the tree.
    Q_PROPERTY(QString reviewNextLabel READ reviewNextLabel CONSTANT)
    /// « Tout », over the list.
    Q_PROPERTY(QString tickAllLabel READ tickAllLabel CONSTANT)
    /// What the grip of a volume says to a reader who cannot see it.
    Q_PROPERTY(QString moveVolumeLabel READ moveVolumeLabel CONSTANT)
    /// Over the files a reading of a folder writes, on its sheet.
    Q_PROPERTY(QString writesLabel READ writesLabel CONSTANT)
    /// Under the tree while the role menu is open: what its greyed lines are.
    Q_PROPERTY(QString greyedRolesLabel READ greyedRolesLabel CONSTANT)
    /// The marks before a clue: for the reading, and against it.
    Q_PROPERTY(QString clueForMark READ clueForMark CONSTANT)
    Q_PROPERTY(QString clueAgainstMark READ clueAgainstMark CONSTANT)

public:
    explicit ReviewCaptions(QObject *parent = nullptr);

    QString missingLabel() const;
    QString requiredLegend() const;
    QString editLabel() const;
    QString searchLabel() const;
    QString reviewNextLabel() const;
    QString tickAllLabel() const;
    QString moveVolumeLabel() const;
    QString writesLabel() const;
    QString greyedRolesLabel() const;
    QString clueForMark() const;
    QString clueAgainstMark() const;

    Q_INVOKABLE QString keepLabel() const;
    Q_INVOKABLE QString newValueLabel() const;
    /// « + Étiquette » — what the list field `key` offers to add.
    Q_INVOKABLE QString addWordLabel(const QString &key) const;
    /// « Étiquettes : retirer « X » » — the × of one word, named with its field.
    Q_INVOKABLE QString removeWordLabel(const QString &field, const QString &word) const;
    /// The two sides of a reimported volume's splice, and the range each keeps.
    Q_INVOKABLE QString spliceCurrentSide() const;
    Q_INVOKABLE QString spliceSourceSide() const;
    Q_INVOKABLE QString spliceFromLabel() const;
    Q_INVOKABLE QString spliceToLabel() const;
    Q_INVOKABLE QString pagesRange(int from, int to) const;
};
