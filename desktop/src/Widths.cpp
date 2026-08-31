#include "Widths.h"

#include "Words.h"

Widths::Widths(QObject *parent) : QObject(parent) {}

QString Widths::bandLabel() const
{
    return Words::band(band());
}

Widths::Band Widths::bandFor(int width)
{
    if (width >= 1100)
        return Band::Wide;
    if (width >= 600)
        return Band::Medium;
    return Band::Narrow;
}

void Widths::setWindow(int width)
{
    if (m_window == width)
        return;
    const Band before = bandFor(m_window);
    m_window = width;
    emit windowChanged();
    if (bandFor(m_window) != before)
        emit changed();
}
