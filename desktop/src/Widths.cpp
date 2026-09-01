#include "Widths.h"

#include "Words.h"

Widths::Widths(QObject *parent) : QObject(parent) {}

QString Widths::bandLabel() const
{
    return Words::band(band());
}

Widths::Band Widths::bandFor(int width)
{
    using enum Widths::Band;

    if (width >= 1100)
        return Wide;
    if (width >= 600)
        return Medium;
    return Narrow;
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
