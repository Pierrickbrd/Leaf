#pragma once

// What the bar and its filter panel wear, whatever the search found: the field's placeholder,
// its buttons, the sort, and the filter panel's own words.
//
// Split off `Captions` when that class reached thirty-nine methods — Sonar's `cpp:S1448` —
// by carrying both what a search found and what the bar says around it. `Captions` keeps the
// words that quote a result; these quote nothing but the shelf's order, which is why this one
// listens to `Shelf` alone.
//
// Nothing here decides anything. `Words` holds the French and the typography; `Shelf` holds
// the order; this re-announces itself when the shelf moves, so a binding on `sortLabel` is
// refreshed by the same change that re-sorted the grid.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

class QJSEngine;
class QQmlEngine;
class Shelf;

class BarCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The bar's own words, which never change.
    Q_PROPERTY(QString placeholder READ placeholder CONSTANT)
    Q_PROPERTY(QString shortPlaceholder READ shortPlaceholder CONSTANT)
    Q_PROPERTY(QString clearLabel READ clearLabel CONSTANT)
    Q_PROPERTY(QString filterLabel READ filterLabel CONSTANT)
    Q_PROPERTY(QString settingsLabel READ settingsLabel CONSTANT)
    Q_PROPERTY(QString clearFiltersLabel READ clearFiltersLabel CONSTANT)
    Q_PROPERTY(QString nothingToFilterLabel READ nothingToFilterLabel CONSTANT)
    Q_PROPERTY(QString noValueByThatNameLabel READ noValueByThatNameLabel CONSTANT)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY changed)
    Q_PROPERTY(QVariantList sortOptions READ sortOptions CONSTANT)
    Q_PROPERTY(QString sortValue READ sortValue NOTIFY changed)

public:
    explicit BarCaptions(Shelf *shelf, QObject *parent = nullptr);

    static BarCaptions *create(QQmlEngine *engine, QJSEngine *);

    QString placeholder() const;
    QString shortPlaceholder() const;
    QString clearLabel() const;
    QString filterLabel() const;
    QString settingsLabel() const;
    QString clearFiltersLabel() const;
    QString nothingToFilterLabel() const;
    QString noValueByThatNameLabel() const;
    /// « Chercher dans auteur… » — the placeholder of one axis's own field.
    Q_INVOKABLE QString quotedAxis(const QString &axisTitle) const;
    Q_INVOKABLE QString searchWithin(const QString &axisTitle) const;

    QString sortLabel() const;
    QString sortValue() const;
    QVariantList sortOptions() const;

signals:
    void changed();

private:
    Shelf *m_shelf;
};
