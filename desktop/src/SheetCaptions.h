#pragma once

// The words of a card's sheet sections that open a window of their own: credits, arcs,
// chapters and reading orders, with the order editor.
//
// Split off `ImportCaptions` when that class reached a hundred and three methods — Sonar's
// `cpp:S1448` — by holding every step of the dialog at once; `ImportCaptions.h` says why. The
// constant words come as two maps, as `Editing.words` does: a property each was a
// dozen more methods apiece, which is how the class they came from got where it did.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

class SheetCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The words of a sheet's credits, arcs, chapters and orders, one map for the lot.
    Q_PROPERTY(QVariantMap sheetWords READ sheetWords CONSTANT)
    /// The order editor's words, one map for the lot as `sheetWords`.
    Q_PROPERTY(QVariantMap orderWords READ orderWords CONSTANT)

public:
    explicit SheetCaptions(QObject *parent = nullptr);

    QVariantMap sheetWords() const;
    QVariantMap orderWords() const;

    Q_INVOKABLE QString arcsEditLabel(int count) const;
    /// « 3 sagas · 9 arcs » beside the title of the window of a series' arcs.
    Q_INVOKABLE QString arcsCountLabel(int sagas, int arcs) const;
    Q_INVOKABLE QString chaptersEditLabel(int count) const;
    Q_INVOKABLE QString chaptersWindowTitle(const QString &volume) const;
    Q_INVOKABLE QString chaptersCountLabel(int chapters, int pages) const;
    /// « Étape 2 », « 7 étapes », « Si « X » change, « Y » suit. » and what names a step's ×
    /// and its handle: the words of the editor that carry something of the draft.
    Q_INVOKABLE QString stepNumbered(int step) const;
    Q_INVOKABLE QString flatSteps(int steps) const;
    Q_INVOKABLE QString followsIt(const QString &included, const QString &order) const;
    Q_INVOKABLE QString removeStep(const QString &step) const;
};
