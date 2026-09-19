#pragma once

// What the search says, as opposed to what it found.
//
// These twenty-two lines of French used to hang off `Search` itself, beside the model of
// file hits, under a comment that already said what they were: "the bar's words live on its
// state object rather than as untested French in QML". They are the bar's words, and the
// bar's words are not a list model — the class carried fifty-six methods, half of them
// captions, and neither half was easy to find inside the other.
//
// Nothing here decides anything. `Words` holds the French and the typography; `Search` and
// `Shelf` hold the state; this binds the two and re-announces itself when either moves, so
// a QML binding on a caption is refreshed by the same change that refreshed the count it
// quotes.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QJSEngine;
class QQmlEngine;
class Search;
class Shelf;

class Captions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The heading over the file rows, and the way to the rest of them.
    Q_PROPERTY(QString heading READ heading NOTIFY changed)
    Q_PROPERTY(QString moreLabel READ moreLabel NOTIFY changed)
    Q_PROPERTY(QString overviewLabel READ overviewLabel CONSTANT)
    Q_PROPERTY(QString seriesHeading READ seriesHeading NOTIFY changed)
    Q_PROPERTY(QString filesHeading READ filesHeading NOTIFY changed)
    Q_PROPERTY(QString allSeriesLabel READ allSeriesLabel NOTIFY changed)
    Q_PROPERTY(QString allFilesLabel READ allFilesLabel NOTIFY changed)
    /// « aucun résultat dans manga · 3 sans les filtres », when a lit pill is what emptied
    /// the screen. Without it the screen just looks broken.
    Q_PROPERTY(QString outsideFilters READ outsideFilters NOTIFY changed)
    /// « Vouliez-vous dire Tsugumi Ōba ? » — a guess, phrased as one.
    Q_PROPERTY(QString suggestion READ suggestion NOTIFY changed)

    /// The bar's own words, which never change.
    Q_PROPERTY(QString placeholder READ placeholder CONSTANT)
    Q_PROPERTY(QString shortPlaceholder READ shortPlaceholder CONSTANT)
    Q_PROPERTY(QString clearLabel READ clearLabel CONSTANT)
    Q_PROPERTY(QString filterLabel READ filterLabel CONSTANT)
    Q_PROPERTY(QString settingsLabel READ settingsLabel CONSTANT)
    Q_PROPERTY(QString noSeriesLabel READ noSeriesLabel CONSTANT)
    Q_PROPERTY(QString clearFiltersLabel READ clearFiltersLabel CONSTANT)
    Q_PROPERTY(QString nothingToFilterLabel READ nothingToFilterLabel CONSTANT)
    Q_PROPERTY(QString noValueByThatNameLabel READ noValueByThatNameLabel CONSTANT)
    Q_PROPERTY(QString sortLabel READ sortLabel NOTIFY changed)
    Q_PROPERTY(QVariantList sortOptions READ sortOptions CONSTANT)
    Q_PROPERTY(QString sortValue READ sortValue NOTIFY changed)

public:
    explicit Captions(Search *search, Shelf *shelf, QObject *parent = nullptr);

    static Captions *create(QQmlEngine *engine, QJSEngine *);

    QString heading() const;
    QString moreLabel() const;
    QString overviewLabel() const;
    QString seriesHeading() const;
    QString filesHeading() const;
    QString allSeriesLabel() const;
    QString allFilesLabel() const;
    QString outsideFilters() const;
    QString suggestion() const;

    QString placeholder() const;
    QString shortPlaceholder() const;
    QString clearLabel() const;
    QString filterLabel() const;
    QString settingsLabel() const;
    QString noSeriesLabel() const;
    QString clearFiltersLabel() const;
    QString nothingToFilterLabel() const;
    QString noValueByThatNameLabel() const;
    /// « Chercher dans auteur… » — the placeholder of one axis's own field.
    Q_INVOKABLE QString searchWithin(const QString &axisTitle) const;

    QString sortLabel() const;
    QString sortValue() const;
    QVariantList sortOptions() const;

signals:
    void changed();

private:
    /// Every value lit, worded, for the sentence that says what the filters are hiding.
    QStringList activeLabels() const;
    QStringList narrowedBy(const QString &axis) const;

    Search *m_search;
    Shelf *m_shelf;
};
