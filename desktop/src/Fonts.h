#pragma once

// The two families the client draws in, and the four files behind them.
//
// They are embedded rather than looked up: `fc-match sans-serif` answers Noto Sans on this
// machine, where neither Inter nor Barlow Condensed is installed. Without embedding, the
// application draws in whatever Ubuntu has to hand — and in something else again elsewhere.

#include <QString>

namespace Fonts {

/// Registers the four faces. False if any of them did not register — which is the only
/// moment the failure is visible, since a missing family degrades to a fallback in silence.
bool load();

/// « Barlow Condensed » — screen titles at 700, names under covers at 600.
const QString &display();

/// « Inter » — everything else, at 400. The family carries its optical size because the
/// shipped .ttf declares it that way internally: Qt registers it as "Inter 18pt", not
/// "Inter", and resolving the shorter name silently falls back to another face.
const QString &text();

} // namespace Fonts
