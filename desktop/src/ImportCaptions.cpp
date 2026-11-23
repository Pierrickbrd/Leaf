#include "ImportCaptions.h"

#include "Imports.h"
#include "Words.h"

#include <QDebug>
#include <QQmlEngine>

using namespace Qt::StringLiterals;

ImportCaptions *ImportCaptions::create(QQmlEngine *engine, QJSEngine *)
{
    auto *imports = engine->singletonInstance<Imports *>(qmlTypeId("Leaf", 1, 0, "Imports"));
    if (!imports) {
        qWarning().noquote()
            << QStringLiteral("error resolving the Imports singleton — the import dialog will "
                              "have no words");
    }
    return new ImportCaptions(imports);
}

ImportCaptions::ImportCaptions(Imports *imports, QObject *parent)
    : QObject(parent)
    , m_imports(imports)
{
    // `waitingLabel` quotes `Imports::deciding()`, which moves as the queue is answered —
    // re-announcing on every `Imports::changed` is what keeps a binding on it from freezing
    // on whatever count happened to be true when the dialog first opened.
    if (m_imports)
        connect(m_imports, &Imports::changed, this, &ImportCaptions::changed);
}

QString ImportCaptions::title() const
{
    return Words::importing();
}

QString ImportCaptions::waitingLabel() const
{
    // A card stopped to be checked waits for a person as surely as one asking for a tick:
    // counted apart, the bar said nothing over a Komga folder dropped and the window shut.
    return Words::decisionsWaiting(m_imports ? m_imports->deciding() + m_imports->reviewing()
                                             : 0);
}

QStringList ImportCaptions::stepLabels() const
{
    return {Words::importStep(1), Words::importStep(2), Words::importStep(3)};
}

QString ImportCaptions::nextLabel() const
{
    return Words::next();
}

QString ImportCaptions::previousLabel() const
{
    return Words::previous();
}

QString ImportCaptions::cancelLabel() const
{
    return Words::cancel();
}

QString ImportCaptions::closeLabel() const
{
    return Words::closeIt();
}

QString ImportCaptions::backLabel() const
{
    return Words::goBack();
}

QString ImportCaptions::returnLabel() const
{
    return Words::back();
}

QString ImportCaptions::pauseLabel() const
{
    return Words::pauseIt();
}

QString ImportCaptions::resumeLabel() const
{
    return Words::resumeIt();
}

QString ImportCaptions::abandonLabel() const
{
    return Words::abandonIt();
}

QString ImportCaptions::sendLabel() const
{
    return Words::importAll();
}

QString ImportCaptions::finishLabel() const
{
    return Words::finishIt();
}

QString ImportCaptions::removeLabel() const
{
    return Words::removeIt();
}

QString ImportCaptions::emptyLabel() const
{
    return Words::emptyTheQueue();
}

QString ImportCaptions::anotherFileLabel() const
{
    return Words::anotherFile();
}

QString ImportCaptions::reimportTitle(const QString &kind, const QString &name) const
{
    return Words::reimportAsked(kind, name);
}

QString ImportCaptions::reimportHolds(const QString &kind, int volumes) const
{
    if (kind == u"SERIES_NEW_EDITIONS"_s)
        return Words::addEditionsSubtitle();
    return kind == u"SERIES"_s && volumes > 0 ? Words::reimportHolds(volumes) : QString();
}

QString ImportCaptions::reimportUniverseLabel(const QString &name) const
{
    return Words::reimportUniverse(name);
}
