#pragma once

// How long a field waits before acting on what was typed.
//
// One number in one file, because it is one decision reached twice: the shelf's search asks
// the server a question, the volumes' search rebuilds a grid, and neither should do it once
// per key. Written in both places it would drift, and two fields that settle at different
// speeds read as two fields that work differently.
//
// Long enough to swallow a burst of keystrokes, short enough that a pause between two words
// is not felt as a stall. Two hundred milliseconds is the usual answer, and it is what a fast
// typist leaves between letters.
//
// It is not the two hundred milliseconds `SeriesView` waits before drawing a skeleton. That
// one measures how long an answer may take before its absence is worth saying, which is a
// different decision that happens to have landed on the same number — folding them together
// would mean changing one of them the day the other moved.

namespace Settling {

inline constexpr int Milliseconds = 200;

} // namespace Settling
