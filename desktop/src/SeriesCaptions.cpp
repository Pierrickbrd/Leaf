#include "SeriesCaptions.h"

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
    return new SeriesCaptions(entries);
}

SeriesCaptions::SeriesCaptions(Entries *entries, QObject *parent)
    : QObject(parent)
    , m_entries(entries)
{
    if (m_entries)
        connect(m_entries, &Entries::changed, this, &SeriesCaptions::changed);
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
