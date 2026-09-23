#include "SeriesCaptions.h"

#include "Elsewhere.h"
#include "Entries.h"
#include "Words.h"

#include <QDebug>

SeriesCaptions *SeriesCaptions::create(QQmlEngine *engine, QJSEngine *)
{
    auto *entries = engine->singletonInstance<Entries *>(qmlTypeId("Leaf", 1, 0, "Entries"));
    if (!entries) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Entries singleton — a series page will not "
                              "say why its list is empty");
    }
    auto *elsewhere =
        engine->singletonInstance<Elsewhere *>(qmlTypeId("Leaf", 1, 0, "Elsewhere"));
    if (!elsewhere) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Elsewhere singleton — the universe block "
                              "will be drawn without its heading");
    }
    return new SeriesCaptions(entries, elsewhere);
}

SeriesCaptions::SeriesCaptions(Entries *entries, Elsewhere *elsewhere, QObject *parent)
    : QObject(parent)
    , m_entries(entries)
    , m_elsewhere(elsewhere)
{
    if (m_entries)
        connect(m_entries, &Entries::changed, this, &SeriesCaptions::changed);
    if (m_elsewhere)
        connect(m_elsewhere, &Elsewhere::changed, this, &SeriesCaptions::changed);
}

QString SeriesCaptions::sameWorkHeading() const
{
    return Words::sameWorkOtherwise();
}

QString SeriesCaptions::universeHeading() const
{
    return Words::inTheUniverse();
}

QString SeriesCaptions::outsideHeading() const
{
    return Words::outsideTheOrder();
}

QString SeriesCaptions::universeLine() const
{
    if (m_elsewhere == nullptr)
        return {};
    // A walked way names its own steps; counting series beside them would count something
    // else, so the count is there only when the block is the flat list.
    const bool walking = !m_elsewhere->chosenOrder().isEmpty();
    return Words::universeLine(m_elsewhere->universe(),
                               walking ? 0 : int(m_elsewhere->tiles().size()));
}

QString SeriesCaptions::outsideLine() const
{
    return m_elsewhere == nullptr ? QString()
                                  : Words::seriesCount(int(m_elsewhere->outside().size()));
}

QString SeriesCaptions::hereToo(const QString &detail) const
{
    return Words::hereToo(detail);
}

QString SeriesCaptions::volumesTab() const
{
    return Words::tab(Words::Tab::Volumes);
}

QString SeriesCaptions::descriptionTab() const
{
    return Words::tab(Words::Tab::Description);
}

QString SeriesCaptions::elsewhereTab() const
{
    return Words::tab(Words::Tab::Elsewhere);
}

QString SeriesCaptions::neverRead() const
{
    return Words::neverRead();
}

QString SeriesCaptions::inThisLibrary() const
{
    return Words::inThisLibrary();
}

QString SeriesCaptions::volumesAxis() const
{
    return Words::volumesAxis();
}

QString SeriesCaptions::nothingFound() const
{
    return narrowedToNothing() ? Words::noVolumeByThatName() : QString();
}

bool SeriesCaptions::narrowedToNothing() const
{
    return m_entries && m_entries->narrowedToNothing();
}
