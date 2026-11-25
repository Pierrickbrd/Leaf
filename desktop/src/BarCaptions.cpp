#include "BarCaptions.h"

#include "Api.h"
#include "Navigation.h"
#include "Shelf.h"
#include "Words.h"

#include <QDebug>
#include <QQmlEngine>
#include <QVariantMap>

using namespace Qt::StringLiterals;

BarCaptions *BarCaptions::create(QQmlEngine *engine, QJSEngine *)
{
    auto *shelf = engine->singletonInstance<Shelf *>(qmlTypeId("Leaf", 1, 0, "Shelf"));
    if (!shelf) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Shelf singleton — the bar's sort will "
                              "say the default order");
    }
    return new BarCaptions(shelf);
}

BarCaptions::BarCaptions(Shelf *shelf, QObject *parent)
    : QObject(parent)
    , m_shelf(shelf)
{
    // The sort's label quotes the shelf's order, which a click on the menu changes.
    if (m_shelf)
        connect(m_shelf, &Shelf::changed, this, &BarCaptions::changed);
}

QString BarCaptions::placeholder() const
{
    return Words::searchHint();
}

QString BarCaptions::shortPlaceholder() const
{
    return Words::searchHintShort();
}

QString BarCaptions::clearLabel() const
{
    return Words::clearTheSearch();
}

QString BarCaptions::filterLabel() const
{
    return Words::filter();
}

QString BarCaptions::settingsLabel() const
{
    return Words::destination(Navigation::Destination::Settings);
}

QString BarCaptions::clearFiltersLabel() const
{
    return Words::clearEveryFilter();
}

QString BarCaptions::nothingToFilterLabel() const
{
    return Words::nothingToFilter();
}

QString BarCaptions::noValueByThatNameLabel() const
{
    return Words::noValueByThatName();
}

QString BarCaptions::quotedAxis(const QString &axisTitle) const
{
    return Words::quotedAxis(axisTitle);
}

QString BarCaptions::searchWithin(const QString &axisTitle) const
{
    return Words::searchWithin(axisTitle);
}

QString BarCaptions::sortValue() const
{
    const Api::Sort order = m_shelf ? Api::sort(m_shelf->sort()) : Api::Sort::Name;
    return Words::sortValue(order, m_shelf && m_shelf->sortReversed());
}

QString BarCaptions::sortLabel() const
{
    const Api::Sort order = m_shelf ? Api::sort(m_shelf->sort()) : Api::Sort::Name;
    return Words::labelled(u"Trier"_s,
                           Words::sortValue(order, m_shelf && m_shelf->sortReversed()));
}

QVariantList BarCaptions::sortOptions() const
{
    QVariantList options;
    using enum Api::Sort;

    for (const Api::Sort order : {Name, Added, Volumes, Read}) {
        options << QVariantMap{{u"value"_s, Api::spell(order)},
                               {u"label"_s, Words::sortOrder(order)}};
    }
    return options;
}
