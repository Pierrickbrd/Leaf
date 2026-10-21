#include "Widths.h"

#include "Words.h"

#include <algorithm>

Widths::Widths(QObject *parent) : QObject(parent), m_scale(new Scale(this, this)) {}

Scale::Scale(const Widths *widths, QObject *parent) : QObject(parent), m_widths(widths) {}

int Scale::size(qreal drawn) const
{
    return sizeFor(drawn, m_widths->controls());
}

int Scale::icon(qreal drawn) const
{
    return iconFor(drawn, m_widths->controls());
}

int Scale::column(qreal drawn) const
{
    return columnFor(drawn, m_widths->controls());
}

int Scale::room(qreal drawn) const
{
    return roomFor(drawn, m_widths->controls());
}

qreal Scale::letters(qreal drawn) const
{
    return lettersFor(drawn, m_widths->controls());
}

int Scale::caption(int drawn) const
{
    return captionFor(drawn, m_widths->controls());
}

int Scale::body(int drawn) const
{
    return bodyFor(drawn, m_widths->controls());
}

int Scale::title(int drawn) const
{
    return titleFor(drawn, m_widths->controls());
}

int Scale::sizeFor(qreal drawn, qreal factor)
{
    return qRound(drawn * factor);
}

// Half the window's growth: at 1920, a quarter larger for a length and an eighth for this.
// Measured on 23/10/2026: at the full rate the keys on the shortcuts card stood larger than
// the words beside them once those stopped growing.
int Scale::iconFor(qreal drawn, qreal factor)
{
    return sizeFor(drawn, factor > 1.0 ? 1.0 + (factor - 1.0) / 2 : factor);
}

// A card of words that never grow, as wide as a length would make it, left a band of empty
// paper down its right side: it grows as icons do, and its paddings, which are lengths, take
// what it gains.
int Scale::columnFor(qreal drawn, qreal factor)
{
    return iconFor(drawn, factor);
}

// Twice the window's growth: at 1920 a gap of 4 between the dock's buttons became 5, which
// nobody sees, beside words that no longer took any of the width the dock had gained.
int Scale::roomFor(qreal drawn, qreal factor)
{
    return sizeFor(drawn, factor > 1.0 ? 1.0 + 2 * (factor - 1.0) : factor);
}

qreal Scale::lettersFor(qreal drawn, qreal factor)
{
    return drawn * std::min(factor, 1.0);
}

// Words never grow past the size they were drawn at: on 23/10/2026, at 1920, every card's
// words had grown by a quarter and read too big — see `Scale`. Under 1 they shrink.
//
// A floor never lifts a word above the size it was drawn at: a caption drawn at 10 stays 10
// rather than becoming 11 at the very window it was drawn for.
int Scale::captionFor(int drawn, qreal factor)
{
    return std::max(std::min(drawn, CaptionFloor), sizeFor(drawn, std::min(factor, 1.0)));
}

int Scale::bodyFor(int drawn, qreal factor)
{
    return std::max(std::min(drawn, BodyFloor), sizeFor(drawn, std::min(factor, 1.0)));
}

// Under 1 a title gives twice what the rest gives: at 0.85, 0.7. It is the largest thing on
// its card and the one that can lose most before it stops being read.
int Scale::titleFor(int drawn, qreal factor)
{
    const qreal steeper = factor < 1.0 ? 1.0 - 2 * (1.0 - factor) : 1.0;
    return std::max(std::min(drawn, TitleFloor), sizeFor(drawn, steeper));
}

int Widths::window() const
{
    return m_window;
}

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

qreal Widths::controlsFor(int width)
{
    // Drawn for a window of 1280, and grown or shrunk from it. Tried by hand on 22/10/2026 in
    // full screen on a display of 2560 at a scale of 1.333 — a window of 1920 — the reader's
    // dock was a strip of specks at its drawn size, and too large for the page at half as
    // large again: a quarter more there, half more at 2560 and no further. The same day, on
    // a small window, its words kept their drawn size beside a page that had shrunk round
    // them: down to 0.85 at 900, and no further, where 11 pixels become 9. Linear and not
    // stepped on both sides, so a window dragged wider never jumps.
    if (width >= 1280)
        return std::min(1.0 + 0.5 * (width - 1280) / 1280.0, 1.5);
    return std::max(0.85 + 0.15 * (width - 900) / 380.0, 0.85);
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

    const Band band = bandFor(width);
    if (band == Narrow) {
        return 2;
    }
    if (band == Medium) {
        return std::clamp(fit, 4, 5);
    }
    return std::min(fit, MaximumShelfColumns);
}

int Widths::shelfColumns() const
{
    return shelfColumnsFor(m_window);
}

int Widths::shelfMargin() const
{
    return ShelfMargin;
}

int Widths::shelfGap() const
{
    return ShelfGap;
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

int Widths::markOn(qreal coverWidth)
{
    return qBound(22, qRound(coverWidth / 8), 30);
}
