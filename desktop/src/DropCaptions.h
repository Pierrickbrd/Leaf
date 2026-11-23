#pragma once

// What the first step of the import dialog says: the drop zone, the pickers, and how far the
// reading of what was dropped has got.
//
// Split off `ImportCaptions` when that class reached a hundred and three methods — Sonar's
// `cpp:S1448` — by holding every step of the dialog at once. `ImportCaptions.h` says why one
// class per step: a step is a subject a new word can be filed under, "the import" was not.
//
// Nothing here decides anything, and nothing here is bound to the queue: every count it
// quotes arrives as an argument from the QML that already holds it, so a binding is refreshed
// by the property that changed and this class announces nothing.

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

class DropCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString dropHereLabel READ dropHereLabel CONSTANT)
    Q_PROPERTY(QString dropHowLabel READ dropHowLabel CONSTANT)
    Q_PROPERTY(QString dropWhatLabel READ dropWhatLabel CONSTANT)
    Q_PROPERTY(QString reassuranceLabel READ reassuranceLabel CONSTANT)
    Q_PROPERTY(QString chooseLeadLabel READ chooseLeadLabel CONSTANT)
    Q_PROPERTY(QString chooseFilesLabel READ chooseFilesLabel CONSTANT)
    Q_PROPERTY(QString chooseFilesTitle READ chooseFilesTitle CONSTANT)
    Q_PROPERTY(QString chooseFolderLabel READ chooseFolderLabel CONSTANT)
    Q_PROPERTY(QString chooseFolderTitle READ chooseFolderTitle CONSTANT)
    Q_PROPERTY(QString verifyLabel READ verifyLabel CONSTANT)
    Q_PROPERTY(QString verifyAside READ verifyAside CONSTANT)
    Q_PROPERTY(QString everythingIsCheckedLabel READ everythingIsCheckedLabel CONSTANT)
    Q_PROPERTY(QString startWaitsLabel READ startWaitsLabel CONSTANT)
    Q_PROPERTY(QString startWaitsHint READ startWaitsHint CONSTANT)
    /// What a line says when no series could be told from its file, and what accepts the
    /// series a reading would create.
    Q_PROPERTY(QString noSeriesLabel READ noSeriesLabel CONSTANT)
    Q_PROPERTY(QString acceptLabel READ acceptLabel CONSTANT)

public:
    explicit DropCaptions(QObject *parent = nullptr);

    QString dropHereLabel() const;
    QString dropHowLabel() const;
    QString dropWhatLabel() const;
    QString reassuranceLabel() const;
    QString chooseLeadLabel() const;
    QString chooseFilesLabel() const;
    QString chooseFilesTitle() const;
    QString chooseFolderLabel() const;
    QString chooseFolderTitle() const;
    QString verifyLabel() const;
    QString verifyAside() const;
    QString everythingIsCheckedLabel() const;
    QString startWaitsLabel() const;
    QString startWaitsHint() const;
    QString noSeriesLabel() const;
    QString acceptLabel() const;

    /// What its drop zone expects: « Un fichier », « Un dossier de série ».
    Q_INVOKABLE QString aimedDropLabel(const QString &kind) const;
    Q_INVOKABLE QString toSendTotal(int files, qint64 bytes) const;
    Q_INVOKABLE QString readSoFar(int cards) const;
    Q_INVOKABLE QString heldNotResent(int files) const;
    Q_INVOKABLE QString readHowLabel(int origin) const;
    /// The count a line of the choosing step carries, by `progress.state`, and the one line
    /// under the drop zone that says what the whole drop is doing and why « Suivant » waits.
    Q_INVOKABLE QString lineCountLabel(const QVariantMap &progress, bool brief) const;
    Q_INVOKABLE QString dropProgressLabel(const QVariantMap &counts) const;
};
