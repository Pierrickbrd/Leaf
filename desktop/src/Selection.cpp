#include "Selection.h"

#include "Words.h"

QString Selection::label() const
{
    return Words::selectionCount(count(), !m_seriesId.isEmpty());
}

QString Selection::readWord() const
{
    return Words::markSelection(true);
}

QString Selection::unreadWord() const
{
    return Words::markSelection(false);
}

QString Selection::clearWord() const
{
    return Words::clearSelection();
}

QString Selection::exportWord() const
{
    return Words::command(Words::Command::Export);
}

void Selection::toggle(const QString &id, const QString &seriesId)
{
    if (id.isEmpty())
        return;
    // Another kind, or another edition's volumes: what was picked cannot share a command
    // with this one, and keeping it would mark volumes nobody can see any more.
    if (!m_ids.isEmpty() && seriesId != m_seriesId)
        m_ids.clear();
    m_seriesId = seriesId;
    if (!m_ids.removeOne(id))
        m_ids.append(id);
    emit changed();
}

void Selection::clear()
{
    if (m_ids.isEmpty())
        return;
    m_ids.clear();
    m_seriesId.clear();
    emit changed();
}
