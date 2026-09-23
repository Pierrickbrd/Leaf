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
// **A failure stays until it is clicked.** A failure that fades on its own is a failure
// nobody read. What succeeded goes after five seconds, because it is already true.
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

    /// How many are drawn — three at most, whatever is waiting behind them.
    Q_PROPERTY(int count READ count NOTIFY changed)
    /// How many more there are than the three drawn. Nought when everything fits.
    Q_PROPERTY(int more READ more NOTIFY changed)
    Q_PROPERTY(QString moreLabel READ moreLabel NOTIFY changed)
    /// Which of the six zones they are laid in, as the reader asked.
    Q_PROPERTY(int corner READ corner NOTIFY changed)

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

    /// `awake` says whether the window has the focus, and is given rather than read so that a
    /// test can say. `lifetime` is how long a success is left up, in milliseconds — a
    /// condition of correctness and not a taste, which is why nothing in the settings screen
    /// offers to change it.
    explicit Toasts(Preferences *preferences, std::function<bool()> awake = {},
                    int lifetime = 5000, QObject *parent = nullptr);

    static Toasts *create(QQmlEngine *engine, QJSEngine *);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return rowCount(); }
    int more() const;
    QString moreLabel() const;
    int corner() const;

    /// Says something. `family` is what the settings screen answers for: silent there is
    /// silent here, and the bubble is not raised at all rather than raised and hidden.
    void say(Preferences::Warns family, Tone tone, const QString &headline,
             const QString &detail, Offer offer = Offer::Nothing,
             const QString &subject = {});

    /// The five things that warn, each worded where the words live. A `.qml` connecting a
    /// signal to one of these is a `.qml` that carries no French, which is the whole
    /// arrangement of this client.
    Q_INVOKABLE void importSettled(bool went, const QString &subject, const QString &said);
    Q_INVOKABLE void scanFinished(const QString &counts);
    Q_INVOKABLE void scanFailed(const QString &why);
    Q_INVOKABLE void copySaved(const QString &path);
    Q_INVOKABLE void commandRefused(const QString &why);

    /// Takes one away — the cross, which is the way out and not an action of its own.
    Q_INVOKABLE void dismiss(int row);
    /// Does what the one button offers, and takes the bubble with it.
    Q_INVOKABLE void act(int row);
    /// Sweeps what has been up long enough. Driven by a timer; called by a test.
    Q_INVOKABLE void sweep();

signals:
    void changed();
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
        /// When it should go, and invalid for a failure: those stay until they are clicked.
        QDateTime until;
    };

    void take(qsizetype row);

    Preferences *m_preferences;
    std::function<bool()> m_awake;
    int m_lifetime;
    QList<Bubble> m_shown;
    QTimer m_clock;
};
