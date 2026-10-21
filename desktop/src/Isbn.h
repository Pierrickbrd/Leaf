#pragma once

// An ISBN read as the EAN-13 printed on the back of the book: its bars, and the number shown
// beside them.
//
// The owner chose on 22/10/2026 to show the ISBN of the open volume as a barcode on a key at
// the foot of its panel, rather than as one more line of facts: it is the one fact of a volume
// that is copied elsewhere — into a bookseller's search, a library catalogue — and a line of
// text gave no sign that it could be. The drawing is QML's; what the bars are is decided here,
// where a test can check every one of the 95 modules against the standard.
//
// **A number that is not an ISBN draws no bars.** Wrong length, a letter where a digit goes,
// a check digit that does not add up, an EAN-13 outside 978 and 979: `read` answers nothing, and the panel keeps the plain
// line it had. A barcode is a claim that the number is right; drawn over a typo, it would make
// the typo look certified.
//
// **Hyphens are shown when the file wrote them, never invented.** Where an ISBN-13 splits —
// after the agency, the publisher, the title — depends on ranges the ISBN agency publishes and
// revises; a table copied here would be wrong the day a range moves. So a number written with
// its five groups (« 978-2-505-06533-3 ») is shown with them, on two lines after the publisher;
// one written as bare digits is shown in plain groups of 3, 4, 5 and 1, separated by spaces
// that claim no structure — the prefix and the check digit are the only boundaries certain.

#include <QList>
#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

class Isbn final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    /// One drawn bar: consecutive dark modules merged, so that a bar three modules wide is one
    /// rounded shape and not three slivers with seams between them. A guard bar — the two at
    /// each end and in the middle — is never merged with a data bar beside it, since it is
    /// drawn longer.
    struct Bar {
        int from = 0;
        int width = 0;
        bool guard = false;
        bool operator==(const Bar &) const = default;
    };

    struct Code {
        /// The thirteen digits the bars encode: what is copied.
        QString digits;
        /// The 95 modules, `1` dark and `0` light, quiet zones excluded.
        QString modules;
        QList<Bar> bars;
        /// The number as shown, on two lines.
        QStringList lines;
    };

    explicit Isbn(QObject *parent = nullptr);

    /// The barcode of an ISBN as a file writes it — hyphens and spaces anywhere, ISBN-10
    /// (with an `X` for ten as its check digit) or ISBN-13 — or nothing when it is not one.
    static std::optional<Code> read(const QString &written);
    /// The 95 modules of thirteen digits; the caller has checked they are thirteen digits.
    static QString modulesOf(const QString &digits);
    static QList<Bar> barsOf(const QString &modules);

    /// `read` for QML: `{ digits, lines, bars: [{ from, width, guard }] }`, or an empty map.
    Q_INVOKABLE QVariantMap barcode(const QString &written) const;
    /// Puts `text` on the clipboard.
    Q_INVOKABLE void copy(const QString &text) const;
};
