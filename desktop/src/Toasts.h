#pragma once

// What warns, and where it goes.
//
// **One rule, and it is a rule of restraint.** Something is announced when it finished while
// nobody was looking, and when it failed. Never when it is happening in plain sight: a line
// that turns « Lu » under the pointer needs no bubble, and a notification that repeats what
// the screen already shows teaches people to stop reading notifications.
//
// **Grouped, never one per thing.** An import of thirty files makes one bubble at the end and
// not thirty. It is the commit that warns, not each byte — and forty volumes that fail do not
// make forty bubbles either: past three, the rest are one line saying how many.
//
// **Every bubble goes after ten seconds, and waits while the pointer is on it.** A failure
// used to stay until it was clicked, so that none went unread; tried by hand on 22/10/2026,
// a refusal left over from a moment's disconnection was still covering the corner of the page
// being read long after it meant anything, and a success gone in five seconds went before
// it was read. One duration for both, held while it is being looked at: a bubble that is
// being read is never taken away, and one nobody reads does not stay over the book.
//
// The desktop is the same event escalated, and only when the window is not active: getting
// both for one fact is the surest way to have notifications switched off altogether.

#include "Preferences.h"

#include <QAbstractListModel>
#include <QDateTime>
#include <QQmlEngine>
#include <QString>
#include <QTimer>

#include <functional>

class Toasts : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// How many are drawn — `atOnce` at most, whatever is waiting behind them.
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// The window is short (`Widths.short`, which `ToastStack` hands it): two are drawn and
    /// not three.
    Q_PROPERTY(bool cramped READ cramped WRITE setCramped NOTIFY changed)
    /// How many more there are than the ones drawn. Nought when everything fits.
    Q_PROPERTY(int more READ more NOTIFY changed)
    Q_PROPERTY(QString moreLabel READ moreLabel NOTIFY changed)
    /// Which of the six zones they are laid in, as the reader asked.
    Q_PROPERTY(int corner READ corner NOTIFY changed)
    /// What was said lately, newest first: `[{ failed, headline, detail, when }]`. A bubble
    /// is gone in ten seconds and the import it announced may not be: this is where a reader
    /// who was elsewhere finds out what happened.
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(QString historyTitle READ historyTitle CONSTANT)
    Q_PROPERTY(QString historyEmpty READ historyEmpty CONSTANT)

public:
    /// What the icon says, and nothing else does: the card keeps a card's background. A
    /// bubble filled with colour on a wall of covers would be the flashing light the
    /// architecture already refuses for pills.
    enum class Tone { Done, Failed };
    Q_ENUM(Tone)

    /// The one thing a bubble may offer besides its cross. Never two, and never « OK » —
    /// a button that only closes is the cross with a word on it.
    enum class Offer { Nothing, See, Retry, OpenFolder };
    Q_ENUM(Offer)

    enum class Role { Tone_ = Qt::UserRole, Headline, Detail, Label, Offer_ };
    Q_ENUM(Role)

    /// At most three at a time, the newest at the bottom.
    static constexpr int AtOnce = 3;
    /// At most two on a short window. On 23/10/2026, at 800 × 560, three bubbles and the
    /// line counting the rest stood from the foot of the window to the bar, over the whole
    /// right half of the shelf; two leave the page readable above them.
    static constexpr int AtOnceWhenShort = 2;

    /// How long a bubble is left up, in milliseconds, success and failure alike.
    static constexpr int Lifetime = 10000;

    /// `awake` says whether the window has the focus, and is given rather than read so that a
    /// test can say. `lifetime` is how long a bubble is left up, in milliseconds — a
    /// condition of correctness and not a taste, which is why nothing in the settings screen
    /// offers to change it.
    explicit Toasts(Preferences *preferences, std::function<bool()> awake = {},
                    int lifetime = Lifetime, QObject *parent = nullptr);

    static Toasts *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    bool cramped() const { return m_atOnce == AtOnceWhenShort; }
    /// Draws `AtOnceWhenShort` when cramped, `AtOnce` otherwise: the rest wait behind, as a
    /// count. The rows drawn change all at once, so the view is told the model was reset.
    void setCramped(bool cramped);
    int more() const;
    QString moreLabel() const;
    int corner() const;
    QVariantList history() const;
    QString historyTitle() const;
    QString historyEmpty() const;
    /// How many are kept: enough to cover an evening of imports, few enough to be read.
    static constexpr int Remembered = 20;

    /// Says something. `family` is what the settings screen answers for: silent there is
    /// silent here, and the bubble is not raised at all rather than raised and hidden. What
    /// each event says, and under which family, is `Warnings`'s business.
    void say(Preferences::Warns family, Tone tone, const QString &headline,
             const QString &detail, Offer offer = Offer::Nothing,
             const QString &subject = {});

    /// Lays the bubble, whatever the families say: `say` decides whether to call it, and
    /// `Warnings` calls it directly for the two events that answer to no family — the ISBN
    /// copied and the WebP plugin missing.
    void raise(Tone tone, const QString &headline, const QString &detail, Offer offer,
               const QString &subject);

    /// Forgets what was said: the history belongs to the profile that heard it, and the next
    /// one to sit at this window must not read a series title it has no right to see.
    void forget();

    /// Takes one away — the cross, which is the way out and not an action of its own.
    Q_INVOKABLE void dismiss(int row);
    /// Does what the one button offers, and takes the bubble with it.
    Q_INVOKABLE void act(int row);
    /// The pointer is over a bubble, or has left it: its time stops while it is being read,
    /// and goes on from where it stopped once the pointer leaves.
    Q_INVOKABLE void hold(int row, bool held);
    /// Sweeps what has been up long enough. Driven by a timer; called by a test.
    Q_INVOKABLE void sweep();

signals:
    void changed();
    void historyChanged();
    /// The reader pressed the one thing a bubble offered. Whoever knows what to do with it
    /// is listening — this object knows what was said, not what to do about it.
    void acted(Offer what, const QString &subject);
    /// The same event, for the desktop. Emitted only when the window is not active and the
    /// family is allowed to escalate; a `Notifier` puts it on the bus, because what a bubble
    /// is has nothing to do with what D-Bus is.
    void escalated(const QString &headline, const QString &detail);

private:
    struct Bubble {
        Tone tone = Tone::Done;
        QString headline;
        QString detail;
        Offer offer = Offer::Nothing;
        QString subject;
        /// When it should go.
        QDateTime until;
        /// The pointer is on it, and how long it had left when the pointer came.
        bool held = false;
        qint64 left = 0;
    };

    void take(qsizetype row);

    struct Memory {
        Tone tone = Tone::Done;
        QString headline;
        QString detail;
        QDateTime at;
    };

    Preferences *m_preferences;
    QList<Memory> m_history;
    std::function<bool()> m_awake;
    int m_lifetime;
    int m_atOnce = AtOnce;
    QList<Bubble> m_shown;
    QTimer m_clock;
};
