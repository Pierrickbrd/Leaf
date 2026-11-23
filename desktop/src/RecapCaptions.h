#pragma once

// What the third step of the import dialog says: the recap before anything is sent, the
// banner while it goes, and the marks of each line.
//
// Split off `ImportCaptions` when that class reached a hundred and three methods — Sonar's
// `cpp:S1448` — by holding every step of the dialog at once. `ImportCaptions.h` says why one
// class per step.
//
// Every count here arrives as an argument from the QML that holds it, so this class is bound
// to nothing and announces nothing.

#include <QObject>
#include <QQmlEngine>
#include <QString>

class RecapCaptions final : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    /// The recap's column heads.
    Q_PROPERTY(QString recapSeries READ recapSeries CONSTANT)
    Q_PROPERTY(QString recapVolumes READ recapVolumes CONSTANT)
    Q_PROPERTY(QString recapSize READ recapSize CONSTANT)
    Q_PROPERTY(QString recapState READ recapState CONSTANT)
    Q_PROPERTY(QString recapVerifies READ recapVerifies CONSTANT)
    /// The foot's sentence before a line has gone, and once one has: the third step says what
    /// it is for in each of its two moments.
    Q_PROPERTY(QString recapFoot READ recapFoot CONSTANT)
    Q_PROPERTY(QString sendingFoot READ sendingFoot CONSTANT)
    /// The tooltips of the marks of a line.
    Q_PROPERTY(QString lineStart READ lineStart CONSTANT)
    Q_PROPERTY(QString lineStop READ lineStop CONSTANT)
    Q_PROPERTY(QString lineRetry READ lineRetry CONSTANT)
    Q_PROPERTY(QString lineCancel READ lineCancel CONSTANT)

public:
    explicit RecapCaptions(QObject *parent = nullptr);

    QString recapSeries() const;
    QString recapVolumes() const;
    QString recapSize() const;
    QString recapState() const;
    QString recapVerifies() const;
    QString recapFoot() const;
    QString sendingFoot() const;
    QString lineStart() const;
    QString lineStop() const;
    QString lineRetry() const;
    QString lineCancel() const;

    Q_INVOKABLE QString recapTitle(int files) const;
    Q_INVOKABLE QString recapAbout(qint64 bytes, int cards) const;
    Q_INVOKABLE QString sendingTitle(int done, int whole) const;
    Q_INVOKABLE QString sendingAbout(qint64 sent, qint64 whole) const;
    Q_INVOKABLE QString sentTitle(int done, int whole) const;
    /// The banner's paragraph, which says something different while lines go and after.
    Q_INVOKABLE QString bannerExplains(bool going, bool finished) const;
    Q_INVOKABLE QString sentAbout(qint64 bytes, int failed) const;
    Q_INVOKABLE QString percentOf(double fraction) const;
    Q_INVOKABLE QString sizeOf(qint64 bytes) const;
};
