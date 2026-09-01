#pragma once

// What used to be four paragraphs of comment inside `main`, because the order they describe
// cannot be seen from the outside and a test cannot reach a local in a function nobody calls.
//
// Two decisions, and both are about order rather than about what each step does on its own:
//
//   · the fonts have to register before `engine.load(...)` runs, because a label asking for
//     "Barlow Condensed" before the family is in the font database draws in Noto Sans and
//     nothing says why;
//
//   · the `Theme` singleton has to be resolved by `qmlTypeId` *after* `engine.load(...)`, not
//     before. `qt_add_qml_module`'s registration for "Leaf" is lazy: it is recorded, not run,
//     until something actually imports "Leaf". A lookup made first finds nothing and, worse,
//     leaves the module looking checked-and-empty to every import that follows it — so the
//     ordering is not a preference, it is the only order that can work at all.
//
// `main` held both of these once, the way the server's `main` held what is now `boot.rs`:
// a hundred and seventy lines nothing could reach, because `tests/opens.sh` drives the real
// binary under `timeout` and expects it killed, and gcov writes nothing on a kill. Putting the
// decisions here is the same repair — see `server/src/boot.rs` — so that `crosses-the-seam`
// can call `run()` the way `main` does and observe what happened.

#include <QQmlApplicationEngine>

class QGuiApplication;

namespace Boot {

/// Loads the four fonts, loads `Main.qml` into `engine`, and — once that load has made "Leaf"
/// a real, resolvable module — resolves the `Theme` singleton and tells it to follow the
/// system palette. `application` is the connection context for a QML object-creation failure,
/// nothing more; it is not read.
///
/// Neither failure throws or aborts: a window that opens with the wrong fonts or the wrong
/// palette still opens, and nothing else would notice, so each is a warning rather than a
/// silent fallback.
void run(QQmlApplicationEngine &engine, QGuiApplication &application);

} // namespace Boot
