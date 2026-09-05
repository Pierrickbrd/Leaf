#include "Widths.h"

#include "Words.h"

#include <algorithm>

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

int Widths::shelfColumnsFor(int width)
{
    using enum Widths::Band;

    // Sixteen pixels of paper on both sides and fourteen between covers are the measured
    // mock-up, rounded to whole device-independent pixels. Count a cover only when its
    // 140-pixel floor and every gutter before it fit. Written without `available + gap`, so
    // even an absurd INT_MAX-sized window cannot overflow before the division.
    const int available = std::max(0, std::max(0, width) - 2 * ShelfMargin);
    const int fit = available < MinimumCoverWidth
        ? 1
        : 1 + (available - MinimumCoverWidth) / (MinimumCoverWidth + ShelfGap);

    switch (bandFor(width)) {
    case Narrow:
        return 2;
    case Medium:
        return std::clamp(fit, 4, 5);
    case Wide:
        return fit;
    }
    return 2;
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
