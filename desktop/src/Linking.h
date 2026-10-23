#pragma once

// Linking this device to a library, from a `leaf://` link: the screen « Aucune bibliothèque
// reliée » and everything it decides.
//
// Three steps, and the order is the whole point:
//
//   1. **Read.** Whatever is offered — typed, pasted, found on the clipboard, handed over by
//      a click — goes through `Link::read`, so a link that is not exactly Leaf's goes no
//      further than this machine.
//   2. **Check.** `GET /health` at the link's address, held to the link's fingerprint. No
//      code, no key, nothing a stranger answering there could use: a server presenting
//      another certificate learns only that somebody knocked.
//   3. **Trade.** Only on « Relier », the code and the name in the body of `POST /link`;
//      the key that comes back goes straight to `Settings::remember`, with the address and
//      the fingerprint that were checked.
//
// Its own network manager, and not `Server`'s. `Server` talks to the library this device is
// linked to, with that library's key and pin; this talks to a library it is not linked to
// yet, with no key and the pin of a link nobody has accepted. Sharing one manager would mean
// changing the pin of every request on screen to check a link that may be refused.

#include "Link.h"

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QQmlEngine>
#include <QString>

#include <optional>

class QNetworkReply;
class Settings;

class Linking : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(Stage stage READ stage NOTIFY changed)
    /// What went wrong, or what is happening — a refusal, a sentence from `Settings`. Empty
    /// when there is nothing to say.
    Q_PROPERTY(QString sentence READ sentence NOTIFY changed)
    /// « Bibliothèque trouvée : … » and the line under it, once the check has passed; empty
    /// before. Apart from `sentence` because a taken name says both at once.
    Q_PROPERTY(QString found READ found NOTIFY changed)
    Q_PROPERTY(QString detail READ detail NOTIFY changed)
    /// What the field holds, as it was offered.
    Q_PROPERTY(QString typed READ typed NOTIFY changed)
    /// What a click that arrived at a linked device is answered, for the settings to show
    /// where the person is sent. Not a bubble: a bubble can be switched off, and this is not
    /// a failure. Empty until such a click, and again once the settings are left.
    Q_PROPERTY(QString notice READ notice NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    /// Whether « Relier » would do anything.
    Q_PROPERTY(bool canLink READ canLink NOTIFY changed)

    // The screen's own words, held by the object the screen is named after.
    Q_PROPERTY(QString title READ title CONSTANT)
    Q_PROPERTY(QString intro READ intro CONSTANT)
    Q_PROPERTY(QString linkField READ linkField CONSTANT)
    Q_PROPERTY(QString linkPlaceholder READ linkPlaceholder CONSTANT)
    Q_PROPERTY(QString nameField READ nameField CONSTANT)
    Q_PROPERTY(QString nameHelp READ nameHelp CONSTANT)
    Q_PROPERTY(QString action READ action NOTIFY changed)

public:
    enum class Stage {
        Empty,
        NotALink,
        Checking,
        Unreachable,
        Mismatch,
        Found,
        Sending,
        CodeNotValid,
        NameTaken,
        Linked
    };
    Q_ENUM(Stage)

    explicit Linking(Settings *settings, QObject *parent = nullptr);

    /// Built by the engine from the `Settings` singleton, as `Server` is.
    static Linking *create(QQmlEngine *engine, QJSEngine *);

    Stage stage() const { return m_stage; }
    QString sentence() const { return m_sentence; }
    QString found() const { return m_found; }
    QString detail() const { return m_detail; }
    QString typed() const { return m_typed; }
    QString name() const { return m_name; }
    QString notice() const { return m_notice; }
    Q_INVOKABLE void clearNotice();
    void setName(const QString &name);
    bool canLink() const;

    QString title() const;
    QString intro() const;
    QString linkField() const;
    QString linkPlaceholder() const;
    QString nameField() const;
    QString nameHelp() const;
    QString action() const;

    /// The field changed, the clipboard held a link, a link was handed over: read it, and
    /// check the server it names. Ignored while a code is on its way — the answer to it is a
    /// key, and nothing may stand between that key and `Settings`.
    Q_INVOKABLE void offer(const QString &typed);
    /// « Relier »: the code and the name, once. Does nothing unless `canLink`.
    Q_INVOKABLE void link();
    /// Read once, when the screen opens: a link already on the clipboard fills the field.
    /// Anything else there is somebody else's text, and is left alone.
    Q_INVOKABLE void lookAtTheClipboard();
    /// A link that came from outside the window — a click, through the command line or
    /// through the first process. Offered like a pasted one and never sent: the person still
    /// presses « Relier ». Waits for `Settings` to have read its key before deciding, and
    /// says `refusedWhileLinked` instead when there is already a library: a click never
    /// replaces one, it is « Délier » that does.
    void arrive(const QString &link);
    /// The library was let go of — unlinked here, or removed on the server. The screen opens
    /// empty, saying `why` if there is anything to say, and not on the link this device was
    /// made with: that code is spent, and its stage would read « relié ».
    void startOver(const QString &why);

signals:
    void changed();
    /// The key is stored and `Settings` is configured: the shelf can be asked for.
    void linked();
    /// A link arrived at a device that already has a library.
    void refusedWhileLinked();

private:
    void check();
    void heard(QNetworkReply *reply);
    void answered(QNetworkReply *reply);
    void store();
    /// Starts a request to the link's address, held to its pin, after letting go of the one
    /// before. No key: this device has none for that library.
    QNetworkReply *send(const QByteArray &verb, const QString &path, const QByteArray &body);
    /// Lets go of the request in flight, without a word: a cancelled check is not an answer.
    void drop();
    void become(Stage stage, const QString &sentence = {});

    Settings *m_settings;
    QNetworkAccessManager m_network;
    /// The fingerprint every request of `m_network` is held to: the link's.
    QString m_pin;
    std::optional<Link> m_link;
    QPointer<QNetworkReply> m_reply;
    /// The key and name the server gave, kept until `Settings` says it has written them: if
    /// it refuses, « Relier » tries the writing again — the code is spent, and a second
    /// `POST /link` would only be told so.
    std::optional<QString> m_key;
    QString m_linkedName;
    bool m_storing = false;

    Stage m_stage = Stage::Empty;
    QString m_sentence;
    QString m_found;
    QString m_detail;
    QString m_typed;
    QString m_notice;
    QString m_name;
};
