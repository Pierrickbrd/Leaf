#include "Widths.h"

#include "Words.h"

#include <algorithm>
#include <cmath>

Widths::Widths(QObject *parent)
    : QObject(parent), m_scale(new Scale(this, this)), m_space(new Space(this, this))
{
    // `scale` says it changed whenever anything it reads did, so that one NOTIFY reaches
    // every `Widths.scale.…` binding for a window dragged wider and for a text size changed
    // in the desktop's settings alike.
    connect(this, &Widths::windowChanged, this, &Widths::scaleChanged);
    connect(this, &Widths::systemTextChanged, this, &Widths::scaleChanged);
    connect(this, &Widths::scaleChanged, m_space, &Space::changed);
}

Scale::Scale(const Widths *widths, QObject *parent) : QObject(parent), m_widths(widths) {}

int Scale::size(qreal drawn) const
{
    return sizeFor(drawn, m_widths->controls());
}

int Scale::fit(qreal drawn) const
{
    return fitFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::icon(qreal drawn) const
{
    return iconFor(drawn, m_widths->controls());
}

int Scale::column(qreal drawn) const
{
    return columnFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::room(qreal drawn) const
{
    return roomFor(drawn, m_widths->controls());
}

qreal Scale::letters(qreal drawn) const
{
    return lettersFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::caption(int drawn) const
{
    return captionFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::body(int drawn) const
{
    return bodyFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::title(int drawn) const
{
    return titleFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::display(int drawn) const
{
    return displayFor(drawn, m_widths->controls(), m_widths->systemText());
}

int Scale::sizeFor(qreal drawn, qreal factor)
{
    return qRound(drawn * factor);
}

namespace {

/// Half a length's growth above 1, all of it under.
qreal halfAbove(qreal factor)
{
    return factor > 1.0 ? 1.0 + (factor - 1.0) / 2 : factor;
}

/// Twice what the rest gives under 1, nothing above.
qreal steeperBelow(qreal factor)
{
    return factor < 1.0 ? 1.0 - 2 * (1.0 - factor) : 1.0;
}

/// A word at `drawn × grown × text`, never under its floor and never over `most × text`.
///
/// The floor applies after the desktop's text size and is never scaled by it: someone who
/// asked for text a quarter larger keeps the window's 13 as the least a body can shrink to,
/// not 16. Nor does a floor ever lift a word above what it is at the window it was drawn for:
/// a caption drawn at 10 stays 10 rather than becoming 11, and at a text size of 0.8, asked
/// for on purpose, a body of 14 is 11 rather than lifted back to 13.
int aWord(int drawn, qreal grown, qreal text, int floor, qreal most)
{
    const qreal ceiling = drawn * most * text;
    const qreal least = std::min(qreal(floor), drawn * text);
    return qRound(std::clamp(drawn * grown * text, least, ceiling));
}

} // namespace

// Half the window's growth: at 1920, a quarter larger for a length and an eighth for this.
// Measured on 23/10/2026: at the full rate the keys on the shortcuts card stood larger than
// the words beside them once those stopped growing.
int Scale::iconFor(qreal drawn, qreal factor)
{
    return sizeFor(drawn, halfAbove(factor));
}

// A card of words that never grow, as wide as a length would make it, left a band of empty
// paper down its right side: it grows as icons do, and its paddings, which are lengths, take
// what it gains. Its words follow the desktop's text size, so it does too: a card of 380 round
// words half as large again would wrap every line in two.
int Scale::columnFor(qreal drawn, qreal factor, qreal text)
{
    return qRound(drawn * halfAbove(factor) * text);
}

// Twice the window's growth: at 1920 a gap of 4 between the dock's buttons became 5, which
// nobody sees, beside words that no longer took any of the width the dock had gained.
int Scale::roomFor(qreal drawn, qreal factor)
{
    return sizeFor(drawn, factor > 1.0 ? 1.0 + 2 * (factor - 1.0) : factor);
}

// The measure of the paper round a word, by the word's own law: the pills of the
// reader's card stood 45 high round 14 pixels of text at 1920, 3.2 times the word where the
// tabs above the shelf stand 2.4 — the box had taken the window's rate and the word had not.
int Scale::fitFor(qreal drawn, qreal factor, qreal text)
{
    return qRound(drawn * std::min(factor, 1.0) * text);
}

qreal Scale::lettersFor(qreal drawn, qreal factor, qreal text)
{
    return drawn * std::min(factor, 1.0) * text;
}

// Words never grow past the size they were drawn at: on 23/10/2026, at 1920, every card's
// words had grown by a quarter and read too big — see `Scale`. Under 1 they shrink.
int Scale::captionFor(int drawn, qreal factor, qreal text)
{
    return aWord(drawn, std::min(factor, 1.0), text, CaptionFloor, 1.0);
}

int Scale::bodyFor(int drawn, qreal factor, qreal text)
{
    return aWord(drawn, std::min(factor, 1.0), text, BodyFloor, 1.0);
}

// Under 1 a title gives twice what the rest gives: at 0.85, 0.7. It is the largest thing on
// its card and the one that can lose most before it stops being read.
int Scale::titleFor(int drawn, qreal factor, qreal text)
{
    return aWord(drawn, steeperBelow(factor), text, TitleFloor, 1.0);
}

// The one word that grows: under 1 as a title, above it at an icon's rate — 1.125 at 1920,
// 1.25 at 2560 — and never past a quarter more, whatever a larger screen would make of it.
int Scale::displayFor(int drawn, qreal factor, qreal text)
{
    const qreal grown = factor < 1.0 ? steeperBelow(factor) : halfAbove(factor);
    return aWord(drawn, grown, text, TitleFloor, DisplayCeiling);
}

Space::Space(const Widths *widths, QObject *parent) : QObject(parent), m_widths(widths) {}

// The gaps between groups take what a wider window gives, at `room`'s rate; the paddings
// inside a surface grow with the surface, at `size`'s. The column holds words, so it is a
// `column`: it follows the desktop's text size as the words in it do.
int Space::gutter() const
{
    return Scale::roomFor(Gutter, m_widths->controls());
}

int Space::top() const
{
    return Scale::roomFor(Top, m_widths->controls());
}

int Space::section() const
{
    return Scale::roomFor(Section, m_widths->controls());
}

int Space::block() const
{
    return Scale::roomFor(Block, m_widths->controls());
}

int Space::heading() const
{
    return Scale::roomFor(Heading, m_widths->controls());
}

int Space::card() const
{
    return Scale::sizeFor(Card, m_widths->controls());
}

int Space::compact() const
{
    return Scale::sizeFor(Compact, m_widths->controls());
}

int Space::row() const
{
    return Scale::roomFor(Row, m_widths->controls());
}

int Space::label() const
{
    return Scale::roomFor(Label, m_widths->controls());
}

int Space::pill() const
{
    return Scale::roomFor(Pill, m_widths->controls());
}

int Space::tight() const
{
    return Scale::roomFor(Tight, m_widths->controls());
}

int Space::column() const
{
    return Scale::columnFor(Column, m_widths->controls(), m_widths->systemText());
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

qreal Widths::textFor(qreal factor)
{
    // Zero, a negative or a NaN is a portal that answered nonsense, not a person asking for
    // no text at all.
    if (!std::isfinite(factor) || factor <= 0)
        return 1.0;
    return std::clamp(factor, LeastText, MostText);
}

int Widths::shelfColumnsFor(int width, int margin, int gap)
{
    using enum Widths::Band;

    // Sixteen pixels of paper on both sides and fourteen between covers are the measured
    // mock-up, rounded to whole device-independent pixels; the window grows them as room.
    // Count a cover only when its 140-pixel floor and every gutter before it fit. Written
    // without `available + gap`, so even an absurd INT_MAX-sized window cannot overflow
    // before the division.
    const int available = std::max(0, std::max(0, width) - 2 * std::max(0, margin));
    const int fit = available < MinimumCoverWidth
        ? 1
        : 1 + (available - MinimumCoverWidth) / (MinimumCoverWidth + std::max(0, gap));

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
    return shelfColumnsFor(m_window, shelfMargin(), shelfGap());
}

int Widths::shelfMargin() const
{
    return Scale::roomFor(ShelfMargin, controls());
}

int Widths::shelfGap() const
{
    return Scale::roomFor(ShelfGap, controls());
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

void Widths::setHeight(int height)
{
    if (m_height == height)
        return;
    m_height = height;
    emit heightChanged();
}

void Widths::follow(SystemText *source)
{
    disconnect(m_following);
    m_source = source;
    if (source)
        m_following = connect(source, &SystemText::changed, this, &Widths::readText);
    readText();
}

void Widths::readText()
{
    const qreal text = textFor(m_source ? m_source->factor() : 1.0);
    if (qFuzzyCompare(text, m_systemText))
        return;
    m_systemText = text;
    emit systemTextChanged();
}

int Widths::markOn(qreal coverWidth)
{
    return qBound(22, qRound(coverWidth / 8), 30);
}
